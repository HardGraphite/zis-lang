#include "objmem.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h> // abort()
#include <string.h>

#include "algorithm.h"
#include "attributes.h"
#include "bits.h"
#include "compat.h"
#include "context.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "typeobj.h"

#include "zis_config.h"

#if ZIS_DEBUG
#    include <stdio.h>
#    include <time.h>
#endif // ZIS_DEBUG

/* ----- Configurations ----------------------------------------------------- */

#define OBJECT_POINTER_SIZE            (sizeof(struct zis_object *))
#define SIZE_KiB(N)                    ((N) * (size_t)1024)
#define SIZE_MiB(N)                    (SIZE_KiB((N)) * (size_t)1024)
#define SIZE_GiB(N)                    (SIZE_MiB((N)) * (size_t)1024)

#define NON_BIG_SPACE_MAX_ALLOC_SIZE   (OBJECT_POINTER_SIZE * SIZE_KiB(1))

#define NEW_SPACE_CHUNK_SIZE_MIN       (OBJECT_POINTER_SIZE * SIZE_KiB(4))
#define NEW_SPACE_CHUNK_SIZE_DFL       (OBJECT_POINTER_SIZE * SIZE_KiB(64))

#define OLD_SPACE_CHUNK_SIZE_MIN       (OBJECT_POINTER_SIZE * SIZE_KiB(4))
#define OLD_SPACE_CHUNK_SIZE_DFL       (OBJECT_POINTER_SIZE * SIZE_KiB(32))
#define OLD_SPACE_SIZE_LIMIT_DFL       (SIZE_GiB(1))

#define BIG_SPACE_THRESHOLD_INIT_DFL   (16 * NON_BIG_SPACE_MAX_ALLOC_SIZE)
#define BIG_SPACE_SIZE_LIMIT_DFL       (SIZE_GiB(1))

static_assert(NON_BIG_SPACE_MAX_ALLOC_SIZE >= SIZE_KiB(4), "");
static_assert(NEW_SPACE_CHUNK_SIZE_DFL >= NEW_SPACE_CHUNK_SIZE_MIN, "");
static_assert(OLD_SPACE_CHUNK_SIZE_DFL >= OLD_SPACE_CHUNK_SIZE_MIN, "");
static_assert(NEW_SPACE_CHUNK_SIZE_MIN > NON_BIG_SPACE_MAX_ALLOC_SIZE * 2, "");
static_assert(OLD_SPACE_CHUNK_SIZE_MIN > NON_BIG_SPACE_MAX_ALLOC_SIZE * 2, "");

struct objmem_config {
    size_t new_spc_chunk_size;
    size_t old_spc_chunk_size;
    size_t old_spc_size_limit;
    size_t big_spc_threshold_init;
    size_t big_spc_size_limit;
};

static void objmem_config_conv(struct objmem_config *config, const struct zis_objmem_options *opts) {
    // new space
    if (opts->new_space_size == 0)
        config->new_spc_chunk_size = NEW_SPACE_CHUNK_SIZE_DFL;
    else if (opts->new_space_size < NEW_SPACE_CHUNK_SIZE_MIN * 2)
        config->new_spc_chunk_size = NEW_SPACE_CHUNK_SIZE_MIN;
    else
        config->new_spc_chunk_size = opts->new_space_size / 2;
    // old space
    if (opts->old_space_size_new == 0)
        config->old_spc_chunk_size = OLD_SPACE_CHUNK_SIZE_DFL;
    else if (opts->old_space_size_new < OLD_SPACE_CHUNK_SIZE_MIN)
        config->old_spc_chunk_size = OLD_SPACE_CHUNK_SIZE_MIN;
    else
        config->old_spc_chunk_size = opts->old_space_size_new;
    if (opts->old_space_size_max == 0)
        config->old_spc_size_limit = OLD_SPACE_SIZE_LIMIT_DFL;
    else if (opts->old_space_size_max < config->old_spc_chunk_size)
        config->old_spc_size_limit = config->old_spc_chunk_size;
    else
        config->old_spc_size_limit = opts->old_space_size_max;
    // big space
    if (opts->big_space_size_new == 0)
        config->big_spc_threshold_init = BIG_SPACE_THRESHOLD_INIT_DFL;
    else
        config->big_spc_threshold_init = opts->big_space_size_new;
    if (opts->big_space_size_max == 0)
        config->big_spc_size_limit = BIG_SPACE_SIZE_LIMIT_DFL;
    else
        config->big_spc_size_limit = opts->big_space_size_max;
}

/* ----- Memory span set with function pointer ------------------------------ */

/// A record of span.
struct mem_span_set_node {
    struct mem_span_set_node *_next;
    void *span_addr;
    void (*function)(void);
};

/// A set of memory span.
struct mem_span_set {
    struct mem_span_set_node *_nodes;
};

static_assert(
    offsetof(struct mem_span_set_node, _next)
        == offsetof(struct mem_span_set, _nodes),
    "");

/// Iterate over nodes. Nodes will not be accessed after `STMT`.
#define mem_span_set_foreach_node(set, NODE_VAR, STMT) \
    do {                                               \
        struct mem_span_set_node *__next_node;         \
        for (struct mem_span_set_node *__node = (set)->_nodes; \
            __node; __node = __next_node               \
        ) {                                            \
            __next_node = __node->_next;               \
            { struct mem_span_set_node *NODE_VAR = __node; STMT } \
        }                                              \
    } while (0)                                        \
// ^^^ mem_span_set_foreach_node() ^^^

/// Iterate over nodes and the predecessors.
#define mem_span_set_foreach_node_2(set, PREV_NODE_VAR, NODE_VAR, STMT) \
    do {                                                                \
        struct mem_span_set_node *__node;                               \
        struct mem_span_set_node *PREV_NODE_VAR =                       \
            (struct mem_span_set_node *)(set);                          \
        for (; ; PREV_NODE_VAR = __node) {                              \
            __node = (PREV_NODE_VAR) ->_next;                           \
            if (!__node)                                                \
                break;                                                  \
            { struct mem_span_set_node *NODE_VAR = __node; STMT }       \
        }                                                               \
    } while (0)                                                         \
// ^^^ mem_span_set_foreach_node_2() ^^^

/// Iterate over records.
#define mem_span_set_foreach(set, SPAN_T, SPAN_VAR, FN_T, FN_VAR, STMT) \
    do {                                                                \
        struct mem_span_set_node *__next_node;                          \
        for (struct mem_span_set_node *__node = (set)->_nodes;          \
            __node; __node = __next_node                                \
        ) {                                                             \
            __next_node = __node->_next;                                \
            {                                                           \
                SPAN_T SPAN_VAR = (SPAN_T)__node->span_addr;            \
                FN_T FN_VAR     = (FN_T)__node->function;               \
                STMT                                                    \
            }                                                           \
        }                                                               \
    } while (0)                                                         \
// ^^^ mem_span_set_foreach() ^^^

/// Initialize the set.
static void mem_span_set_init(struct mem_span_set *set) {
    set->_nodes = NULL;
}

/// Finalize the set.
static void mem_span_set_fini(struct mem_span_set *set) {
    assert(!set->_nodes);
    mem_span_set_foreach_node(set, node, { zis_mem_free(node); });
    set->_nodes = NULL;
}

/// Search for a record. If found, returns the predecessor node; otherwise returns `NULL`.
static struct mem_span_set_node *mem_span_set_find(
    struct mem_span_set *set, void *span_addr,
    struct mem_span_set_node **predecessor_node /* = NULL */
) {
    mem_span_set_foreach_node_2(set, prev_node, node, {
        if (node->span_addr == span_addr) {
            if (predecessor_node)
                *predecessor_node = prev_node;
            return node;
        }
    });
    return NULL;
}

/// Add a record or update a existing one.
static void mem_span_set_add(
    struct mem_span_set *set,
    void *span_addr, void(*function)(void)
) {
    struct mem_span_set_node *node =
        mem_span_set_find(set, span_addr, NULL);
    if (node) {
        assert(node->span_addr == span_addr);
        node->function = function;
    } else {
        node = zis_mem_alloc(sizeof(struct mem_span_set_node));
        node->_next = set->_nodes;
        node->span_addr = span_addr;
        node->function = function;
        set->_nodes = node;
    }
}

/// Remove a record. Return whether successful.
static bool mem_span_set_remove(struct mem_span_set *set, void *span_addr) {
    struct mem_span_set_node *node, *prev_node;
    node = mem_span_set_find(set, span_addr, &prev_node);
    if (!node)
        return false;
    assert(node == prev_node->_next);
    prev_node->_next = node->_next;
    zis_mem_free(node);
    return true;
}

/* ----- Memory chunk ------------------------------------------------------- */

/// A huge block of memory from where smaller memory block can be allocated.
struct mem_chunk {

    /*
     * +------+-------------+----------+
     * | Meta | Allocated   | Free     |
     * +------+-------------+----------+
     *         ^             ^          ^
     *         _mem       _free      _end
     */

    char             *_free;
    char             *_end;
    struct mem_chunk *_next;
    char              _mem[];
};

/// A `struct mem_chunk` without the member variable `_mem`.
/// Nested flexible array member seems to be invalid. This struct shall only
/// be used as member variable in other struct as a replacement of `struct mem_chunk`.
struct empty_mem_chunk {
    char             *_free;
    char             *_end;
    struct mem_chunk *_next;
};

static_assert(
    sizeof(struct mem_chunk) == sizeof(struct empty_mem_chunk) &&
    offsetof(struct mem_chunk, _free) == offsetof(struct empty_mem_chunk, _free) &&
    offsetof(struct mem_chunk, _end) == offsetof(struct empty_mem_chunk, _end) &&
    offsetof(struct mem_chunk, _next) == offsetof(struct empty_mem_chunk, _next),
    "struct empty_mem_chunk"
);

/// Allocate a chunk (virtual memory).
static struct mem_chunk *mem_chunk_create(size_t size) {
    assert(size > sizeof(struct mem_chunk));
    struct mem_chunk *const chunk = zis_vmem_alloc(size);
    assert(chunk);
    chunk->_free = chunk->_mem;
    chunk->_end  = (char *)chunk + size;
    chunk->_next = NULL;
    return chunk;
}

/// Deallocate a chunk.
static void mem_chunk_destroy(struct mem_chunk *chunk) {
    assert(chunk->_end >= chunk->_mem);
    zis_vmem_free(chunk, (size_t)(chunk->_end - (char *)chunk));
}

/// Allocate from the chunk. On failure (no enough space), returns `NULL`.
zis_force_inline static void *mem_chunk_alloc(struct mem_chunk *chunk, size_t size) {
    assert(size > 0 && !(size & (sizeof(void *) - 1)));
    char *const ptr = chunk->_free;
    char *const new_free = ptr + size;
    if (zis_unlikely(new_free >= chunk->_end))
        return NULL;
    chunk->_free = new_free;
    return ptr;
}

