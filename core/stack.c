#include "stack.h"

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"

/* ----- configuration ------------------------------------------------------ */

#define ZIS_CALLSTACK_SIZE_MIN  (sizeof(struct zis_callstack) + sizeof(void *) * 2)
#define ZIS_CALLSTACK_SIZE_DFL  (sizeof(void *) * 1020)
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
    struct zis_object **const sp_p1 = cs->top + 1;
    assert(sp_p1 <= cs->_data_end);
    zis_objmem_visit_object_vec(bp, sp_p1, op);
}

/// Fill slots with known objects.
zis_static_force_inline struct zis_object **
callstack_clear_range(struct zis_object **begin, size_t count) {
    return zis_object_vec_zero(begin, count);
}

/* ----- public functions --------------------------------------------------- */

zis_noinline zis_noreturn static void
callstack_error_overflow(struct zis_callstack *cs) {
    zis_debug_log(FATAL, "Stack", "stack@%p overflow", (void *)cs);
    zis_context_panic(cs->z, ZIS_CONTEXT_PANIC_SOV);
}

struct zis_callstack *zis_callstack_create(struct zis_context *z, size_t cs_size) {
    if (cs_size == 0)
        cs_size = ZIS_CALLSTACK_SIZE_DFL;
    else if (cs_size < ZIS_CALLSTACK_SIZE_MIN)
        cs_size = ZIS_CALLSTACK_SIZE_MIN;
    assert(cs_size > sizeof(struct zis_callstack));
    struct zis_callstack *const cs = zis_mem_alloc(cs_size);
    cs->top = cs->_data;
    cs->frame = cs->_data;
    fi_list_init(&cs->_fi_list);
    cs->z = z;
    cs->_data_end = cs->_data + (cs_size - sizeof(struct zis_callstack)) / sizeof(void *);
    cs->frame[0] = zis_smallint_to_ptr(0);
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
    assert(cs->z == z);
    zis_mem_free(cs);
}

void zis_callstack_enter(struct zis_callstack *cs, size_t frame_size, void *return_ip) {
    struct zis_object **const old_sp = cs->top, **const old_fp = cs->frame;
    struct zis_object **const new_sp = old_sp + frame_size, **const new_fp = old_sp + 1;
    if (zis_unlikely((size_t)(cs->_data_end - old_sp) < frame_size))
        callstack_error_overflow(cs);
    struct zis_callstack_frame_info *const fi = fi_list_push(&cs->_fi_list);
    fi->frame_top = new_sp;
    fi->prev_frame = old_fp;
    fi->return_ip = return_ip;
    cs->top = new_sp, cs->frame = new_fp;
    callstack_clear_range(new_fp, frame_size);
    zis_debug_log(TRACE, "Stack", "enter frame @%ti~+%zu", new_fp - cs->_data, frame_size);
}

void zis_callstack_leave(struct zis_callstack *cs) {
    const struct zis_callstack_frame_info *const fi = zis_callstack_frame_info(cs);
    struct zis_object **const old_fp = cs->frame;
    struct zis_object **const new_sp = old_fp - 1, **const new_fp = fi->prev_frame;
    assert(cs->top >= fi->frame_top);
    fi_list_pop(&cs->_fi_list); // Drop `fi`.
    cs->top = new_sp, cs->frame = new_fp;
    zis_debug_log(TRACE, "Stack", "leave frame @%ti", old_fp - cs->_data);
}

struct zis_object **zis_callstack_frame_alloc_temp(struct zis_context *z, size_t n) {
    struct zis_callstack *const cs = z->callstack;
    struct zis_object **const old_sp = cs->top;
    if (zis_unlikely((size_t)(cs->_data_end - old_sp) < n))
        callstack_error_overflow(cs);
    cs->top = old_sp + n;
    return callstack_clear_range(old_sp + 1, n);
}

void zis_callstack_frame_free_temp(struct zis_context *z, size_t n) {
    struct zis_callstack *const cs = z->callstack;
    struct zis_object **const old_sp = cs->top;
    if (zis_unlikely((size_t)(old_sp - zis_callstack_frame_info(cs)->frame_top) < n)) {
        zis_debug_log(FATAL, "Stack", "free_temp(%zu)", n);
        zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT);
    }
    cs->top = old_sp - n;
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
