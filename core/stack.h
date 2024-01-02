/// Call stack.

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "attributes.h"

struct zis_context;
struct zis_object;

/// Info of a call stack frame.
struct zis_callstack_frame_info {
    struct zis_object **frame_top; // excluding temp registers
    struct zis_object **prev_frame;
    void               *return_ip;
    struct zis_callstack_frame_info *_next_node;
};

struct _zis_callstack_fi_list {
    struct zis_callstack_frame_info *_list;
    struct zis_callstack_frame_info *_free_list;
    size_t _free_count;
};

/// Runtime call stack.
/// This is a GC root. Assigning to stack slots (registers) needs no write barrier.
struct zis_callstack {

    /**
     * @struct zis_callstack
     * ## Call Stack Layout
     *
     * ```
     * +----------+ <-- _data_end
     * | ******** |
     * | (unused) |
     * | ******** |
     * |----------|             -----
     * |          | <-- top       ^
     * | FRAME-N  |         current frame
     * |          | <-- frame     v
     * |----------|             -----
     * |          |               ^
     *      ...             previous frames
     * |          | <-- _data     v
     * +----------+             -----
     * ```
     *
     * ## Frame Layout
     *
     * ```
     * +-------+  - Top
     * | REG-N |
     * |  ...  |
     * | REG-1 |
     * | REG-0 |
     * +-------+  - Base
     * ```
     */

    struct zis_object **top;       ///< Top of the stack (SP).
    struct zis_object **frame;     ///< Base of top frame (FP).
    struct zis_object **_data_end; ///< End of `_data[]` (max of SP+1).
    struct _zis_callstack_fi_list _fi_list;
    struct zis_context *z; // just for panic
    struct zis_object  *_data[];   ///< Base of the stack (BP).
};

/// Crate a call stack.
zis_nodiscard struct zis_callstack *zis_callstack_create(struct zis_context *z);

/// Destroy a call stack.
void zis_callstack_destroy(struct zis_callstack *cs, struct zis_context *z);

/// Push a new frame.
void zis_callstack_enter(struct zis_callstack *cs, size_t frame_size, void *return_ip);

/// Pop the current frame.
void zis_callstack_leave(struct zis_callstack *cs);

/// Allocate temporary storage in current frame.
struct zis_object **zis_callstack_frame_alloc_temp(struct zis_context *z, size_t n);

/// Free temporary storage allocated with `zis_callstack_frame_alloc_temp()`.
void zis_callstack_frame_free_temp(struct zis_context *z, size_t n);

/// Check if no frame has been created.
zis_static_force_inline bool zis_callstack_empty(const struct zis_callstack *cs) {
    return cs->_fi_list._list == NULL;
}

/// Get frame info of the current frame.
zis_static_force_inline const struct zis_callstack_frame_info *
zis_callstack_frame_info(const struct zis_callstack *cs) {
    const struct zis_callstack_frame_info *const fi = cs->_fi_list._list;
    assert(fi);
    return fi;
}

/// Get the number of registers in the current frame.
zis_static_force_inline size_t
zis_callstack_frame_size(const struct zis_callstack *cs) {
    struct zis_object **const fp = cs->frame, **const sp = cs->top;
    assert(sp >= fp);
    return (size_t)(sp + 1 - fp);
}

struct zis_callstack_foreach_frame_fn_arg {
    size_t frame_index;
    const struct zis_callstack_frame_info *frame_info;
    struct zis_object **frame_base, **frame_top;
    void *func_arg;
};

typedef int (*zis_callstack_foreach_frame_fn_t)
    (struct zis_callstack_foreach_frame_fn_arg *);

/// Iterate over frames in the stack from last (most recent) to first.
int zis_callstack_foreach_frame(
    const struct zis_callstack *cs,
    zis_callstack_foreach_frame_fn_t fn, void *fn_arg
);
