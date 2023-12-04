/// The `Tuple` type.

#pragma once

#include "attributes.h"
#include "object.h"

struct zis_context;

/// `Tuple` object. Immutable array of objects.
struct zis_tuple_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *_slots_num;
    struct zis_object *_data[];
};

/// Create a `Tuple` object.
void zis_tuple_obj_new(
    struct zis_context *z, struct zis_object **ret,
    struct zis_object *v[], size_t n
);

struct zis_tuple_obj *_zis_tuple_obj_new_empty(struct zis_context *z);

/// Return the number of elements in the tuple.
zis_static_force_inline size_t zis_tuple_obj_length(const struct zis_tuple_obj *self) {
    assert(zis_object_is_smallint(self->_slots_num));
    const zis_smallint_t n = zis_smallint_from_ptr(self->_slots_num);
    assert(n >= 1);
    return (size_t)(n - 1);
}

/// Return the array of elements in the tuple.
zis_static_force_inline struct zis_object *const *
zis_tuple_obj_data(const struct zis_tuple_obj *self) {
    return self->_data;
}
