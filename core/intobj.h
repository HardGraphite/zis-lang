/// The Int object.

#pragma once

#include <stdint.h>

#include "attributes.h"
#include "smallint.h"

struct zis_context;

/// `Int` object. Arbitrary precision integer (aka big integer).
struct zis_int_obj;

/// Create an `Int` object explicitly. Prefer `zis_smallint_or_int_obj()`.
void _zis_int_obj_new(struct zis_context *z, struct zis_object **ret, int64_t val);

/// Make a small int or an `Int` object.
zis_static_force_inline void zis_smallint_or_int_obj(
    struct zis_context *z, struct zis_object **ret, zis_smallint_t val
) {
    struct zis_object *obj = zis_smallint_try_to_ptr(val);
    if (zis_likely(obj))
        *ret = obj;
    else
        _zis_int_obj_new(z, ret, val);
}

/// Get value as `int`. If the value falls out of range of type `int`,
/// `errno` is set to `ERANGE` and `INT_MIN` is returned.
int zis_int_obj_value_i(const struct zis_int_obj *self);

/// Get value as `int64_t`. If the value falls out of range of type `int64_t`,
/// `errno` is set to `ERANGE` and `INT64_MIN` is returned.
int64_t zis_int_obj_value_l(const struct zis_int_obj *self);

/// Get value as `double`. The result may be inaccurate. If the value falls
/// out of range of type `double`, `errno` is set to `ERANGE` and `HUGE_VAL` is returned.
double zis_int_obj_value_f(const struct zis_int_obj *self);
