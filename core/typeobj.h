/// The Type objects.

#pragma once

#include <assert.h>
#include <stddef.h>

#include "attributes.h"
#include "object.h"

/// `Type` object.
struct zis_type_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    // TODO: methods, field names, ...
    // --- BYTES ---
    size_t _slots_num; ///< Number of slots. `-1` means extendable.
    size_t _bytes_len; ///< Size (bytes) of the bytes part. `-1` means extendable.
    size_t _obj_size;  ///< Object size. `0` means SLOTS or BYTES extendable and the size needs to be calculated.
};

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
    size_t slots_size, bytes_size;
    // SLOTS size. See `zis_object_slot_count()`.
    slots_size = type->_slots_num;
    if (slots_size == (size_t)-1) {
        struct zis_object *const vn = zis_object_get_slot(obj, 0);
        assert(zis_object_is_smallint(vn));
        slots_size = (size_t)zis_smallint_from_ptr(vn);
    }
    slots_size *= sizeof(struct zis_object *);
    // BYTES size. See `zis_object_bytes_size()`.
    bytes_size = type->_bytes_len;
    if (bytes_size == (size_t)-1) {
        bytes_size = *(size_t *)zis_object_ref_bytes(obj, slots_size);
    }
    // HEAD + SLOTS + BYTES
    return ZIS_OBJECT_HEAD_SIZE + slots_size + bytes_size;
}
