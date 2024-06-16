/// The `Array` type.

#pragma once

#include "attributes.h"
#include "object.h"
#include "objmem.h" // zis_object_write_barrier()

struct zis_context;

/* ----- array slots -------------------------------------------------------- */

/// `Array.Slots` object.
struct zis_array_slots_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *_slots_num;
    struct zis_object *_data[];
};

/// Create slots and initialize with `v[0 ... n-1]`. `v` can be NULL.
struct zis_array_slots_obj *zis_array_slots_obj_new(
    struct zis_context *z, struct zis_object *v[], size_t n
);

/// Create slots and initialize with another.
struct zis_array_slots_obj *zis_array_slots_obj_new2(
    struct zis_context *z, size_t len,
    struct zis_array_slots_obj *other_slots
);

struct zis_array_slots_obj *_zis_array_slots_obj_new_empty(struct zis_context *z);

/// Return number of elements.
zis_static_force_inline size_t zis_array_slots_obj_length(
    const struct zis_array_slots_obj *self
) {
    assert(zis_object_is_smallint(self->_slots_num));
    const zis_smallint_t n = zis_smallint_from_ptr(self->_slots_num);
    assert(n >= 1);
    return (size_t)(n - 1);
}

/// Get element. No bounds checking.
zis_static_force_inline struct zis_object *zis_array_slots_obj_get(
    const struct zis_array_slots_obj *self, size_t i
) {
    // See `zis_object_get_slot()`.
    assert(i < zis_array_slots_obj_length(self));
    return self->_data[i];
}

/// Set element. No bounds checking.
zis_static_force_inline void zis_array_slots_obj_set(
    struct zis_array_slots_obj *self, size_t i, struct zis_object *v
) {
    // See `zis_object_set_slot()`.
    assert(i < zis_array_slots_obj_length(self));
    self->_data[i] = v;
    zis_object_write_barrier(self, v);
}

/* ----- array -------------------------------------------------------------- */

/// `Array` object. Array of objects with dynamic length.
struct zis_array_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_array_slots_obj *_data;
    // --- BYTES ---
    size_t length;
};

/// Create an `Array` object. Initialize data `v` can be NULL.
struct zis_array_obj *zis_array_obj_new(
    struct zis_context *z,
    struct zis_object *v[], size_t n
);

/// Create an `Array` object. Initialize data `v` can be NULL.
struct zis_array_obj *zis_array_obj_new2(
    struct zis_context *z,
    size_t reserve, struct zis_object *v[], size_t n
);

/// Concatenate a vector of arrays. Always creates a new array.
struct zis_array_obj *zis_array_obj_concat(
    struct zis_context *z,
    struct zis_array_obj *v[], size_t n
);

/// Return number of elements.
zis_static_force_inline size_t zis_array_obj_length(
    const struct zis_array_obj *self
) {
    return self->length;
}

/// Get element without bounds checking.
zis_static_force_inline struct zis_object *zis_array_obj_get(
    const struct zis_array_obj *self, size_t i
) {
    assert(i < self->length);
    return zis_array_slots_obj_get(self->_data, i);
}

/// Get element with bounds checking. Return NULL if `i` is out of range,
zis_static_force_inline zis_nodiscard struct zis_object *zis_array_obj_get_checked(
    const struct zis_array_obj *self, size_t i
) {
    if (zis_unlikely(i >= self->length))
        return NULL;
    return zis_array_slots_obj_get(self->_data, i);
}

/// Set element without bounds checking.
zis_static_force_inline void zis_array_obj_set(
    struct zis_array_obj *self, size_t i, struct zis_object *v
) {
    assert(i < self->length);
    zis_array_slots_obj_set(self->_data, i, v);
}

/// Set element with bounds checking. Return false if `i` is out of range,
zis_static_force_inline zis_nodiscard bool zis_array_obj_set_checked(
    struct zis_array_obj *self, size_t i, struct zis_object *v
) {
    if (zis_unlikely(i >= self->length))
        return false;
    zis_array_slots_obj_set(self->_data, i, v);
    return true;
}

/// Reserve slots. Won't shrink.
void zis_array_obj_reserve(struct zis_context *z, struct zis_array_obj *self, size_t n);

/// Delete all elements.
void zis_array_obj_clear(struct zis_array_obj *self);

/// Get vector of elements.
zis_static_force_inline struct zis_object *const *
zis_array_obj_data(const struct zis_array_obj *self) {
    return self->_data->_data;
}

/// Get the first element of the array.
/// Return NULL if the array is empty.
zis_static_force_inline struct zis_object *
zis_array_obj_front(const struct zis_array_obj *self) {
    return self->length ? self->_data->_data[0] : NULL;
}

/// Get the last element of the array.
/// Return NULL if the array is empty.
zis_static_force_inline struct zis_object *
zis_array_obj_back(const struct zis_array_obj *self) {
    const size_t n = self->length;
    return n ? self->_data->_data[n - 1] : NULL;
}

/// Add new element to the end of the array.
void zis_array_obj_append(
    struct zis_context *z,
    struct zis_array_obj *self, struct zis_object *v
);

/// Remove and return the last element of the array.
/// Return NULL if the array is empty.
struct zis_object *zis_array_obj_pop(struct zis_array_obj *self);

/// Insert an element to `pos`.
bool zis_array_obj_insert(
    struct zis_context *z,
    struct zis_array_obj *self, size_t pos, struct zis_object *v
);

/// Delete the element at `pos`.
bool zis_array_obj_remove(
    struct zis_context *z,
    struct zis_array_obj *self, size_t pos
);