/// Forget allocations, i.e., reset the "free" pointer.
static void mem_chunk_forget(struct mem_chunk *chunk) {
    chunk->_free = chunk->_mem;
}

/// Get allocated range (`{&begin, &end}`).
static void mem_chunk_allocated(
    struct mem_chunk *chunk,
    void **region[ZIS_PARAMARRAY_STATIC 2] /*{&begin, &end}*/
) {
    *region[0] = chunk->_mem;
    *region[1] = chunk->_free;
}

/// Assume all allocations are for objects. Iterate over allocated objects.
#define mem_chunk_foreach_allocated_object(                                    \
    chunk, begin_offset, OBJ_VAR, OBJ_TYPE_VAR, OBJ_SIZE_VAR, STMT             \
)                                                                              \
    do {                                                                       \
        void *allocated_begin, *allocated_end;                                 \
        mem_chunk_allocated(chunk, (void **[]){&allocated_begin, &allocated_end}); \
        allocated_begin = (char *)allocated_begin + begin_offset;              \
        size_t __obj_size;                                                     \
        for (struct zis_object *__this_obj = allocated_begin;                  \
            (void *)__this_obj < allocated_end;                                \
            __this_obj = (struct zis_object *)((char *)__this_obj + __obj_size)\
        ) {                                                                    \
            __obj_size = zis_object_size(__this_obj);                          \
            struct zis_object *const OBJ_VAR = __this_obj;                     \
            struct zis_type_obj *const OBJ_TYPE_VAR = zis_object_type(__this_obj); \
            const size_t OBJ_SIZE_VAR = __obj_size;                            \
            STMT                                                               \
        }                                                                      \
    } while (0)                                                                \
// ^^^ new_space_foreach_allocated() ^^^

/// A list of `struct mem_chunk`.
struct mem_chunk_list {

    /*
     * Node-0    Node-1   ...    Last node
     * +-+--+   +-+--+           +-+--+
     * | | -+-->| | -+--> ... -->| | -+--> X
     * +-+--+   +-+--+           +-+--+
     *  ^                         ^
     *  _head.next                _tail
     */

    struct mem_chunk *_tail;
    struct empty_mem_chunk _head;
};

/// Iterate over chunks.
#define mem_chunk_list_foreach(LIST, CHUNK_VAR, STMT) \
    do {                                              \
        struct mem_chunk *__chunk;                    \
        struct mem_chunk *__prev_chunk = _mem_chunk_list_head((LIST));\
        for (; ; __prev_chunk = __chunk) {            \
            __chunk = __prev_chunk->_next;            \
            if (!__chunk)                             \
                break;                                \
            {                                         \
                struct mem_chunk *CHUNK_VAR = __chunk;\
                STMT                                  \
            }                                         \
        }                                             \
    } while (0)                                       \
// ^^^ mem_chunk_list_foreach() ^^^

static void _mem_chunk_list_del_from(struct mem_chunk *chunk) {
    while (chunk) {
        struct mem_chunk *const next = chunk->_next;
        mem_chunk_destroy(chunk);
        chunk = next;
    }
}

zis_force_inline static struct mem_chunk *
_mem_chunk_list_head(struct mem_chunk_list *list) {
    return (struct mem_chunk *)(&list->_head);
}

/// Initialize the list.
static void mem_chunk_list_init(struct mem_chunk_list *list) {
    list->_tail = _mem_chunk_list_head(list);
    list->_head._free = _mem_chunk_list_head(list)->_mem;
    list->_head._end  = _mem_chunk_list_head(list)->_mem;
    list->_head._next = NULL;
    assert(!mem_chunk_alloc(_mem_chunk_list_head(list), ZIS_OBJECT_HEAD_SIZE));
}

/// Delete all chunks in the list.
static void mem_chunk_list_fini(struct mem_chunk_list *list) {
    _mem_chunk_list_del_from(list->_head._next);
    list->_head._next = NULL;
}

/// Get first chunk.
static struct mem_chunk *mem_chunk_list_front(struct mem_chunk_list *list) {
    return _mem_chunk_list_head(list);
}

/// Get last chunk.
static struct mem_chunk *mem_chunk_list_back(struct mem_chunk_list *list) {
    return list->_tail;
}

/// Check if the list contains the given chunk.
zis_unused_fn static bool mem_chunk_list_contains(
    struct mem_chunk_list *list, const struct mem_chunk *chunk
) {
    mem_chunk_list_foreach(list, c, {
        if (c == chunk)
            return true;
    });
    return false;
}

/// Add chunk to the list tail.
static void mem_chunk_list_append(
    struct mem_chunk_list *list, struct mem_chunk *chunk
) {
    assert(!chunk->_next);
    assert(!list->_tail->_next);
    list->_tail->_next = chunk;
    list->_tail = chunk;
}

/// Remove chunks after the given one.
static void mem_chunk_list_pop_after(
    struct mem_chunk_list *list, const struct mem_chunk *after_chunk
) {
    assert(mem_chunk_list_contains(list, after_chunk));
    list->_tail = (struct mem_chunk *)after_chunk;
    list->_tail->_next = NULL;
}

/// Create chunk and add it to the list tail.
static struct mem_chunk *mem_chunk_list_append_created(
    struct mem_chunk_list *list, size_t chunk_size
) {
    struct mem_chunk *const chunk = mem_chunk_create(chunk_size);
    mem_chunk_list_append(list, chunk);
    return chunk;
}

/// Delete chunks after the given one.
static void mem_chunk_list_destroy_after(
    struct mem_chunk_list *list, const struct mem_chunk *after_chunk
) {
    struct mem_chunk *const first_chunk = after_chunk->_next;
    mem_chunk_list_pop_after(list, after_chunk);
    _mem_chunk_list_del_from(first_chunk);
}

/* ----- Big space (old generation, large objects) -------------------------- */

/*
 * In big space, mark-sweep GC algorithm is used.
 * All allocated objects are put in a linked list.
 * The GC_PTR in object meta stores the next object in the list.
 * `GC_PTR & 0b0100` indicates whether this object contains references to young objects.
 */

struct _big_space_head {
    ZIS_OBJECT_HEAD
};

static_assert(
    sizeof(struct _big_space_head) == sizeof(struct zis_object),
    "struct _big_space_head"
);

/// Big space manager.
struct big_space {
    size_t allocated_size;
    size_t threshold_size;
    struct _big_space_head _head; // Fake object.
};

zis_force_inline static struct zis_object *_big_space_head(struct big_space *space) {
    return (struct zis_object *)&space->_head;
}

#define big_space_make_meta_ptr_data(NEXT_OBJ, YOUNG_REF) \
    ( assert(!((uintptr_t)NEXT_OBJ & 7)), ((uintptr_t)NEXT_OBJ | (YOUNG_REF ? 4 : 0)) )

#define big_space_unpack_meta_ptr_data(PTR_DATA, NEXT_OBJ_VAR, YOUNG_REF_VAR) \
    do {                                                                      \
        NEXT_OBJ_VAR  = (struct zis_object *)(PTR_DATA & ~(uintptr_t)7);      \
        YOUNG_REF_VAR = PTR_DATA & 7;                                         \
    } while (0)

/// Iterate over objects. Object will not be access after `STMT`.
#define big_space_foreach(space, OBJ_VAR, OBJ_HAS_YOUNG_VAR, STMT) \
    do {                                                           \
        struct zis_object *__obj, *__next_obj;                     \
        bool __young_ref;                                          \
        for (__obj = _big_space_get_first((space)); __obj; __obj = __next_obj) { \
            big_space_unpack_meta_ptr_data(                        \
                zis_object_meta_get_gc_ptr(__obj->_meta, uintptr_t),             \
                __next_obj, __young_ref                            \
            );                                                     \
            {                                                      \
                struct zis_object *const OBJ_VAR = __obj;          \
                const bool OBJ_HAS_YOUNG_VAR = __young_ref;        \
                STMT                                               \
            }                                                      \
        }                                                          \
    } while (0)                                                    \
// ^^^ big_space_foreach() ^^^

/// Iterate over objects. Providing the predecessors.
#define big_space_foreach_2(space, PREV_OBJ_VAR, OBJ_VAR, STMT) \
    do {                                                        \
        struct zis_object *__prev_obj, *__this_obj;             \
        bool __young_ref;                                       \
        for (__prev_obj = _big_space_head(space); ; __prev_obj = __this_obj) { \
            big_space_unpack_meta_ptr_data(                     \
                zis_object_meta_get_gc_ptr(__prev_obj->_meta, uintptr_t),      \
                __this_obj, __young_ref                         \
            );                                                  \
            zis_unused_var(__young_ref);                        \
            if (zis_unlikely(!__this_obj))                      \
                break;                                          \
            {                                                   \
                struct zis_object *const PREV_OBJ_VAR = __prev_obj;            \
                struct zis_object *const OBJ_VAR      = __this_obj;            \
                STMT                                            \
            }                                                   \
        }                                                       \
    } while (0)                                                 \
// ^^^ big_space_foreach_2() ^^^

static struct zis_object *_big_space_get_first(struct big_space *space) {
    const uintptr_t ptr_data =
        zis_object_meta_get_gc_ptr(space->_head._meta, uintptr_t);
    assert(!(ptr_data & 7));
    return (struct zis_object *)ptr_data;
}

static void _big_space_set_first(struct big_space *space, struct zis_object *obj) {
    zis_object_meta_set_gc_ptr(
        space->_head._meta,
        big_space_make_meta_ptr_data(obj, false)
    );
}

/// Initialize space.
static void big_space_init(struct big_space *space, const struct objmem_config *conf) {
    space->allocated_size = 0U;
    space->threshold_size = conf->big_spc_threshold_init;
    zis_object_meta_init(space->_head._meta, ZIS_OBJMEM_OBJ_BIG, 0U, NULL);
}

/// Finalize allocated objects and the space.
static void big_space_fini(struct big_space *space) {
    big_space_foreach(space, obj, has_young, {
        zis_unused_var(has_young);
        // NOTE: object terminates here.
        zis_mem_free(obj);
    });
}

#if ZIS_DEBUG

zis_unused_fn
static void big_space_print_usage(struct big_space *space, FILE *stream) {
    fprintf(
        stream, "<BigSpc threshold_size=\"%zu\" allocated_size=\"%zu\">\n",
        space->threshold_size , space->allocated_size
    );
    big_space_foreach(space, obj, has_young, {
        fprintf(
            stream,
            "  <obj addr=\"%p\" has_young=\"%s\" />\n",
            (void *)obj,
            has_young ? "yes" : "no"
        );
    });
    fputs("</BigSpc>\n", stream);
}

#endif // ZIS_DEBUG

