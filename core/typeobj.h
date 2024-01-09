/// The Type objects.

#pragma once

#include <assert.h>
#include <stddef.h>

#include "attributes.h"
#include "compat.h"
#include "object.h"

#include "arrayobj.h"

struct zis_context;
struct zis_map_obj;
struct zis_native_type_def;
struct zis_symbol_obj;

/// `Type` object.
struct zis_type_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_array_slots_obj *_methods; ///< Method table.
    struct zis_map_obj         *_name_map;///< { name (Symbol) -> field_or_method_index (smallint, i_method = -1 - i) }.
    struct zis_map_obj         *_statics; ///< Static member variables.
    // --- BYTES ---
    size_t _slots_num; ///< Number of slots. `-1` means extendable.
    size_t _bytes_len; ///< Size (bytes) of the bytes part. `-1` means extendable.
    size_t _obj_size;  ///< Object size. `0` means SLOTS or BYTES extendable and the size needs to be calculated.
};

struct zis_type_obj *_zis_type_obj_bootstrap_alloc(struct zis_context *z, const struct zis_native_type_def *restrict def);
void _zis_type_obj_bootstrap_init_r(struct zis_context *z, struct zis_type_obj *self, struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]);

/// Create an empty `Type`.
/// R = { [0] = out_type_obj, [1] = tmp }.
struct zis_type_obj *zis_type_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]
);

/// Load a native type def.
void zis_type_obj_load_native_def(
    struct zis_context *z,
    struct zis_type_obj *self, const struct zis_native_type_def *restrict def
);

/// Get the index of a field by name. Returns -1 if not found.
size_t zis_type_obj_find_field(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
);

/// Get the index of a method by name. Returns -1 if not found.
size_t zis_type_obj_find_method(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
);

/// Get the number of methods.
zis_static_force_inline size_t zis_type_obj_method_count(
    const struct zis_type_obj *self
) {
    return zis_array_slots_obj_length(self->_methods);
}

/// Get a method by index. No bounds checking.
zis_static_force_inline struct zis_object *zis_type_obj_get_method_i(
    const struct zis_type_obj *self,
    size_t index
) {
    return zis_array_slots_obj_get(self->_methods, index);
}

/// Update a method by index. No bounds checking.
void zis_type_obj_set_method_i(
    const struct zis_type_obj *self,
    size_t index, struct zis_object *new_method
);

/// Get a method by index. Returns NULL if not found.
struct zis_object *zis_type_obj_get_method(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
);

/// Get a static member. Returns NULL if not exists.
struct zis_object *zis_type_obj_get_static(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
);

/// Get a static member. Returns NULL if not exists.
void zis_type_obj_set_static(
    struct zis_context *z, struct zis_type_obj *self,
    struct zis_symbol_obj *name, struct zis_object *value
);

/// Get number of slots in SLOTS of an object.
zis_static_force_inline size_t zis_object_slot_count(const struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    const size_t n = zis_object_type(obj)->_slots_num;
    if (zis_likely(n != (size_t)-1))
        return n;
    struct zis_object *const vn = zis_object_get_slot(obj, 0);
    assert(zis_object_is_smallint(vn));
    return (size_t)zis_smallint_from_ptr(vn);
}

/// Get size in bytes of BYTES of an object.
zis_static_force_inline size_t zis_object_bytes_size(const struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    const size_t n = zis_object_type(obj)->_bytes_len;
    if (zis_likely(n != (size_t)-1))
        return n;
    return *(size_t *)zis_object_ref_bytes(obj, zis_object_slot_count(obj));
}

/// Object size in bytes.
zis_static_force_inline size_t zis_object_size(const struct zis_object *obj) {
    assert(!zis_object_is_smallint(obj));
    struct zis_type_obj *const type = zis_object_type(obj);
    const size_t obj_size = type->_obj_size;
    if (zis_likely(obj_size))
        return obj_size;
    size_t slot_count, bytes_size;
    // SLOTS size. See `zis_object_slot_count()`.
    slot_count = type->_slots_num;
    if (slot_count == (size_t)-1) {
        struct zis_object *const vn = zis_object_get_slot(obj, 0);
        assert(zis_object_is_smallint(vn));
        slot_count = (size_t)zis_smallint_from_ptr(vn);
    }
    // BYTES size. See `zis_object_bytes_size()`.
    bytes_size = type->_bytes_len;
    if (bytes_size == (size_t)-1) {
        bytes_size = *(size_t *)zis_object_ref_bytes(obj, slot_count);
    }
    // HEAD + SLOTS + BYTES
    return ZIS_OBJECT_HEAD_SIZE + (slot_count * sizeof(struct zis_object *)) + bytes_size;
}
