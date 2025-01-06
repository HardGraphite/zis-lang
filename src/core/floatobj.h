/// The Float object.

#pragma once

#include "attributes.h"
#include "object.h"

struct zis_context;
struct zis_object;

/// `Float` object. Floating-point numbers.
struct zis_float_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    double _value;
};

/// Create a new Float object.
struct zis_float_obj *zis_float_obj_new(struct zis_context *z, double val);

/// Get floating-point value.
zis_static_force_inline double zis_float_obj_value(const struct zis_float_obj *self) {
    return self->_value;
}