/// Allocate storage for an object. On failure, returns `NULL`.
zis_force_inline static struct zis_object *
big_space_alloc(struct big_space *space, void *type_ptr, size_t size) {
    assert(size >= sizeof(struct zis_object_meta));
    const size_t new_allocated_size = space->allocated_size + size;
    if (zis_unlikely(new_allocated_size > space->threshold_size))
        return NULL;
    space->allocated_size = new_allocated_size;
    struct zis_object *const obj = zis_mem_alloc(size);
    const uintptr_t ptr_data =
        big_space_make_meta_ptr_data(_big_space_get_first(space), false);
    _big_space_set_first(space, obj);
    zis_object_meta_assert_ptr_fits(ptr_data);
    zis_object_meta_assert_ptr_fits(type_ptr);
    zis_object_meta_init(obj->_meta, ZIS_OBJMEM_OBJ_BIG, ptr_data, type_ptr);
    return obj;
}

/// Write barrier: mark object containing young reference.
zis_force_inline static void big_space_remember_object(struct zis_object *obj) {
    struct zis_object *next_obj;
    bool orig_young_ref;
    big_space_unpack_meta_ptr_data(
        zis_object_meta_get_gc_ptr(obj->_meta, uintptr_t),
        next_obj, orig_young_ref
    );
    if (!orig_young_ref) {
        uintptr_t new_ptr_data = big_space_make_meta_ptr_data(next_obj, true);
        zis_object_meta_assert_ptr_fits(new_ptr_data);
        zis_object_meta_set_gc_ptr(obj->_meta, new_ptr_data);
    }
}

/// Fast GC: mark young slots of remembered objects. Return number of found objects.
static size_t big_space_mark_remembered_objects_young_slots(
    struct big_space *space
) {
    size_t count = 0;
    big_space_foreach(space, obj, has_young, {
        if (zis_unlikely(has_young)) {
            count++;
            assert(zis_object_meta_is_not_young(obj->_meta));
            _zis_objmem_mark_object_slots_rec_o2y(obj);
        }
    });
    return count;
}

/// Fast GC: Update references in remembered objects and clear the remembered flags.
static size_t big_space_update_remembered_objects_references_and_forget_remembered_objects(
    struct big_space *space, size_t hint_max_count
) {
    size_t count = 0;
    big_space_foreach(space, obj, has_young, {
        if (zis_unlikely(count >= hint_max_count))
            break;
        if (zis_unlikely(has_young)) {
            count++;
            // Update reference.
            _zis_objmem_move_object_slots(obj);
            // Clear remembered flag.
            void *const next_obj = __next_obj; // `__next_obj` is defined in `big_space_foreach()`.
            zis_object_meta_set_gc_ptr(
                obj->_meta,
                big_space_make_meta_ptr_data(next_obj, false)
            );
        }
    });
    return count;
}

/// Full GC: delete unreachable objects and clear flags of reachable objects
/// (including GC marks and remembered flags).
static void big_space_delete_unreachable_objects_and_reset_reachable_objects(
    struct big_space *space
) {
    size_t deleted_size = 0;

    big_space_foreach_2(space, prev_obj, obj, {
        void *next_obj;
        bool obj_has_young;
        big_space_unpack_meta_ptr_data(
            zis_object_meta_get_gc_ptr(obj->_meta, uintptr_t),
            next_obj, obj_has_young
        );
        if (zis_likely(zis_object_meta_test_gc_mark(obj->_meta))) {
            // Clear mark.
            zis_object_meta_reset_gc_mark(obj->_meta);
            // Clear remembered flag.
            if (zis_unlikely(obj_has_young)) {
                zis_object_meta_set_gc_ptr(
                    obj->_meta,
                    big_space_make_meta_ptr_data(next_obj, false)
                );
            }
        } else {
            // Delete object.
            const size_t obj_size = zis_object_size(obj);
            // NOTE: object terminates here.
            deleted_size += obj_size;
            zis_mem_free(obj);
            // Remove list node.
            zis_object_meta_set_gc_ptr(
                prev_obj->_meta,
                big_space_make_meta_ptr_data(next_obj, false)
            );
            // Backward.
            __this_obj = __prev_obj; // `__this_obj` and `__prev_obj` are define in `big_space_foreach_2()`.
            // FIXME: This is ugly.
        }
    });

    assert(deleted_size <= space->allocated_size);
    space->allocated_size -= deleted_size;
}

/// Full GC: update references to objects. Unreachable objects shall have been deleted.
static void big_space_update_references(struct big_space *space) {
    big_space_foreach(space, obj, has_young, {
        zis_unused_var(has_young);
        _zis_objmem_move_object_slots(obj);
    });
}

#if ZIS_DEBUG

zis_unused_fn
static int big_space_post_gc_check(struct big_space *space) {
    big_space_foreach(space, obj, has_young, {
        if (has_young)
            return -1;
        if (zis_object_meta_get_gc_state(obj->_meta) != ZIS_OBJMEM_OBJ_BIG)
            return -2;
        if (zis_object_meta_test_gc_mark(obj->_meta))
            return -3;
    });
    return 0;
}

#endif // ZIS_DEBUG

/* ----- Old space (old generation) ----------------------------------------- */

/*
 * In old space, mark-compact GC algorithm is used.
 * Object storage is allocated from chunks, while the chunks are put in a list.
 * The GC_PTR in object meta stores a pointer to chunk meta when GC is not running.
 * A remembered set is available for each chunk (a pointer at the beginning of chunk)
 * indicating which objects in this chunk contains references to young objects.
 */

#define OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_BITS 1024
#define OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_SIZE \
    zis_bitset_required_size(OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_BITS)

/// Remembered set for a chunk. It records offsets in the chunk.
struct old_space_chunk_remembered_set {

    /*
     * +-------+
     * |bucket |
     * | _count|
     * +-------+
     * |buckets|
     * |       |   +----------------------------+
     * | [0] ----->| bitset, `BUCKET_BITS` bits |
     * |       |   +----------------------------+
     * | [1] ----->(NULL, empty)
     * |       |
     * | [2] ----->(NULL, empty)
     * |       |
     * |  ...  |    ...
     *
     */

    size_t            _bucket_count;
    struct zis_bitset *_buckets[];
};

/// Create an empty remembered set.
static struct old_space_chunk_remembered_set *
old_space_chunk_remembered_set_create(size_t chunk_size) {
    const size_t bucket_count =
        chunk_size / sizeof(void *) / OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_BITS;
    struct old_space_chunk_remembered_set *const set = zis_mem_alloc(
        sizeof(struct old_space_chunk_remembered_set)
            + sizeof(struct zis_bitset *) * bucket_count
    );
    set->_bucket_count = bucket_count;
    memset(set->_buckets, 0, sizeof(struct zis_bitset *) * bucket_count);
    return set;
}

/// Delete a remembered set.
static void old_space_chunk_remembered_set_destroy(
    struct old_space_chunk_remembered_set *set
) {
    for (size_t i = 0, n = set->_bucket_count; i < n; i++) {
        struct zis_bitset *const b = set->_buckets[i];
        zis_mem_free(b);
    }
    zis_mem_free(set);
}

/// Record an offset.
zis_force_inline static void old_space_chunk_remembered_set_record(
    struct old_space_chunk_remembered_set *set, size_t offset
) {
    assert(!(offset & (sizeof(void *) - 1)));
    offset /= sizeof(void *);
    const size_t bucket_index = offset / OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_BITS;
    const size_t bit_index    = offset % OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_BITS;
    assert(bucket_index < set->_bucket_count);
    struct zis_bitset *bucket = set->_buckets[bucket_index];
    if (zis_unlikely(!bucket)) {
        bucket = zis_mem_alloc(OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_SIZE);
        zis_bitset_clear(bucket, OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_SIZE);
        set->_buckets[bucket_index] = bucket;
    }
    zis_bitset_try_set_bit(bucket, bit_index);
}

/// Iterate over records.
#define old_space_chunk_remembered_set_foreach(SET_PTR, OFFSET_VAR, STMT) \
    do {                                                                  \
        struct old_space_chunk_remembered_set *const set = (SET_PTR);     \
        for (size_t i = 0, n = set->_bucket_count; i < n; i++) {          \
            struct zis_bitset *const bucket = set->_buckets[i];            \
            if (!bucket)                                                  \
                continue;                                                 \
            const size_t offset_base =                                    \
                i * OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_BITS * sizeof(void *); \
            zis_bitset_foreach_set(                                        \
                bucket, OLD_SPACE_CHUNK_REMEMBERED_SET_BUCKET_SIZE, bit_index,   \
            {                                                             \
                const size_t OFFSET_VAR =                                 \
                    offset_base | bit_index * sizeof(void *);             \
                { STMT }                                                  \
            });                                                           \
        }                                                                 \
    } while (0)                                                           \
// ^^^ old_space_chunk_remembered_set_foreach() ^^^

/// Old space manager.
struct old_space {
    struct mem_chunk_list _chunks;
    size_t chunk_size;
};

/// Meta data of a old space chunk.
/// Must be the first block of memory allocated from the chunk.
struct old_space_chunk_meta {
    struct old_space_chunk_remembered_set *remembered_set; // Nullable.
    void *iter_visited_end; // Nullable.
};

/// Initialize chunk meta.
static void old_space_chunk_meta_init(struct old_space_chunk_meta *meta) {
    meta->remembered_set = NULL;
    meta->iter_visited_end = NULL;
}

/// Finalize chunk meta.
static void old_space_chunk_meta_fini(struct old_space_chunk_meta *meta) {
    if (meta->remembered_set)
        old_space_chunk_remembered_set_destroy(meta->remembered_set);
}

#define old_space_chunk_meta_addr(CHUNK_PTR) \
    ((struct old_space_chunk_meta *)&((CHUNK_PTR)->_mem[0]))

#define old_space_chunk_meta_of_obj(OBJ_PTR) \
    (zis_object_meta_get_gc_ptr((OBJ_PTR)->_meta, struct old_space_chunk_meta *))

#define old_space_chunk_of_meta(META_PTR) \
    ((struct mem_chunk *)((char *)(META_PTR) - offsetof(struct mem_chunk, _mem)))

#define old_space_chunk_first_obj(CHUNK_PTR) \
    ((struct zis_object *) \
        ((char *)old_space_chunk_meta_addr(CHUNK_PTR) \
            + sizeof(struct old_space_chunk_meta)))

/// Old space storage iterator. Invalidated after de-allocations in old space.
struct old_space_iterator {
    struct mem_chunk *chunk;
    void             *point; // Position in the chunk.
};

/// Make an iterator at the first allocated object.
struct old_space_iterator old_space_allocated_begin(struct old_space *space) {
    struct mem_chunk *const first_chunk = mem_chunk_list_front(&space->_chunks);
    return (struct old_space_iterator){
        .chunk = first_chunk,
        .point = old_space_chunk_first_obj(first_chunk),
    };
}

