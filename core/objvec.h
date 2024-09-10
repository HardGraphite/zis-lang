/// Operations on vectors of object pointers.

#pragma once

#include <string.h>

#include "attributes.h"

struct zis_object;

/// Copy a vector of object pointers like `memcpy()`.
zis_static_force_inline void zis_object_vec_copy(
    struct zis_object **restrict dst,
    struct zis_object *const *restrict src, size_t n
) {
    memcpy(dst, src, n * sizeof(struct zis_object *));
}

/// Copy a vector of object pointers like `memmove()`.
zis_static_force_inline void zis_object_vec_move(
    struct zis_object **restrict dst,
    struct zis_object *const *restrict src, size_t n
) {
    memmove(dst, src, n * sizeof(struct zis_object *));
}

/// Fill a vector of object pointers with small integers like `memset()`.
zis_static_force_inline void zis_object_vec_zero(
    struct zis_object **restrict vec, size_t n
) {
    memset(vec, 0xff, n * sizeof(struct zis_object *));
}

/// Fill a vector of object pointers with a specific object.
zis_static_force_inline void zis_object_vec_fill(
    struct zis_object **restrict vec, struct zis_object *val, size_t n
) {
    for (size_t i = 0; i < n; i++)
        vec[i] = val;
}
