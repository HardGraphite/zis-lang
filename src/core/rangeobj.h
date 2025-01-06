/// The `Range` type.

#pragma once

#include "object.h"
#include "types.h"

struct zis_context;

/// `Range` object.
struct zis_range_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    zis_ssize_t begin; // index of the first element
    zis_ssize_t end;   // index of the last element (inclusive end)
};

/// Create a `Range` object.
struct zis_range_obj *zis_range_obj_new(
    struct zis_context *z,
    zis_ssize_t begin, zis_ssize_t end
);

/// Create a `Range` object. On error, throws exception and returns NULL.
struct zis_range_obj *zis_range_obj_new_ob(
    struct zis_context *z,
    struct zis_object *begin, struct zis_object *end, bool exclude_end
);