/// Make an iterator after the last allocated object.
struct old_space_iterator old_space_allocated_end(struct old_space *space) {
    struct mem_chunk *const last_chunk = mem_chunk_list_back(&space->_chunks);
    return (struct old_space_iterator){
        .chunk = last_chunk,
        .point = last_chunk->_free,
    };
}

/// Move iterator forward `size` bytes. Return the old value of `iter->point`.
/// Return `NULL` if reaches the end of last chunk.
zis_force_inline static void *old_space_iterator_forward(
    struct old_space_iterator *iter, size_t size
) {
    struct mem_chunk *chunk = iter->chunk;
    void *point = iter->point;
    char *new_point = (char *)point + size;
    if (zis_unlikely(new_point >= chunk->_end)) {
        struct mem_chunk *const next_chunk = chunk->_next;
        if (!next_chunk)
            return NULL;
        // Record the last visited position of current chunk.
        struct old_space_chunk_meta *const orig_chunk_meta =
            old_space_chunk_meta_addr(chunk);
        if ((void *)&orig_chunk_meta->iter_visited_end < (void *)chunk->_end) {
            assert(!orig_chunk_meta->iter_visited_end);
            orig_chunk_meta->iter_visited_end = point;
        } else {
            // The `_head` of `struct mem_chunk_list` is an empty chunk and has no meta.
            // Skip if `chunk` is the empty one.
            assert(chunk->_mem == chunk->_end);
        }
        // Go to next chunk.
        chunk = next_chunk;
        point = old_space_chunk_first_obj(chunk);
        new_point = (char *)point + size;
        assert(new_point < chunk->_end);
        iter->chunk = chunk;
    }
    iter->point = new_point;
    return point;
}

static struct mem_chunk *old_space_add_chunk(struct old_space *);

/// Initialize space.
static void old_space_init(struct old_space *space, const struct objmem_config *conf) {
    space->chunk_size = conf->old_spc_chunk_size;
    mem_chunk_list_init(&space->_chunks);
    old_space_add_chunk(space);
}

/// Finalize allocated objects and delete remembered sets, but do not free storage.
static void old_space_pre_fini(struct old_space *space) {
    mem_chunk_list_foreach(&space->_chunks, chunk, {
        old_space_chunk_meta_fini(old_space_chunk_meta_addr(chunk));
        /*
        mem_chunk_foreach_allocated_object(
            chunk, sizeof(struct old_space_chunk_meta), obj, obj_type, obj_size,
        {
            zis_unused_var(obj_size);
            // NOTE: object terminates here.
        });
        */
    });
}

/// Finalize space. `old_space_pre_fini()` must have been called.
static void old_space_fini(struct old_space *space) {
    mem_chunk_list_fini(&space->_chunks);
}

/// Add a chunk to the end of list.
zis_noinline static struct mem_chunk *old_space_add_chunk(struct old_space *space) {
    const size_t chunk_size = space->chunk_size;
    struct  mem_chunk *const chunk =
        mem_chunk_list_append_created(&space->_chunks, chunk_size);
    struct old_space_chunk_meta *const chunk_meta =
        mem_chunk_alloc(chunk, sizeof(struct old_space_chunk_meta));
    assert(chunk_meta);
    assert(chunk_meta == old_space_chunk_meta_addr(chunk));
    old_space_chunk_meta_init(chunk_meta);
    return chunk;
}

/// Delete chunks after the given one.
static void old_space_remove_chunks_after(
    struct old_space *space, struct mem_chunk *after_chunk
) {
    for (struct mem_chunk *chunk = after_chunk->_next; chunk; chunk = chunk->_next)
        old_space_chunk_meta_fini(old_space_chunk_meta_addr(chunk));
    mem_chunk_list_destroy_after(&space->_chunks, after_chunk);
}

#if ZIS_DEBUG

zis_unused_fn
static void old_space_print_usage(struct old_space *space, FILE *stream) {
    fputs("<OldSpc>\n", stream);
    size_t chunk_index = 0;
    mem_chunk_list_foreach(&space->_chunks, chunk, {
        const size_t chunk_mem_size = (size_t)(chunk->_end - chunk->_mem);
        const size_t chunk_free_size = (size_t)(chunk->_end - chunk->_free);
        struct old_space_chunk_remembered_set *const r_set =
            old_space_chunk_meta_addr(chunk)->remembered_set;
        fprintf(
            stream, "  <chunk id=\"%zu\" addr=\"%p\" size=\"%zu\" free_size=\"%zu\" has_r_set=\"%s\" />\n",
            chunk_index, (void *)chunk, chunk_mem_size, chunk_free_size, r_set ? "yes" : "no"
        );
        if (r_set) {
            fprintf(stream, "  <r_set id=\"%zu\" addr=\"%p\">", chunk_index, (void *)r_set);
            old_space_chunk_remembered_set_foreach(r_set, offset, {
                fprintf(stream, " %zu", offset);
            });
            fputs(" </r_set>\n", stream);
        }
        chunk_index++;
    });
    fputs("</OldSpc>\n", stream);
}

#endif // ZIS_DEBUG

/// Allocate storage for an object. On failure, returns `NULL`.
zis_force_inline static struct zis_object *
old_space_alloc(struct old_space *space, void *type_ptr, size_t size) {
    assert(size >= sizeof(struct zis_object_meta));
    struct mem_chunk *const chunk = mem_chunk_list_back(&space->_chunks);
    struct zis_object *const obj = mem_chunk_alloc(chunk, size);
    if (zis_unlikely(!obj))
        return NULL;
    void *const meta_addr = old_space_chunk_meta_addr(chunk);
    zis_object_meta_assert_ptr_fits(meta_addr);
    zis_object_meta_assert_ptr_fits(type_ptr);
    zis_object_meta_init(obj->_meta, ZIS_OBJMEM_OBJ_OLD, meta_addr, type_ptr);
    return obj;
}

/// Full GC: move iterator to reserve storage. Allocate new chunk if there is
/// no enough storage. Return the storage, which is not initialized. The space
/// state is not modified.
zis_force_inline static void *old_space_pre_alloc(
    struct old_space *space, struct old_space_iterator *alloc_pos, size_t size
) {
    void *ptr;
retry:
    ptr = old_space_iterator_forward(alloc_pos, size);
    if (zis_unlikely(!ptr)) {
        assert(alloc_pos->chunk == mem_chunk_list_back(&space->_chunks));
        old_space_add_chunk(space);
        assert(alloc_pos->chunk->_next == mem_chunk_list_back(&space->_chunks));
        goto retry;
    }
    return ptr;
}

/// Full GC: delete unused chunks and update `_free` pointer of each chunk.
/// The iterator `trunc_from` can only be modified by `old_space_pre_alloc()`
/// before calling this function.
static void old_space_truncate(
    struct old_space *space, struct old_space_iterator trunc_from
) {
    // TODO: cache unused chunks instead of deleting them.
    old_space_remove_chunks_after(space, trunc_from.chunk);

    assert(space->_chunks._tail == trunc_from.chunk);
    assert(!old_space_chunk_meta_addr(trunc_from.chunk)->iter_visited_end);
    old_space_chunk_meta_addr(trunc_from.chunk)->iter_visited_end = trunc_from.point;

    mem_chunk_list_foreach(&space->_chunks, chunk, {
        struct old_space_chunk_meta *const chunk_meta =
            old_space_chunk_meta_addr(chunk);
        void *const new_free_pos = chunk_meta->iter_visited_end;
        chunk_meta->iter_visited_end = NULL;
        assert(new_free_pos);
        assert(new_free_pos > (void *)chunk->_mem
            && new_free_pos < (void *)chunk->_end);
        chunk->_free = new_free_pos;
    });
}

/// Full GC: reallocate storages for survivors and clear remembered set.
/// Reallocated objects are neither initialized nor moved. Pointer to new storage
/// is written to the GC_PTR of object meta. Also call finalizers of dead
/// objects if there are.
static void old_space_realloc_survivors_and_forget_remembered_objects(
    struct old_space *space, struct old_space_iterator *realloc_iter
) {
    // To avoid overlapping and minimize movements, the iterator must be at the
    // beginning of available spaces.
    assert(realloc_iter->chunk == mem_chunk_list_front(&space->_chunks)
        && realloc_iter->point == old_space_chunk_first_obj(realloc_iter->chunk));

    mem_chunk_list_foreach(&space->_chunks, chunk, {
        // Delete remembered set.
        struct old_space_chunk_meta *const chunk_meta = old_space_chunk_meta_addr(chunk);
        if (chunk_meta->remembered_set) {
            old_space_chunk_remembered_set_destroy(chunk_meta->remembered_set);
            chunk_meta->remembered_set = NULL;
        }
        // Update references.
        mem_chunk_foreach_allocated_object(
            chunk, sizeof(struct old_space_chunk_meta), obj, obj_type, obj_size,
        {
            zis_unused_var(obj_type);
            if (zis_unlikely(!zis_object_meta_test_gc_mark(obj->_meta))) {
                // NOTE: object terminates here.
                continue;
            }
            void *const new_mem =
                old_space_pre_alloc(space, realloc_iter, obj_size);
            assert(new_mem);
            zis_object_meta_assert_ptr_fits(new_mem);
            zis_object_meta_set_gc_ptr(obj->_meta, new_mem);
        });
    });
}

/// Write barrier: record object in remembered set.
zis_force_inline static void old_space_add_remembered_object(
    struct old_space_chunk_meta *chunk_meta, struct zis_object *obj
) {
    assert(
        (void *)chunk_meta < (void *)obj &&
        (void *)old_space_chunk_of_meta(chunk_meta)->_end > (void *)obj);
    struct old_space_chunk_remembered_set *r_set = chunk_meta->remembered_set;
    if (zis_unlikely(!r_set)) {
        struct mem_chunk *const chunk = old_space_chunk_of_meta(chunk_meta);
        const size_t chunk_size = (size_t)(chunk->_end - (char *)chunk);
        r_set = old_space_chunk_remembered_set_create(chunk_size);
        chunk_meta->remembered_set = r_set;
    }
    old_space_chunk_remembered_set_record(
        r_set,
        (size_t)((char *)obj - (char *)chunk_meta)
    );
}

