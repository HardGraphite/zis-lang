/// Common algorithms.

#pragma once

/// Round up to `to_n`, where `to_n` must be a power of 2.
#define zis_round_up_to_n_pow2(to_n, num) \
    (((num) + ((to_n) - 1)) & ~((to_n) - 1))

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
