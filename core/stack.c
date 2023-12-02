#include "stack.h"

#include <string.h>

#include "attributes.h"
#include "debug.h"
#include "memory.h"
#include "objmem.h"

/* ----- configuration ------------------------------------------------------ */

#define ZIS_CALLSTACK_SIZE  (sizeof(void *) * 1020)
#define ZIS_CALLSTACK_FI_POOL_SIZE  20

/* ----- frame info list operations ----------------------------------------- */

/// Initialize frame info list.
static void fi_list_init(struct _zis_callstack_fi_list *fi_list) {
    fi_list->_list = NULL;
    fi_list->_free_list = NULL;
    fi_list->_free_count = 0;
}

/// Finalize frame info list.
static void fi_list_fini(struct _zis_callstack_fi_list *fi_list) {
    struct zis_callstack_frame_info *lists[2] = { fi_list->_list, fi_list->_free_list };
    for (size_t i = 0; i < 2; i++) {
        struct zis_callstack_frame_info *p = lists[i];
        while (p) {
            struct zis_callstack_frame_info *next = p->_next_node;
            zis_mem_free(p);
            p = next;
        }
    }
}

/// Add a new frame info.
zis_static_force_inline struct zis_callstack_frame_info *
fi_list_push(struct _zis_callstack_fi_list *fi_list) {
    struct zis_callstack_frame_info *fi;
    if (fi_list->_free_list) {
        assert(fi_list->_free_count);
        fi = fi_list->_free_list;
        fi_list->_free_list = fi->_next_node;
        fi_list->_free_count--;
    } else {
        assert(!fi_list->_free_count);
        fi = zis_mem_alloc(sizeof(struct zis_callstack_frame_info));
    }
    fi->_next_node = fi_list->_list;
    fi_list->_list = fi;
    return fi;
}

/// Drop last frame info.
zis_static_force_inline void fi_list_pop(struct _zis_callstack_fi_list *fi_list) {
    struct zis_callstack_frame_info *const fi = fi_list->_list;
    assert(fi);
    fi_list->_list = fi->_next_node;
    if (zis_likely(fi_list->_free_count < ZIS_CALLSTACK_FI_POOL_SIZE)) {
        fi->_next_node = fi_list->_free_list;
        fi_list->_free_list = fi;
        fi_list->_free_count++;
    } else {
        zis_mem_free(fi);
    }
}

/* ----- GC adaptation ------------------------------------------------------ */

/// GC objects visitor. See `zis_objmem_object_visitor_t`.
static void callstack_gc_visitor(void *_cs, enum zis_objmem_obj_visit_op op) {
    struct zis_callstack *const cs = _cs;
    struct zis_object **const bp = cs->_data;
    struct zis_object **const sp = cs->top;
    assert(sp < cs->_data_end);
    for (struct zis_object **p = bp; p <= sp; p++)
        zis_objmem_visit_object(*p, op);
}

/// Fill slots with known objects.
zis_static_force_inline void
callstack_clear_range(struct zis_object **first, struct zis_object **last) {
    assert(first <= last);
#if 0
    struct zis_object *const v = zis_smallint_to_ptr(0); // Fill with small integer `0`.
    for (struct zis_object **p = first; p <= last; p++)
        *p = v;
#else
    memset(first, 0xff, (size_t)(last - first));
#endif
}

/* ----- public functions --------------------------------------------------- */

struct zis_callstack *zis_callstack_create(struct zis_context *z) {
    static_assert(ZIS_CALLSTACK_SIZE > sizeof(struct zis_callstack), "");
    const size_t cs_size = ZIS_CALLSTACK_SIZE;
    struct zis_callstack *const cs = zis_mem_alloc(cs_size);
    cs->top = cs->_data;
    cs->frame = cs->_data;
    fi_list_init(&cs->_fi_list);
    cs->_data_end = cs->_data;
    zis_objmem_add_gc_root(z, cs, callstack_gc_visitor);
    zis_debug_log(
        INFO, "Stack", "new stack @%p: size=%zu,n_slots=%zu",
        (void *)cs, cs_size, (cs_size - sizeof(struct zis_callstack)) / sizeof(void *)
    );
    return cs;
}

void zis_callstack_destroy(struct zis_callstack *cs, struct zis_context *z) {
    zis_debug_log(INFO, "Stack", "deleting stack @%p", (void *)cs);
    const bool ok = zis_objmem_remove_gc_root(z, cs);
    assert(ok); zis_unused_var(ok);
    fi_list_fini(&cs->_fi_list);
    zis_mem_free(cs);
}

bool zis_callstack_enter(struct zis_callstack *cs, size_t frame_size, void *return_ip) {
    struct zis_object **const old_sp = cs->top, **const old_fp = cs->frame;
    struct zis_object **const new_sp = old_sp + frame_size, **const new_fp = old_sp + 1;
    if (zis_unlikely(new_sp >= cs->_data_end)) {
        zis_debug_log(
            ERROR, "Stack", "stack@%p overflow: %ti+%zu>%ti",
            (void *)cs, old_sp - cs->_data, frame_size, cs->_data_end - cs->_data - 1
        );
        return false;
    }
    struct zis_callstack_frame_info *const fi = fi_list_push(&cs->_fi_list);
    fi->prev_frame = old_fp;
    fi->return_ip = return_ip;
    cs->top = new_sp, cs->frame = new_fp;
    callstack_clear_range(new_fp, new_sp);
    zis_debug_log(TRACE, "Stack", "enter frame @%ti~+%zu", new_fp - cs->_data, frame_size);
    return true;
}

void zis_callstack_leave(struct zis_callstack *cs) {
    const struct zis_callstack_frame_info *const fi = zis_callstack_frame_info(cs);
    struct zis_object **const old_fp = cs->frame;
    struct zis_object **const new_sp = old_fp - 1, **const new_fp = fi->prev_frame;
    fi_list_pop(&cs->_fi_list); // Drop `fi`.
    cs->top = new_sp, cs->frame = new_fp;
    zis_debug_log(TRACE, "Stack", "leave frame @%ti", old_fp - cs->_data);
}

int zis_callstack_foreach_frame(
    const struct zis_callstack *cs,
    zis_callstack_foreach_frame_fn_t fn, void *fn_arg
) {
    struct zis_callstack_foreach_frame_fn_arg x;
    x.frame_index = 0;
    x.frame_info = zis_callstack_frame_info(cs);
    x.frame_base = cs->frame;
    x.frame_top = cs->top;
    x.func_arg = fn_arg;
    while (x.frame_info) {
        assert(x.frame_base >= cs->_data);
        const int fn_ret = fn(&x);
        if (fn_ret)
            return fn_ret;
        x.frame_top = x.frame_base - 1;
        x.frame_base = x.frame_info->prev_frame;
        x.frame_info = x.frame_info->_next_node;
        x.frame_index++;
    }
    return 0;
}