/// Fast GC: mark young slots of recorded objects in remembered set.
/// Return the number of involved chunks.
static size_t old_space_mark_remembered_objects_young_slots(struct old_space *space) {
    size_t count = 0;
    mem_chunk_list_foreach(&space->_chunks, chunk, {
        struct old_space_chunk_meta *const chunk_meta =
            old_space_chunk_meta_addr(chunk);
        struct old_space_chunk_remembered_set *const r_set =
            chunk_meta->remembered_set;
        if (zis_likely(!r_set))
            continue;
        count++;
        old_space_chunk_remembered_set_foreach(r_set, obj_offset, {
            struct zis_object *const obj =
                (struct zis_object *)((char *)chunk_meta + obj_offset);
            assert(zis_object_meta_is_not_young(obj->_meta));
            _zis_objmem_mark_object_slots_rec_o2y(obj);
        });
    });
    return count;
}

/// Fast GC: update references in a region starting from `begin`.
static void old_space_update_references_from(
    struct old_space *space,
    struct old_space_iterator begin
) {
    zis_unused_var(space);
    assert(mem_chunk_list_contains(&space->_chunks, begin.chunk));
    assert(begin.point > (void *)begin.chunk->_mem);
    struct mem_chunk *chunk = begin.chunk;
    size_t chunk_start_offset = (size_t)((char *)begin.point - begin.chunk->_mem);

    while (chunk) {
        mem_chunk_foreach_allocated_object(
            chunk, chunk_start_offset, obj, obj_type, obj_size,
        {
            (zis_unused_var(obj_type), zis_unused_var(obj_size));
            _zis_objmem_move_object_slots(obj);
        });

        chunk = chunk->_next;
        chunk_start_offset = sizeof(struct old_space_chunk_meta);
    }
}

/// GC: update references in recorded objects in remembered set.
static size_t old_space_update_remembered_objects_references_and_forget_remembered_objects(
    struct old_space *space, size_t hint_max_count
) {
    size_t count = 0;
    mem_chunk_list_foreach(&space->_chunks, chunk, {
        if (zis_unlikely(count >= hint_max_count))
            break;
        struct old_space_chunk_meta *const chunk_meta =
            old_space_chunk_meta_addr(chunk);
        struct old_space_chunk_remembered_set *const r_set =
            chunk_meta->remembered_set;
        if (zis_likely(!r_set))
            continue;
        count++;
        // Update references.
        old_space_chunk_remembered_set_foreach(r_set, obj_offset, {
            struct zis_object *const obj =
                (struct zis_object *)((char *)chunk_meta + obj_offset);
            _zis_objmem_move_object_slots(obj);
        });
        // Delete remembered set.
        old_space_chunk_remembered_set_destroy(r_set);
        chunk_meta->remembered_set = NULL;
    });
    return count;
}

/// Full GC: update references to objects. References in unmarked objects are skipped.
static void old_space_update_references(struct old_space *space) {
    mem_chunk_list_foreach(&space->_chunks, chunk, {
        mem_chunk_foreach_allocated_object(
            chunk, sizeof(struct old_space_chunk_meta), obj, obj_type, obj_size,
        {
            (zis_unused_var(obj_type), zis_unused_var(obj_size));
            if (zis_unlikely(!zis_object_meta_test_gc_mark(obj->_meta)))
                continue;
            _zis_objmem_move_object_slots(obj);
        });
    });
}

struct old_space_init_reallocated_obj_meta_context {
    struct old_space_chunk_meta *this_chunk_meta;
    struct mem_chunk            *this_chunk;
    void                        *this_chunk_end;
    struct old_space            *space;
};

/// Make a `struct old_space_init_reallocated_obj_meta_context`.
static struct old_space_init_reallocated_obj_meta_context
old_space_init_reallocated_obj_meta_context(struct old_space *space) {
    const struct old_space_iterator begin = old_space_allocated_begin(space);
    return (struct old_space_init_reallocated_obj_meta_context){
        .this_chunk_meta = old_space_chunk_meta_addr(begin.chunk),
        .this_chunk      = begin.chunk,
        .this_chunk_end  = begin.chunk->_end,
        .space           = space,
    };
}

zis_noinline static void _old_space_init_reallocated_obj_meta_slow(
    struct old_space_init_reallocated_obj_meta_context *ctx,
    struct zis_object *obj, void *obj_type
) {
    assert(!(
        (void *)obj > (void *)ctx->this_chunk &&
        (void *)obj < ctx->this_chunk_end
    ));

    for (struct mem_chunk *chunk = ctx->this_chunk->_next; chunk; chunk = chunk->_next) {
        if ((void *)obj > (void *)chunk && (void *)obj < (void *)chunk->_end) {
            assert(obj >= old_space_chunk_first_obj(chunk));

            struct old_space_chunk_meta *const chunk_meta =
                old_space_chunk_meta_addr(chunk);

            ctx->this_chunk_meta = chunk_meta;
            ctx->this_chunk = chunk;
            ctx->this_chunk_end = chunk->_end;

            zis_object_meta_assert_ptr_fits((void *)chunk_meta);
            zis_object_meta_init(obj->_meta, ZIS_OBJMEM_OBJ_OLD, chunk_meta, obj_type);
            return;
        }
    }

    abort(); // Function misused. No matching chunk found after the given one.
}

/// Initialize object meta whose storage is allocated with `old_space_pre_alloc()`.
/// The order of calling this function must be the same with that of calling
/// `old_space_pre_alloc()`.
zis_force_inline static void old_space_init_reallocated_obj_meta(
    struct old_space_init_reallocated_obj_meta_context *ctx,
    struct zis_object *obj, void *obj_type
) {
    if (zis_likely(
        (void *)obj > (void *)ctx->this_chunk &&
        (void *)obj < ctx->this_chunk_end
    )) {
        void *const ptr = ctx->this_chunk_meta;
        zis_object_meta_assert_ptr_fits(ptr);
        zis_object_meta_init(obj->_meta, ZIS_OBJMEM_OBJ_OLD, ptr, obj_type);
    } else {
        _old_space_init_reallocated_obj_meta_slow(ctx, obj, obj_type);
    }
}

/// Full GC: move objects whose storages are reallocated with function
/// `old_space_realloc_survivors()`.
static void old_space_move_reallocated_objects(
    struct old_space *space,
    struct old_space_init_reallocated_obj_meta_context *ctx
) {
    // Like what is stated in `old_space_realloc_survivors()`, the order matters.
    assert(ctx->this_chunk == mem_chunk_list_front(&space->_chunks));

    mem_chunk_list_foreach(&space->_chunks, chunk, {
        mem_chunk_foreach_allocated_object(
            chunk, sizeof(struct old_space_chunk_meta), obj, obj_type, obj_size,
        {
            if (zis_unlikely(!zis_object_meta_test_gc_mark(obj->_meta)))
                continue;

            zis_object_meta_reset_gc_mark(obj->_meta);

            struct zis_object *const new_obj =
                zis_object_meta_get_gc_ptr(obj->_meta, struct zis_object *);

            if (obj == new_obj) {
                // The storage address is not changed. There is no doubt that
                // chunk_meta is the meta of current chunk.
                void *const ptr = old_space_chunk_meta_addr(chunk);
                zis_object_meta_assert_ptr_fits(ptr);
                zis_object_meta_init(obj->_meta, ZIS_OBJMEM_OBJ_OLD, ptr, obj_type);
                continue;
            }

            old_space_init_reallocated_obj_meta(ctx, new_obj, obj_type);

            // May overlap. DO NOT use `memcpy()`.
            memmove(
                (char *)new_obj + ZIS_OBJECT_HEAD_SIZE,
                (char *)obj + ZIS_OBJECT_HEAD_SIZE,
                obj_size - ZIS_OBJECT_HEAD_SIZE
            );
        });
    });
}

#if ZIS_DEBUG

zis_unused_fn
static int old_space_post_gc_check(struct old_space *space) {
    mem_chunk_list_foreach(&space->_chunks, chunk, {
        struct old_space_chunk_meta *const chunk_meta = old_space_chunk_meta_addr(chunk);
        if (chunk_meta->remembered_set)
            return -1;
        if (chunk_meta->iter_visited_end)
            return -2;
        mem_chunk_foreach_allocated_object(
            chunk, sizeof(struct old_space_chunk_meta), obj, obj_type, obj_size,
        {
            (zis_unused_var(obj_type), zis_unused_var(obj_size));
            if (zis_object_meta_get_gc_state(obj->_meta) != ZIS_OBJMEM_OBJ_OLD)
                return -8;
            if (zis_object_meta_test_gc_mark(obj->_meta))
                return -9;
            if (old_space_chunk_meta_of_obj(obj) != chunk_meta)
                return -10;
        });
    });
    return 0;
}

#endif // ZIS_DEBUG

/* ----- New space (young generation) --------------------------------------- */

/*
 * In new space, mark-copy GC algorithm is used.
 * The GC_PTR in object meta is not used.
 */

/// New space manager.
struct new_space {
    struct mem_chunk *_working_chunk, *_free_chunk;
};

/// Initialize space.
static void new_space_init(
    struct new_space *space,
    const struct objmem_config *conf
) {
    const size_t chunk_size = conf->new_spc_chunk_size;
    space->_working_chunk = mem_chunk_create(chunk_size);
    space->_free_chunk    = mem_chunk_create(chunk_size);
}

/// Finalize allocated objects and the space.
static void new_space_fini(struct new_space *space) {
    /*
    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        zis_unused_var(obj_size);
        // NOTE: object terminates here.
    });
    */
    mem_chunk_destroy(space->_working_chunk);
    mem_chunk_destroy(space->_free_chunk);
}

#if ZIS_DEBUG

zis_unused_fn
static void new_space_print_usage(struct new_space *space, FILE *stream) {
    fputs("<NewSpc>\n", stream);
    struct mem_chunk *const chunks[2] = {space->_working_chunk, space->_free_chunk};
    for (int i = 0; i < 2; i++) {
        struct mem_chunk *const chunk = chunks[i];
        fprintf(
            stream,
            "  <chunk addr=\"%p\" is_working_chunk=\"%s\" "
            "size=\"%zu\" free_size=\"%zu\" />\n",
            (void *)chunk,
            i == 0 ? "yes" : "no",
            (size_t)(chunk->_end - chunk->_mem),
            (size_t)(chunk->_end - chunk->_free)
        );
    }
    fputs("</NewSpc>\n", stream);
}

#endif // ZIS_DEBUG

/// Allocate storage for an object. On failure, returns `NULL`.
zis_force_inline static struct zis_object *
new_space_alloc(struct new_space *space, void *type_ptr, size_t size) {
    assert(size >= sizeof(struct zis_object_meta));
    struct zis_object *const obj =
        mem_chunk_alloc(space->_working_chunk, size);
    if (zis_unlikely(!obj))
        return NULL;
    zis_object_meta_assert_ptr_fits(type_ptr);
    zis_object_meta_init(obj->_meta, ZIS_OBJMEM_OBJ_NEW, 0U, type_ptr);
    return obj;
}

