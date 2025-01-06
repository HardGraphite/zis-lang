/// The `Tuple` type.

#pragma once

#include "attributes.h"
#include "object.h"
#include "objvec.h"

struct zis_context;

/// `Tuple` object. Immutable array of objects.
struct zis_tuple_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *_slots_num;
    struct zis_object *_data[];
};

/// Create a `Tuple` object.
struct zis_tuple_obj *zis_tuple_obj_new(
    struct zis_context *z,
    struct zis_object *v[], size_t n
);

struct zis_tuple_obj *_zis_tuple_obj_new_empty(struct zis_context *z);

/// Concatenate a vector of tuples.
struct zis_tuple_obj *zis_tuple_obj_concat(
    struct zis_context *z,
    struct zis_object_vec_view tuples
);

/// Create a shallow copy of a range of elements in a tuple.
/// If indices (`start` to `start + length - 1`) are out of range, returns NULL.
struct zis_tuple_obj *zis_tuple_obj_slice(
    struct zis_context *z,
    struct zis_tuple_obj *tuple, size_t start, size_t length
);

/// Return the number of elements in the tuple.
zis_static_force_inline size_t zis_tuple_obj_length(const struct zis_tuple_obj *self) {
    assert(zis_object_is_smallint(self->_slots_num));
    const zis_smallint_t n = zis_smallint_from_ptr(self->_slots_num);
    assert(n >= 1);
    return (size_t)(n - 1);
}

/// Get element without bounds checking.
zis_static_force_inline struct zis_object *
zis_tuple_obj_get(const struct zis_tuple_obj *self, size_t index) {
    assert(index < zis_tuple_obj_length(self));
    return self->_data[index];
}

/// Get element with bounds checking. Returns NULL if `index` is out of range.
zis_static_force_inline struct zis_object *
zis_tuple_obj_get_checked(const struct zis_tuple_obj *self, size_t index) {
    assert(zis_object_is_smallint(self->_slots_num));
    const zis_smallint_t n = zis_smallint_from_ptr(self->_slots_num);
    assert(n >= 1);
    if (zis_unlikely(index + 1 >= (size_t)n))
        return NULL;
    return self->_data[index];
}

/// Return the array of elements in the tuple.
zis_static_force_inline struct zis_object *const *
zis_tuple_obj_data(const struct zis_tuple_obj *self) {
    return self->_data;
}
