/// Common algorithms.

#pragma once

#include <stddef.h>

/* ----- numbers ------------------------------------------------------------ */

/// Round up to a multiple of `to_n`, where `to_n` must be a power of 2.
#define zis_round_up_to_n_pow2(to_n, num) \
    (((num) + ((to_n) - 1U)) & ~((to_n) - 1U))

/// Check whether an unsigned integer `num` (of type `type`) is in range [`min`,`max`].
#define zis_uint_in_range(type, num, min, max) \
    ((type)(num) - (type)(min) <= (type)(max) - (type)(min))

/* ----- hash functions ----------------------------------------------------- */

/// Calculate hash code for a floating-point number.
size_t zis_hash_float(double num);

/// Calculate hash code for a pointer.
size_t zis_hash_pointer(const void *ptr);

/// Calculate hash code for bytes or a string.
size_t zis_hash_bytes(const void *data, size_t size);

/* ----- others ------------------------------------------------------------- */

/// Unreachable statement.
#define zis_unreachable() (_zis_unreachable_impl())
#ifndef NDEBUG
#    include <stdlib.h>
#    define _zis_unreachable_impl() abort()
#elif defined __GNUC__
#    define _zis_unreachable_impl() __builtin_unreachable()
#elif defined _MSC_VER
#    define _zis_unreachable_impl() __assume(0)
#else
#    include <stdlib.h>
#    define _zis_unreachable_impl() abort()
#endif