/// Fast GC: reallocate and copy objects that are marked alive in new space.
/// For objects that survived only once, new storages are in the other chunk,
/// which are still in new space. But the `MID` flag in object meta is set.
/// For other (older) objects, new storages are allocated in old space.
/// If the old space fails to allocate storage, they are kept in new space,
/// and `false` will be returned at the end of function.
/// New storage address is written to the GC_PTR of object meta.
/// Dead objects are finalized.
static bool new_space_realloc_and_copy_survivors(
    struct new_space *space, struct old_space *old_space
) {
    struct mem_chunk *const to_chunk = space->_free_chunk;
    mem_chunk_forget(to_chunk);

    bool old_space_is_full = false;
    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        assert(zis_object_meta_is_young(obj->_meta));
        if (zis_likely(!zis_object_meta_test_gc_mark(obj->_meta))) {
            // NOTE: object terminates here.
            continue;
        }

        struct zis_object *new_obj;
        if (zis_object_meta_young_is_new(obj->_meta)) {
        alloc_in_new_space:
            new_obj = mem_chunk_alloc(to_chunk, obj_size);
            assert(new_obj);
            zis_object_meta_init(new_obj->_meta, ZIS_OBJMEM_OBJ_MID, 0U, obj_type);
        } else {
            if (zis_unlikely(old_space_is_full))
                goto alloc_in_new_space;
            new_obj = old_space_alloc(old_space, obj_type, obj_size);
            if (zis_unlikely(!new_obj)) {
                old_space_is_full = true;
                goto alloc_in_new_space;
            }
        }

        zis_object_meta_assert_ptr_fits(new_obj);
        zis_object_meta_set_gc_ptr(obj->_meta, new_obj);
        assert((char *)new_obj < (char *)obj || (char *)new_obj >= (char *)obj + obj_size);
        memcpy(
            (char *)new_obj + ZIS_OBJECT_HEAD_SIZE,
            (char *)obj + ZIS_OBJECT_HEAD_SIZE,
            obj_size - ZIS_OBJECT_HEAD_SIZE
        );
    });

    return !old_space_is_full;
}

/// Full GC: reallocate storages for survivors. Objects are neither initialized
/// nor moved. Pointer to new storage is written to the GC_PTR of object meta.
/// The rules are same with that in function `new_space_realloc_and_copy_survivors()`.
static void new_space_realloc_survivors(
    struct new_space *space,
    struct old_space *old_space, struct old_space_iterator *old_space_realloc_iter
) {
    struct mem_chunk *const to_chunk = space->_free_chunk;
    mem_chunk_forget(to_chunk);

    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        zis_unused_var(obj_type);
        assert(zis_object_meta_is_young(obj->_meta));
        if (zis_likely(!zis_object_meta_test_gc_mark(obj->_meta))) {
            // NOTE: object terminates here.
            continue;
        }

        void *new_mem;
        if (zis_object_meta_young_is_new(obj->_meta)) {
            new_mem = mem_chunk_alloc(to_chunk, obj_size);
            assert(new_mem);
        } else {
            new_mem = old_space_pre_alloc(
                old_space, old_space_realloc_iter, obj_size
            );
            assert(new_mem);
        }

        zis_object_meta_assert_ptr_fits(new_mem);
        zis_object_meta_set_gc_ptr(obj->_meta, new_mem);
    });
}

/// GC: swap two chunks.
static void new_space_swap_chunks(struct new_space *space) {
    struct mem_chunk *tmp = space->_free_chunk;
    space->_free_chunk    = space->_working_chunk;
    space->_working_chunk = tmp;
}

/// Fast GC: update references to the moved objects that are still in new space.
/// Only references in the `working_chunk` are updated.
/// DO NOT forget to swap chunks before calling this function!
static void new_space_update_references(struct new_space *space) {
    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        (zis_unused_var(obj_type), zis_unused_var(obj_size));
        _zis_objmem_move_object_slots(obj);
    });
}

/// Full GC: update references like `new_space_update_references()`, but only
/// references in objects that are not marked will be skipped.
static void new_space_update_marked_references(struct new_space *space) {
    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        (zis_unused_var(obj_type), zis_unused_var(obj_size));
        if (zis_likely(!zis_object_meta_test_gc_mark(obj->_meta)))
            continue;
        _zis_objmem_move_object_slots(obj);
    });
}

/// Full GC: move survived objects in `working_chunk` to new storage.
static void new_space_move_marked_objects(
    struct new_space *space,
    struct old_space_init_reallocated_obj_meta_context *ctx
) {
    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        if (zis_likely(!zis_object_meta_test_gc_mark(obj->_meta)))
            continue;

        zis_object_meta_reset_gc_mark(obj->_meta);

        struct zis_object *const new_obj =
            zis_object_meta_get_gc_ptr(obj->_meta, struct zis_object *);

        if (zis_object_meta_young_is_new(obj->_meta)) {
            zis_object_meta_init(new_obj->_meta, ZIS_OBJMEM_OBJ_MID, 0, obj_type);
        } else {
            old_space_init_reallocated_obj_meta(ctx, new_obj, obj_type);
        }

        assert((char *)new_obj < (char *)obj || (char *)new_obj >= (char *)obj + obj_size);
        memcpy(
            (char *)new_obj + ZIS_OBJECT_HEAD_SIZE,
            (char *)obj + ZIS_OBJECT_HEAD_SIZE,
            obj_size - ZIS_OBJECT_HEAD_SIZE
        );
    });
}

#if ZIS_DEBUG

zis_unused_fn
static int new_space_post_gc_check(struct new_space *space) {
    mem_chunk_foreach_allocated_object(
        space->_working_chunk, 0, obj, obj_type, obj_size,
    {
        (zis_unused_var(obj_type), zis_unused_var(obj_size));
        if (zis_object_meta_is_not_young(obj->_meta))
            return -1;
        if (zis_object_meta_test_gc_mark(obj->_meta))
            return -2;
    });
    return 0;
}

#endif // ZIS_DEBUG

/* ----- Public functions --------------------------------------------------- */

struct zis_objmem_context {
    bool   force_full_gc; ///< GC type must be full GC.
    int8_t current_gc_type;

    struct new_space new_space;
    struct old_space old_space;
    struct big_space big_space;

    struct mem_span_set gc_roots;
    struct mem_span_set weak_refs;
};

struct zis_objmem_context *zis_objmem_context_create(const struct zis_objmem_options *opts) {
    struct objmem_config conf;
    objmem_config_conv(&conf, opts);
    struct zis_objmem_context *const ctx =
        zis_mem_alloc(sizeof(struct zis_objmem_context));
    ctx->force_full_gc = false;
    ctx->current_gc_type = (int8_t)ZIS_OBJMEM_GC_NONE;
    new_space_init(&ctx->new_space, &conf);
    old_space_init(&ctx->old_space, &conf);
    big_space_init(&ctx->big_space, &conf);
    mem_span_set_init(&ctx->gc_roots);
    mem_span_set_init(&ctx->weak_refs);
    return ctx;
}

void zis_objmem_context_destroy(struct zis_objmem_context *ctx) {
    mem_span_set_fini(&ctx->weak_refs);
    mem_span_set_fini(&ctx->gc_roots);

    big_space_fini(&ctx->big_space);
    new_space_fini(&ctx->new_space);
    old_space_pre_fini(&ctx->old_space);
    old_space_fini(&ctx->old_space);
    /*
     * Type objects are allocated in old space. Free storages in old space last
     * so that the types are accessible when finalizing all objects.
     */

    zis_mem_free(ctx);
}

zis_noreturn zis_noinline static void objmem_error_oom(struct zis_context *z) {
    struct zis_objmem_context *const ctx = z->objmem_context;
    zis_unused_var(ctx);
    zis_debug_log(FATAL, "ObjMem", "objmem@%p: out of memory", (void *)ctx);
    zis_debug_log_with(
        INFO, "ObjMem", "zis_objmem_print_usage()",
        (zis_debug_log_with_func_t)zis_objmem_print_usage, ctx
    );
    zis_context_panic(z, ZIS_CONTEXT_PANIC_OOM);
}

struct zis_object *zis_objmem_alloc(
    struct zis_context *z, struct zis_type_obj *obj_type
) {
    struct zis_object *obj;
    struct zis_objmem_context *const ctx = z->objmem_context;

    const size_t obj_size = obj_type->_obj_size;
    assert(obj_size); // `obj_size == 0` => extendable

    unsigned int retry_count = 0;
    if (zis_likely(obj_size <= NON_BIG_SPACE_MAX_ALLOC_SIZE)) {
    alloc_small:
        obj = new_space_alloc(&ctx->new_space, obj_type, obj_size);
        if (zis_unlikely(!obj)) {
            if (retry_count++ > 2)
                objmem_error_oom(z);
            zis_objmem_gc(z, ZIS_OBJMEM_GC_FAST);
            goto alloc_small;
        }
    } else {
    alloc_large:
        obj = big_space_alloc(&ctx->big_space, obj_type, obj_size);
        if (zis_unlikely(!obj)) {
            if (retry_count++ > 1)
                objmem_error_oom(z);
            zis_objmem_gc(z, ZIS_OBJMEM_GC_FULL);
            goto alloc_large;
        }
    }

    assert(!zis_object_is_smallint(obj));
    assert(zis_object_type(obj) == obj_type);
    assert(zis_object_size(obj) == obj_size);
    return obj;
}

