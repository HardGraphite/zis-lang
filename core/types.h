/// C data types.

#pragma once

#include <stddef.h>
#include <stdint.h>

#if SIZE_MAX == UINTPTR_MAX
    typedef intptr_t      zis_ssize_t;
#    define ZIS_SSIZE_MIN INTPTR_MIN
#    define ZIS_SSIZE_MAX INTPTR_MAX
#elif SIZE_MAX <= UINT64_MAX
    typedef int64_t       zis_ssize_t;
#    define ZIS_SSIZE_MIN INT64_MIN
#    define ZIS_SSIZE_MAX INT64_MAX
#elif SIZE_MAX <= UINT32_MAX
    typedef int32_t       zis_ssize_t;
#    define ZIS_SSIZE_MIN INT32_MIN
#    define ZIS_SSIZE_MAX INT32_MAX
#elif SIZE_MAX <= UINT16_MAX
    typedef int16_t       zis_ssize_t;
#    define ZIS_SSIZE_MIN INT16_MIN
#    define ZIS_SSIZE_MAX INT16_MAX
#elif SIZE_MAX <= UINT8_MAX
    typedef int8_t        zis_ssize_t;
#    define ZIS_SSIZE_MIN INT8_MIN
#    define ZIS_SSIZE_MAX INT8_MAX
#else
#    error "zis_ssize_t is not defined"
#endif
