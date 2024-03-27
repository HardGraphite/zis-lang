/// Small integers.

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "attributes.h"

struct zis_object;

/// Small int, an integer that is small enough to be held in an object pointer.
typedef intptr_t zis_smallint_t;
typedef uintptr_t zis_smallint_unsigned_t;

#define ZIS_SMALLINT_MIN (INTPTR_MIN >> 1)
#define ZIS_SMALLINT_MAX (INTPTR_MAX >> 1)

#ifdef INTPTR_WIDTH
#    define ZIS_SMALLINT_WIDTH (INTPTR_WIDTH - 1)
#else // !INTPTR_WIDTH
#    define ZIS_SMALLINT_WIDTH (sizeof(zis_smallint_t) * 8 - 1)
#endif // INTPTR_WIDTH

/// Check whether an object pointer is a small int.
zis_static_force_inline bool zis_object_is_smallint(const struct zis_object *obj_ptr) {
    return (uintptr_t)obj_ptr & 1;
}

/// Convert object pointer to small int.
zis_static_force_inline zis_smallint_t zis_smallint_from_ptr(const struct zis_object *ptr) {
    assert(zis_object_is_smallint(ptr));
    return (zis_smallint_t)ptr >> 1;
}

/// Convert small int to object pointer.
zis_static_force_inline struct zis_object *zis_smallint_to_ptr(zis_smallint_t val) {
    void *const ptr = (void *)((val << 1) | (zis_smallint_t)1);
    assert(zis_smallint_from_ptr(ptr) == val);
    return ptr;
}

/// Try to convert small int to object pointer. If overflows, return NULL.
zis_static_force_inline struct zis_object *zis_smallint_try_to_ptr(zis_smallint_t val) {
    void *const ptr = (void *)((val << 1) | (zis_smallint_t)1); // See `zis_smallint_to_ptr()`.
    if (zis_likely(zis_smallint_from_ptr(ptr) == val))
        return ptr;
    return NULL;
}

zis_static_force_inline size_t zis_smallint_hash(zis_smallint_t val) {
    static_assert(sizeof val == sizeof(size_t), "");
    union { zis_smallint_t i; size_t z; } x;
    x.i = val;
    return x.z;
}