struct zis_object *zis_objmem_alloc_ex(
    struct zis_context *z, enum zis_objmem_alloc_type alloc_type,
    struct zis_type_obj *obj_type, size_t ext_slots, size_t ext_bytes
) {
    struct zis_object *obj;
    struct zis_objmem_context *const ctx = z->objmem_context;

    size_t obj_size = obj_type->_obj_size;
    const bool has_ext = obj_size == 0;
    bool has_ext_slots, has_ext_bytes;
#ifdef _MSC_VER
    // Don't know why, but MSVC always complains that
    // "Warning C4701: potentially uninitialized local variable 'has_ext_*' used"
    has_ext_slots = false, has_ext_bytes = false;
#endif // _MSC_VER
    assert(has_ext || (ext_slots == 0 && ext_bytes == 0));
    if (has_ext) {
        has_ext_slots = obj_type->_slots_num == (size_t)-1;
        has_ext_bytes = obj_type->_bytes_len == (size_t)-1;
        assert(!has_ext_slots || ext_slots >= 1);
        assert(!has_ext_bytes || ext_bytes >= 1);
        obj_size =
            ZIS_OBJECT_HEAD_SIZE + // HEAD
            (has_ext_slots ? ext_slots : obj_type->_slots_num) * sizeof(void *); // SLOTS
        if (has_ext_bytes) {
            ext_bytes = zis_round_up_to_n_pow2(sizeof(void *), ext_bytes);
            obj_size += ext_bytes; // BYTES
        } else {
            obj_size += obj_type->_bytes_len; // BYTES
        }
    }

    unsigned int retry_count = 0;
    if (zis_likely(alloc_type == ZIS_OBJMEM_ALLOC_AUTO)) {
    alloc_type_auto:
        if (zis_unlikely(obj_size > NON_BIG_SPACE_MAX_ALLOC_SIZE))
            goto alloc_type_huge;
    alloc_small:
        obj = new_space_alloc(&ctx->new_space, obj_type, obj_size);
        if (zis_unlikely(!obj)) {
            if (retry_count++ > 2)
                objmem_error_oom(z);
            zis_objmem_gc(z, ZIS_OBJMEM_GC_FAST);
            goto alloc_small;
        }
    } else if (zis_likely(alloc_type == ZIS_OBJMEM_ALLOC_SURV)) {
        if (zis_unlikely(obj_size > NON_BIG_SPACE_MAX_ALLOC_SIZE))
            goto alloc_type_huge;
    alloc_type_surv:
        obj = old_space_alloc(&ctx->old_space, obj_type, obj_size);
        if (zis_unlikely(!obj)) {
            if (retry_count++ > 1)
                objmem_error_oom(z);
            zis_objmem_gc(z, ZIS_OBJMEM_GC_FULL);
            goto alloc_type_surv;
        }
    } else if (zis_likely(alloc_type == ZIS_OBJMEM_ALLOC_HUGE)) {
    alloc_type_huge:
        obj = big_space_alloc(&ctx->big_space, obj_type, obj_size);
        if (zis_unlikely(!obj)) {
            if (retry_count++)
                ctx->big_space.threshold_size =
                    ctx->big_space.allocated_size + obj_size; // TODO: check heap limit.
            else
                zis_objmem_gc(z, ZIS_OBJMEM_GC_FULL);
            goto alloc_type_huge;
        }
    } else {
        goto alloc_type_auto;
    }
    assert(!zis_object_is_smallint(obj));
    assert(zis_object_type(obj) == obj_type);

    if (has_ext) {
        if (has_ext_slots) {
            const zis_smallint_t n = (zis_smallint_t)ext_slots;
            assert(0 < n && n <= ZIS_SMALLINT_MAX);
            zis_object_set_slot(obj, 0, zis_smallint_to_ptr(n));
        }
        if (has_ext_bytes) {
            const size_t n_slots = has_ext_slots ? ext_slots : obj_type->_slots_num;
            *(size_t *)zis_object_ref_bytes(obj, n_slots) = ext_bytes;
        }
    }
    assert(zis_object_size(obj) == obj_size);

    return obj;
}

void zis_objmem_add_gc_root(
    struct zis_context *z,
    void *root, zis_objmem_object_visitor_t fn
) {
    struct zis_objmem_context *const ctx = z->objmem_context;
    mem_span_set_add(&ctx->gc_roots, root, (void(*)(void))fn);
}

void zis_objmem_visit_object_vec(
    struct zis_object **begin, struct zis_object **end,
    enum zis_objmem_obj_visit_op op
) {
    for (struct zis_object **p = begin; p < end; p++)
        zis_objmem_visit_object(*p, op);
}

bool zis_objmem_remove_gc_root(struct zis_context *z, void *root) {
    struct zis_objmem_context *const ctx = z->objmem_context;
    return mem_span_set_remove(&ctx->gc_roots, root);
}

void zis_objmem_register_weak_ref_collection(
    struct zis_context *z,
    void *ref_container, zis_objmem_weak_refs_visitor_t fn
) {
    struct zis_objmem_context *const ctx = z->objmem_context;
    mem_span_set_add(&ctx->weak_refs, ref_container, (void(*)(void))fn);
}

bool zis_objmem_unregister_weak_ref_collection(
    struct zis_context *z, void *ref_container
) {
    struct zis_objmem_context *const ctx = z->objmem_context;
    return mem_span_set_remove(&ctx->weak_refs, ref_container);
}

/// Fast (young) GC implementation.
static void gc_fast(struct zis_objmem_context *ctx) {
    // ## 1  Mark reachable young objects.

    // ### 1.1  Mark young objects in GC roots.

    mem_span_set_foreach(
        &ctx->gc_roots,
        void *, gc_root,
        zis_objmem_object_visitor_t, visitor,
    {
        visitor(gc_root, ZIS_OBJMEM_OBJ_VISIT_MARK_Y);
    });

    // ### 1.2  Scan remembered sets and mark referred young objects.

    const size_t old_spc_cnt_hint =
        old_space_mark_remembered_objects_young_slots(&ctx->old_space);

    // ### 1.3  Scan big space and mark referred young objects.

    const size_t big_spc_cnt_hint =
        big_space_mark_remembered_objects_young_slots(&ctx->big_space);

    // ## 2  Clean up unused weak references.

    mem_span_set_foreach(
        &ctx->weak_refs,
        void *, weak_ref,
        zis_objmem_weak_refs_visitor_t, visitor,
    {
        visitor(weak_ref, ZIS_OBJMEM_WEAK_REF_VISIT_FINI_Y);
    });

    // ## 3  Re-allocate storage for survived objects, then copy them to new places.

    const struct old_space_iterator old_spc_orig_end =
        old_space_allocated_end(&ctx->old_space);

    if (!new_space_realloc_and_copy_survivors(&ctx->new_space, &ctx->old_space))
        ctx->force_full_gc = true; // Run full GC next time.

    /* `_zis_objmem_mark_object_slots_rec_o2y()` is used when marking
     * remembered young objects in old space and big space. These marked young
     * objects referred by old ones shall be moved to old space.
     *
     *  Dead objects are finalized.
     */

    // ## 4  Update references.

    // ### 4.1  Update references in newly allocated objects in new space.

    new_space_swap_chunks(&ctx->new_space);
    new_space_update_references(&ctx->new_space);

    // ### 4.2  Update references in newly allocated objects in old space.

    old_space_update_references_from(&ctx->old_space, old_spc_orig_end);

    // ### 4.3  Update references in remembered old objects.

    old_space_update_remembered_objects_references_and_forget_remembered_objects(&ctx->old_space, old_spc_cnt_hint);

    // ### 4.4  Update references in remembered large objects.

    big_space_update_remembered_objects_references_and_forget_remembered_objects(&ctx->big_space, big_spc_cnt_hint);

    // ### 4.5  Update references in GC roots.

    mem_span_set_foreach(
        &ctx->gc_roots,
        void *, gc_root,
        zis_objmem_object_visitor_t, visitor,
    {
        visitor(gc_root, ZIS_OBJMEM_OBJ_VISIT_MOVE);
    });

    //### 4.6  Update references in weak references.

    mem_span_set_foreach(
        &ctx->weak_refs,
        void *, weak_ref,
        zis_objmem_weak_refs_visitor_t, visitor,
    {
        visitor(weak_ref, ZIS_OBJMEM_WEAK_REF_VISIT_MOVE);
    });
}

/// Full (young + old) GC implementation.
static void gc_full(struct zis_objmem_context *ctx) {
    // ## 1  Mark reachable objects in GC roots.

    mem_span_set_foreach(
        &ctx->gc_roots,
        void *, gc_root,
        zis_objmem_object_visitor_t, visitor,
    {
        visitor(gc_root, ZIS_OBJMEM_OBJ_VISIT_MARK);
    });

    // ## 2  Clean up unused weak references.

    mem_span_set_foreach(
        &ctx->weak_refs,
        void *, weak_ref,
        zis_objmem_weak_refs_visitor_t, visitor,
    {
        visitor(weak_ref, ZIS_OBJMEM_WEAK_REF_VISIT_FINI);
    });

    // ## 3  Re-allocate storage for survived objects. Remove dead ones.

    // ### 3.1  Finalize and delete unreachable objects in big space. No re-allocation.

    big_space_delete_unreachable_objects_and_reset_reachable_objects(&ctx->big_space);

    // ### 3.2  Re-allocations in old space. Finalize dead ones.

    struct old_space_iterator old_spc_realloc_iter = old_space_allocated_begin(&ctx->old_space);
    old_space_realloc_survivors_and_forget_remembered_objects(&ctx->old_space, &old_spc_realloc_iter);

    // ### 3.3  Re-allocations in new space. Finalize dead ones.

    new_space_realloc_survivors(&ctx->new_space, &ctx->old_space, &old_spc_realloc_iter);

    // ## 4  Update references.

    // ### 4.1  Update references in new space.

    new_space_update_marked_references(&ctx->new_space);

    // ### 4.2  Update references in old space.

    old_space_update_references(&ctx->old_space);

    // ### 4.3  Update references in big space.

    big_space_update_references(&ctx->big_space);

    // ### 4.5  Update references in GC roots.

    mem_span_set_foreach(
        &ctx->gc_roots,
        void *, gc_root,
        zis_objmem_object_visitor_t, visitor,
    {
        visitor(gc_root, ZIS_OBJMEM_OBJ_VISIT_MOVE);
    });

    //### 4.6  Update references in weak references.

    mem_span_set_foreach(
        &ctx->weak_refs,
        void *, weak_ref,
        zis_objmem_weak_refs_visitor_t, visitor,
    {
        visitor(weak_ref, ZIS_OBJMEM_WEAK_REF_VISIT_MOVE);
    });

    // ### 5  Move objects to new storage.

    // ### 5.1  Move objects in old space.

    struct old_space_init_reallocated_obj_meta_context old_spc_init_obj_ctx =
        old_space_init_reallocated_obj_meta_context(&ctx->old_space);
    old_space_move_reallocated_objects(&ctx->old_space, &old_spc_init_obj_ctx);

    // ### 5.2  Move objects in new space.

    new_space_move_marked_objects(&ctx->new_space, &old_spc_init_obj_ctx);
    new_space_swap_chunks(&ctx->new_space);

    // ### 5.3  Clean up unused old space chunks.

    old_space_truncate(&ctx->old_space, old_spc_realloc_iter);

    // TODO: adjust big space threshold.
}

