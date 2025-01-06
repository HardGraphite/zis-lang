/// The Int object.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "attributes.h"
#include "smallint.h"

struct zis_context;
struct zis_float_obj;

/// `Int` object. Arbitrary precision integer (aka big integer).
struct zis_int_obj;

/// Create a small int or an `Int` object.
struct zis_object *zis_int_obj_or_smallint(struct zis_context *z, int64_t val);

/// Create a small int or an `Int` object from string. Underscores ('_') are ignored.
/// Parameter `base` must be in range [2,36].
/// `*str_end_p` will be pointed to the char after the last read one.
/// If no valid character is given, sets `*str_end_p` to `str` and returns NULL.
/// If the integer is too large, returns NULL.
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
int64_t zis_int_obj_value_trunc_i(const struct zis_int_obj *self);

/// Get value as `double`. The result may be inaccurate. If the value falls
/// out of range of type `double`, `errno` is set to `ERANGE` and `HUGE_VAL` is returned.
double zis_int_obj_value_f(const struct zis_int_obj *self);

/// Represent the integer as a string. Negative `base` to use uppercase letters.
/// `buf=NULL` to calculate the required buffer size.
/// If the buffer is not big enough, returns -1.
size_t zis_int_obj_value_s(const struct zis_int_obj *self, char *restrict buf, size_t buf_sz, int base);

/// See `zis_int_obj_value_s()`.
size_t zis_smallint_to_str(zis_smallint_t i, char *restrict buf, size_t buf_sz, int base);

/// Get the lower `n_bits` bits of the integer.
struct zis_object *zis_int_obj_trunc(
    struct zis_context *z,
    const struct zis_int_obj *num, unsigned int n_bits
);

/// Get the number of used bits, aka bit width.
unsigned int zis_int_obj_length(const struct zis_int_obj *num);

/// Count the number of existing `bit` (0 or 1) in `num`.
unsigned int zis_int_obj_count(const struct zis_int_obj *num, int bit);

/// Small integer arithmetic: `lhs + rhs`.
zis_static_force_inline struct zis_object *zis_smallint_add(
    struct zis_context *z, zis_smallint_t lhs, zis_smallint_t rhs
) {
    _Static_assert(ZIS_SMALLINT_MAX + ZIS_SMALLINT_MAX <= INTPTR_MAX, "");
    _Static_assert(ZIS_SMALLINT_MIN + ZIS_SMALLINT_MIN >= INTPTR_MIN, "");
    zis_smallint_t x = lhs + rhs;
    struct zis_object *p = zis_smallint_try_to_ptr(x);
    if (p)
        return p;
    return zis_int_obj_or_smallint(z, x);
}

/// Small integer arithmetic: `lhs - rhs`.
zis_static_force_inline struct zis_object *zis_smallint_sub(
    struct zis_context *z, zis_smallint_t lhs, zis_smallint_t rhs
) {
    _Static_assert(ZIS_SMALLINT_MAX - ZIS_SMALLINT_MIN <= INTPTR_MAX, "");
    _Static_assert(ZIS_SMALLINT_MIN - ZIS_SMALLINT_MAX >= INTPTR_MIN, "");
    zis_smallint_t x = lhs - rhs;
    struct zis_object *p = zis_smallint_try_to_ptr(x);
    if (p)
        return p;
    return zis_int_obj_or_smallint(z, x);
}

/// Small integer arithmetic: `lhs * rhs`.
struct zis_object *zis_smallint_mul(
    struct zis_context *z, zis_smallint_t lhs, zis_smallint_t rhs
);

/// Integral arithmetic: `lhs + rhs`.
/// If the integer is too large, returns NULL.
struct zis_object *zis_int_obj_or_smallint_add(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral arithmetic: `lhs - rhs`.
/// If the integer is too large, returns NULL.
struct zis_object *zis_int_obj_or_smallint_sub(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral arithmetic: `lhs * rhs`.
/// If the integer is too large, returns NULL.
struct zis_object *zis_int_obj_or_smallint_mul(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral arithmetic: `lhs / rhs`. Returns a floating point number as result.
struct zis_float_obj *zis_int_obj_or_smallint_fdiv(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral arithmetic: `quot = lhs / rhs` and `rem = lhs % rhs`.
/// Generates two integers (quotient and remainder) as results.
/// If `rhs` is zero, returns false; otherwise writes the results into `quot_and_rem` and returns true.
zis_nodiscard bool zis_int_obj_or_smallint_divmod(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs,
    struct zis_object **quot, struct zis_object **rem
);

/// Integral arithmetic: `lhs ** rhs` (power).
/// If `rhs` is negative, returns a floating point number as the result.
/// If the integer is too large, returns NULL.
struct zis_object *zis_int_obj_or_smallint_pow(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral arithmetic: `lhs << rhs`.
/// If the integer is too large, returns NULL.
struct zis_object *zis_int_obj_or_smallint_shl(
    struct zis_context *z, struct zis_object *lhs, unsigned int rhs
);

/// Integral arithmetic: `lhs >> rhs`.
struct zis_object *zis_int_obj_or_smallint_shr(
    struct zis_context *z, struct zis_object *lhs, unsigned int rhs
);

/// Compare two integral values.
int zis_int_obj_or_smallint_compare(struct zis_object *lhs, struct zis_object *rhs);

/// Check whether two integral values equal.
bool zis_int_obj_or_smallint_equals(struct zis_object *lhs, struct zis_object *rhs);

/// Integral bitwise operation: `~val`.
struct zis_object *zis_int_obj_or_smallint_not(
    struct zis_context *z, struct zis_object *val
);

/// Integral bitwise operation: `lhs & rhs`.
struct zis_object *zis_int_obj_or_smallint_and(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral bitwise operation: `lhs | rhs`.
struct zis_object *zis_int_obj_or_smallint_or(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);

/// Integral bitwise operation: `lhs & rhs`.
struct zis_object *zis_int_obj_or_smallint_xor(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
);
