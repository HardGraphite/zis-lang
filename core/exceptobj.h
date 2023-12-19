/// The Exception type.

#pragma once

#include "attributes.h"
#include "compat.h"
#include "object.h"

struct zis_context;

/// `Exception` object.
struct zis_exception_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *type; ///< Exception type.
    struct zis_object *what; ///< Message.
    struct zis_object *data; ///< Exception data.
};

/// Create an `Exception` object. `type`, `what`, and `data` are all optional.
struct zis_exception_obj *zis_exception_obj_new(
    struct zis_context *z,
    struct zis_object *type, struct zis_object *what, struct zis_object *data
);

/// [Mi] Exception - new.
/// Regs :: { [0] = type , [1] = what , [2] = data }.
/// Status :: -1 = not found; 0 = OK; 1 = OK, new buckets.
struct zis_exception_obj *zis_exception_obj_Mi_new(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 3]
);
