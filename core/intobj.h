/// The Int object.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "smallint.h"

struct zis_context;

/// `Int` object. Arbitrary precision integer (aka big integer).
struct zis_int_obj;

/// Create a small int or an `Int` object.
struct zis_object *zis_int_obj_or_smallint(struct zis_context *z, int64_t val);

/// Create a small int or an `Int` object from string. Underscores ('_') are ignored.
/// `base` must be in range [2,36]. Returns NULL if no valid character is given.
struct zis_object *zis_int_obj_or_smallint_s(
    struct zis_context *z,
    const char *restrict str, const char **restrict str_end_p,
    unsigned int base
);

/// Return true if the integer is negative, false otherwise.
bool zis_int_obj_sign(const struct zis_int_obj *self);

/// Get value as `int64_t`. If the value falls out of range of type `int64_t`,
/// `errno` is set to `ERANGE` and `INT64_MIN` is returned.
int64_t zis_int_obj_value_i(const struct zis_int_obj *self);

/// Get the lower 63 bits of the integer and return an int64.
int64_t zis_int_obj_value_trunc(const struct zis_int_obj *self);

/// Get value as `double`. The result may be inaccurate. If the value falls
/// out of range of type `double`, `errno` is set to `ERANGE` and `HUGE_VAL` is returned.
double zis_int_obj_value_f(const struct zis_int_obj *self);

/// Represent the integer as a string. Negative `base` to use uppercase letters.
/// `buf=NULL` to calculate the required buffer size.
/// If the buffer is not big enough, returns -1.
size_t zis_int_obj_value_s(const struct zis_int_obj *self, char *restrict buf, size_t buf_sz, int base);

/// See `zis_int_obj_value_s()`.
size_t zis_smallint_to_str(zis_smallint_t i, char *restrict buf, size_t buf_sz, int base);

struct zis_object *zis_int_obj_add_x(struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs);
struct zis_object *zis_int_obj_mul_x(struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs);
