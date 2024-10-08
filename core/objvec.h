/// Object pointer vector (array) utilities.

#pragma once

#include <string.h>

#include "attributes.h"

struct zis_object;

/* ----- vector operations -------------------------------------------------- */

/// Copy a vector of object pointers like `memcpy()`.
zis_static_force_inline void zis_object_vec_copy(
    struct zis_object **restrict dst,
    struct zis_object *const *restrict src, size_t n
) {
    memcpy(dst, src, n * sizeof(struct zis_object *));
}

/// Copy a vector of object pointers like `memmove()`.
zis_static_force_inline void zis_object_vec_move(
    struct zis_object **restrict dst,
    struct zis_object *const *restrict src, size_t n
) {
    memmove(dst, src, n * sizeof(struct zis_object *));
}

/// Fill a vector of object pointers with small integers like `memset()`.
zis_static_force_inline void zis_object_vec_zero(
    struct zis_object **restrict vec, size_t n
) {
    memset(vec, 0xff, n * sizeof(struct zis_object *));
}

/// Fill a vector of object pointers with a specific object.
zis_static_force_inline void zis_object_vec_fill(
    struct zis_object **restrict vec, struct zis_object *val, size_t n
) {
    for (size_t i = 0; i < n; i++)
        vec[i] = val;
}

/* ----- vector view -------------------------------------------------------- */

/// GC-safe reference to a vector (array) of object pointers.
struct zis_object_vec_view {
    /**
     * @struct zis_object_vec_view
     * ## Structure
     * ```
     *    _container_ptr_ref
     *          `.
     *    --------`--------
     *     ... | PTR | ...  <== a GC-root that holds the pointer to the container
     *    --------.--------     PTR = (*_container_ptr_ref)
     *  ,........`
     * :
     * V
     * +-----------------------------------------+
     * | xxxxxxxxxxx | #0 | #1 | .. | #N | xxxxx | <== the container of the vector
     * +-----------------------------------------+
     * |<  _offset  >|< length * ptr_sz >|
     * ^              \_________________/ <== the vector
     *  \
     *   (**_container_ptr_ref)
     * ```
     */

    /**
     * @struct zis_object_vec_view
     * ## Initialization
     *
     * Examples:
     *
     * ```c
     * struct zis_object_vec_view vv1, vv2;
     * struct zis_object **frame = ...;
     * zis_object_vec_view_init(vv1, &frame, 2 * sizeof frame[0], 2); // { frame[2], frame[3] }
     * struct zis_tuple_obj *tuple = ...;
     * frame[1] = zis_object_from(tuple); // Keep the object in a GC-root.
     * zis_object_vec_view_init(vv2, frame + 1, offsetof(struct zis_tuple_obj, _data) + 1 * sizeof(void *), 2); // { tuple[1], tuple[2] }
     * ```
     *
     * @see zis_object_vec_view_init()
     * @see zis_object_vec_view_from_frame()
     * @see zis_object_vec_view_from_fields()
     */

    /// A Reference to the pointer to the container of the vector.
    /// The pointer (`*_container_ptr_ref`) must be always valid, event after
    /// a garbage collection.
    /// When the container (`**_container_ptr_ref`) is an object, it
    /// (`_container_ptr_ref`) must be a GC root or a GC-safe object like
    /// call stack (see `struct zis_callstack`) or locals (see `zis_locals_decl()`)
    /// so that the reference will not be smashed by a garbage collection.
    void *_container_ptr_ref;

    /// Number of bytes from the beginning of the container to the first object pointer.
    size_t _offset;

    /// Number of pointers in the view.
    size_t length;
};

/// Initialize a vec_view.
#define zis_object_vec_view_init(vec_view, container_ptr_ref, offset, _length) \
do { \
    _Static_assert(sizeof(*(container_ptr_ref)) == sizeof(void *), "container_ptr_ref"); \
    (vec_view)._container_ptr_ref = (container_ptr_ref); \
    (vec_view)._offset = (offset); \
    (vec_view).length = (_length); \
} while(0)

/// Make a vec_view from slots in a frame.
/// @see struct zis_callstack
#define zis_object_vec_view_from_frame(frame_expr, var_start, var_count) \
    ((struct zis_object_vec_view){ \
        ._container_ptr_ref = _Generic((frame_expr), struct zis_object **: &(frame_expr)), \
        ._offset = (var_start) * sizeof(struct zis_object *), \
        .length = (var_count), \
    })

/// Make a vec_view from object slots (fields).
/// The object expression `safe_object_ptr_expr` is expected to be something like
/// `frame[2]` (see `struct zis_callstack`) or `locals.some_var` (see `zis_locals_decl()`),
/// so that `&safe_object_ptr_expr` can be meaningful.
/// @see struct zis_object
#define zis_object_vec_view_from_fields(safe_object_ptr_expr, object_struct, object_struct_member, start, count) \
    ((struct zis_object_vec_view){ \
        ._container_ptr_ref = _Generic((safe_object_ptr_expr), struct zis_object *: &(safe_object_ptr_expr), object_struct *: &(safe_object_ptr_expr)), \
        ._offset = offsetof(object_struct, object_struct_member) + (start) * sizeof(struct zis_object *), \
        .length = (count), \
    })

/// Get the array data (an array of object pointers).
/// @warning Must re-fetch the data after statements that may cause garbage collections!
/// @see zis_object_vec_view_foreach() ; zis_object_vec_view_foreach_unchanged()
#define zis_object_vec_view_data(vec_view) \
    ((struct zis_object **)(*((char **)((vec_view)._container_ptr_ref)) + (vec_view)._offset))

/// Get the number of elements in the array.
/// @note The length never changes.
#define zis_object_vec_view_length(vec_view) \
    ((size_t)(vec_view).length)

/// Iterate over the elements.
#define zis_object_vec_view_foreach(vec_view, obj_var, stmt) \
do { \
    const size_t __vec_view_len = zis_object_vec_view_length(vec_view); \
    for (size_t __vec_view_i = 0; __vec_view_i < __vec_view_len; __vec_view_i++) { \
        struct zis_object *const obj_var = \
            zis_object_vec_view_data(vec_view)[__vec_view_i]; \
        { stmt } \
    } \
} while (0)

/// Iterate over the elements. Assume that the data not changed.
#define zis_object_vec_view_foreach_unchanged(vec_view, obj_var, stmt) \
do { \
    const size_t __vec_view_len = zis_object_vec_view_length(vec_view); \
    struct zis_object **const __vec_view_data = zis_object_vec_view_data(vec_view); \
    for (size_t __vec_view_i = 0; __vec_view_i < __vec_view_len; __vec_view_i++) { \
        struct zis_object *const obj_var = __vec_view_data[__vec_view_i]; \
        { stmt } \
    } \
} while (0)
