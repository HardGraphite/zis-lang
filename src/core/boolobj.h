/// The Bool object.

#pragma once

#include <stdbool.h>

#include "attributes.h"
#include "object.h"

struct zis_context;

/// `Bool` object. Boolean values.
struct zis_bool_obj {
    ZIS_OBJECT_HEAD
    bool _value;
};

struct zis_bool_obj *_zis_bool_obj_new(struct zis_context *z, bool v);

/// Get boolean value of the Bool object.
zis_static_force_inline bool zis_bool_obj_value(const struct zis_bool_obj *self) {
    return self->_value;
}
