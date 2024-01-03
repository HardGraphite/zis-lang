/// The `Path` type.

#pragma once

#include "fsutil.h"

struct zis_context;

/// The `Path` object. Representing the path to a file.
struct zis_path_obj;

/// Create a `Path` object.
/// `path` is optional. `path_len` is the number of zis_path_char_t chars and can be -1.
struct zis_path_obj *zis_path_obj_new(
    struct zis_context *z,
    const zis_path_char_t *path, size_t path_len
);

/// Get number of zis_path_char_t chars in the path.
size_t zis_path_obj_path_len(const struct zis_path_obj *self);

/// Get path string.
const zis_path_char_t *zis_path_obj_data(const struct zis_path_obj *self);