int zis_objmem_gc(struct zis_context *z, enum zis_objmem_gc_type type) {
    struct zis_objmem_context *const ctx = z->objmem_context;

    if (zis_unlikely(ctx->force_full_gc)) {
        ctx->force_full_gc = false;
        type = ZIS_OBJMEM_GC_FULL;
    } else if (type == ZIS_OBJMEM_GC_AUTO) {
        type = ZIS_OBJMEM_GC_FAST;
    }
    ctx->current_gc_type = (int8_t)type;

#if ZIS_DEBUG
    zis_debug_log(
        INFO, "ObjMem", "%s GC starts",
        type == ZIS_OBJMEM_GC_FAST ? "fast" : "full"
    );
    struct timespec tp0;
    zis_debug_time(&tp0);
#endif // ZIS_DEBUG

    if (type == ZIS_OBJMEM_GC_FAST)
        gc_fast(ctx);
    else if (type == ZIS_OBJMEM_GC_FULL)
        gc_full(ctx);
    else
        type = ZIS_OBJMEM_GC_NONE; // Illegal type.

#if ZIS_DEBUG
    struct timespec tp1;
    zis_debug_time(&tp1);
    double dt_ms =
        (double)(tp1.tv_sec - tp0.tv_sec) * 1e3 +
        (double)(tp1.tv_nsec - tp0.tv_nsec) / 1e6;
    zis_unused_var(dt_ms);

    zis_debug_log(INFO, "ObjMem", "GC ends, %.2lf ms", dt_ms);
    zis_debug_log_with(
        TRACE, "ObjMem", "zis_objmem_print_usage()",
        (zis_debug_log_with_func_t)zis_objmem_print_usage, ctx
    );

    assert(!new_space_post_gc_check(&ctx->new_space));
    assert(!old_space_post_gc_check(&ctx->old_space));
    assert(!big_space_post_gc_check(&ctx->big_space));
#endif // ZIS_DEBUG

    ctx->current_gc_type = (int8_t)ZIS_OBJMEM_GC_NONE;

    return (int)type;
}

enum zis_objmem_gc_type zis_objmem_current_gc(struct zis_context *z) {
    struct zis_objmem_context *const ctx = z->objmem_context;
    return (enum zis_objmem_gc_type)(int)ctx->current_gc_type;
}

zis_noinline void zis_objmem_record_o2y_ref(struct zis_object *obj) {
    assert(zis_object_meta_is_not_young(obj->_meta));
    if (zis_likely(zis_object_meta_old_is_not_big(obj->_meta))) // ZIS_OBJMEM_OBJ_OLD
        old_space_add_remembered_object(old_space_chunk_meta_of_obj(obj), obj);
    else // ZIS_OBJMEM_OBJ_BIG
        big_space_remember_object(obj);
}

void zis_objmem_print_usage(struct zis_objmem_context *ctx, void *FILE_ptr) {
#if ZIS_DEBUG

    FILE *stream = FILE_ptr ? FILE_ptr : stderr;

    fprintf(
        stream, "<ObjMem context=\"%p\" force_full_gc=\"%s\">\n",
        (void *)ctx, ctx->force_full_gc ? "yes" : "no"
    );
    new_space_print_usage(&ctx->new_space, stream);
    old_space_print_usage(&ctx->old_space, stream);
    big_space_print_usage(&ctx->big_space, stream);
    fputs("</ObjMem>\n", stream);

#else // !ZIS_DEBUG

    zis_unused_var(ctx);
    zis_unused_var(FILE_ptr);

    // Not available!

#endif
}

#define MARK_OBJ_IMPL__RET_IF_MARKED(obj) \
    if (zis_object_meta_test_gc_mark(obj->_meta)) \
        return;

#define MARK_OBJ_IMPL__RET_IF_OLD_OR_MARKED(obj) \
    if (zis_object_meta_is_not_young(obj->_meta) || zis_object_meta_test_gc_mark(obj->_meta)) \
    return;

#define MARK_OBJ_IMPL__MARK_SELF(obj) \
    zis_object_meta_set_gc_mark(obj->_meta);

zis_static_force_inline void _zis_objmem_mark_object_rec_x(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));

    MARK_OBJ_IMPL__RET_IF_MARKED(obj)
    MARK_OBJ_IMPL__MARK_SELF(obj)

    if (zis_object_meta_get_gc_state(obj->_meta) == ZIS_OBJMEM_OBJ_NEW)
        _zis_objmem_mark_object_slots_rec_x(obj);
    else // MID objects will become OLD after GC.
        _zis_objmem_mark_object_slots_rec_o2x(obj);
}

zis_static_force_inline void _zis_objmem_mark_object_rec_y(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));

    MARK_OBJ_IMPL__RET_IF_OLD_OR_MARKED(obj)
    MARK_OBJ_IMPL__MARK_SELF(obj)

    if (zis_object_meta_young_is_new(obj->_meta))
        _zis_objmem_mark_object_slots_rec_y(obj);
    else
        _zis_objmem_mark_object_slots_rec_o2y(obj);
}

zis_static_force_inline void _zis_objmem_mark_object_rec_o2x(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));

    if (zis_object_meta_get_gc_state(obj->_meta) == ZIS_OBJMEM_OBJ_NEW) // Make NEW object MID, and it will become OLD after GC.
        zis_object_meta_set_gc_state(obj->_meta, ZIS_OBJMEM_OBJ_MID); // TODO: meta_word &= 1

    MARK_OBJ_IMPL__RET_IF_MARKED(obj)
    MARK_OBJ_IMPL__MARK_SELF(obj)

    _zis_objmem_mark_object_slots_rec_o2x(obj);
}

zis_static_force_inline void _zis_objmem_mark_object_rec_o2y(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));

    if (zis_object_meta_is_not_young(obj->_meta))
        return;

    if (zis_object_meta_young_is_new(obj->_meta))
        zis_object_meta_set_gc_state(obj->_meta, ZIS_OBJMEM_OBJ_MID); // TODO: meta_word &= 1

    MARK_OBJ_IMPL__RET_IF_MARKED(obj) // MARK_OBJ_IMPL__RET_IF_OLD_OR_MARKED(obj)
    MARK_OBJ_IMPL__MARK_SELF(obj)

    _zis_objmem_mark_object_slots_rec_o2y(obj);
}

#undef MARK_OBJ_IMPL__RET_IF_MARKED
#undef MARK_OBJ_IMPL__RET_IF_OLD_OR_MARKED
#undef MARK_OBJ_IMPL__MARK_SELF

#define MARK_OBJ_SLOT_IMPL__MARK_TYPE_OBJ(obj_type, MARK_FN_SUFFIX) \
    _zis_objmem_mark_object_rec_##MARK_FN_SUFFIX(zis_object_from(obj_type));

#define MARK_OBJ_SLOT_IMPL__ASSERT_TYPE_OLD(obj_type) \
    assert(zis_object_meta_is_not_young(obj_type->_meta));

#define MARK_OBJ_SLOT_IMPL__MARK_SLOTS(obj, obj_type, MARK_FN_SUFFIX) \
{                                                                     \
    size_t slot_i = 0, slot_n = obj_type->_slots_num;                 \
    if (zis_unlikely(slot_n == (size_t)-1)) { /* See `zis_object_slot_count()`. */ \
        struct zis_object *const vn = zis_object_get_slot(obj, 0);    \
        assert(zis_object_is_smallint(vn));                           \
        slot_i = 1, slot_n = (size_t)zis_smallint_from_ptr(vn);       \
    }                                                                 \
    for (; slot_i < slot_n; slot_i++) {                               \
        struct zis_object *const slot_obj = zis_object_get_slot(obj, slot_i);      \
        if (zis_likely(!zis_object_is_smallint(slot_obj)))            \
            _zis_objmem_mark_object_rec_##MARK_FN_SUFFIX(slot_obj);   \
    }                                                                 \
}

/// Set GC mark of slots of an object recursively.
zis_noinline void _zis_objmem_mark_object_slots_rec_x(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    MARK_OBJ_SLOT_IMPL__MARK_TYPE_OBJ(obj_type, x)
    MARK_OBJ_SLOT_IMPL__MARK_SLOTS(obj, obj_type, x)
}

/// Set GC mark of young slots of an object recursively.
zis_noinline void _zis_objmem_mark_object_slots_rec_y(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    MARK_OBJ_SLOT_IMPL__ASSERT_TYPE_OLD(obj_type)
    MARK_OBJ_SLOT_IMPL__MARK_SLOTS(obj, obj_type, y)
}

/// Set GC mark of slots of an old-object-referred object recursively.
zis_noinline void _zis_objmem_mark_object_slots_rec_o2x(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    MARK_OBJ_SLOT_IMPL__MARK_TYPE_OBJ(obj_type, o2x)
    MARK_OBJ_SLOT_IMPL__MARK_SLOTS(obj, obj_type, o2x)
}

/// Set GC mark of young slots of an old-object-referred object recursively.
zis_noinline void _zis_objmem_mark_object_slots_rec_o2y(struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    MARK_OBJ_SLOT_IMPL__ASSERT_TYPE_OLD(obj_type)
    MARK_OBJ_SLOT_IMPL__MARK_SLOTS(obj, obj_type, o2y)
}

#undef MARK_OBJ_SLOT_IMPL__MARK_TYPE_OBJ
#undef MARK_OBJ_SLOT_IMPL__ASSERT_TYPE_OLD
#undef MARK_OBJ_SLOT_IMPL__MARK_SLOTS

/// Update the reference to a moved object.
zis_static_force_inline bool _zis_objmem_move_object(struct zis_object **obj_ref) {
    struct zis_object *obj = *obj_ref;
    assert(!zis_object_is_smallint(obj));

    if (!zis_object_meta_test_gc_mark(obj->_meta))
        return false;

    // Pointer to the new storage shall have been stored in GC_PTR in object meta.
    *obj_ref = zis_object_meta_get_gc_ptr(obj->_meta, struct zis_object *);

    // This operation is not recursive, so `_zis_objmem_move_object_slots()`
    // is not going to be called.

    return true;
}

/// Update the references to moved slots of an object.
zis_noinline void _zis_objmem_move_object_slots(struct zis_object *obj) {
    struct zis_type_obj *obj_type = zis_object_type(obj);
    size_t slot_n = obj_type->_slots_num; // Get size before type ptr updated.

    if (zis_unlikely(_zis_objmem_move_object((struct zis_object **)&obj_type)))
        zis_object_meta_set_type_ptr(obj->_meta, obj_type);

    size_t slot_i = 0;
    if (zis_unlikely(slot_n == (size_t)-1)) { /* See `zis_object_slot_count()`. */
        struct zis_object *const vn = zis_object_get_slot(obj, 0);
        assert(zis_object_is_smallint(vn));
        slot_i = 1, slot_n = (size_t)zis_smallint_from_ptr(vn);
    }
    for (; slot_i < slot_n; slot_i++) {
        struct zis_object *const slot_obj = zis_object_get_slot(obj, slot_i);
        if (zis_likely(!zis_object_is_smallint(slot_obj)))
            _zis_objmem_move_object((struct zis_object **)(obj->_body) + slot_i);
    }
}

void _zis_object_write_barrier_n(
    struct zis_object *obj, struct zis_object *val_arr[], size_t var_arr_len
) {
    assert(zis_object_meta_is_not_young(obj->_meta));
    for (size_t i = 0; i < var_arr_len; i++) {
        struct zis_object *const val = val_arr[i];
        if (!zis_object_is_smallint(val) && zis_object_meta_is_young(val->_meta)) {
            zis_objmem_record_o2y_ref(obj);
            return;
        }
    }
}
