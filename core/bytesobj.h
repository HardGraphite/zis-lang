/// The `Bytes` type.

#pragma once

#include "attributes.h"
#include "object.h"

struct zis_context;

/// The `Bytes` object. An immutable array of bytes.
struct zis_bytes_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size;
    size_t _size;
    char   _data[];
};

struct zis_bytes_obj *_zis_bytes_obj_new_empty(struct zis_context *z);

/// Create a `Bytes` object.
struct zis_bytes_obj *zis_bytes_obj_new(
    struct zis_context *z,
    const void *volatile data, size_t size
);

/// Get a the bytes data.
zis_static_force_inline const void *zis_bytes_obj_data(const struct zis_bytes_obj *self) {
    return self->_data;
}

/// Get the size of the bytes data.
zis_static_force_inline size_t zis_bytes_obj_size(const struct zis_bytes_obj *self) {
    return self->_size;
}
