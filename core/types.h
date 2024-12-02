/// C data types.

#pragma once

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#if SIZE_MAX == UINTPTR_MAX
    typedef intptr_t        zis_ssize_t;
#    define ZIS_SSIZE_MIN   INTPTR_MIN
#    define ZIS_SSIZE_MAX   INTPTR_MAX
#    define ZIS_SSIZE_PRId  PRIdPTR
#    define ZIS_SSIZE_PRIi  PRIiPTR
#    define ZIS_SSIZE_SCNd  SCNdPTR
#    define ZIS_SSIZE_SCNi  SCNiPTR
#elif SIZE_MAX <= UINT64_MAX
    typedef int64_t         zis_ssize_t;
#    define ZIS_SSIZE_MIN   INT64_MIN
#    define ZIS_SSIZE_MAX   INT64_MAX
#    define ZIS_SSIZE_PRId  PRId64
#    define ZIS_SSIZE_PRIi  PRIi64
#    define ZIS_SSIZE_SCNd  SCNd64
#    define ZIS_SSIZE_SCNi  SCNi64
#elif SIZE_MAX <= UINT32_MAX
    typedef int32_t         zis_ssize_t;
#    define ZIS_SSIZE_MIN   INT32_MIN
#    define ZIS_SSIZE_MAX   INT32_MAX
#    define ZIS_SSIZE_PRId  PRId32
#    define ZIS_SSIZE_PRIi  PRIi32
#    define ZIS_SSIZE_SCNd  SCNd32
#    define ZIS_SSIZE_SCNi  SCNi32
#elif SIZE_MAX <= UINT16_MAX
    typedef int16_t         zis_ssize_t;
#    define ZIS_SSIZE_MIN   INT16_MIN
#    define ZIS_SSIZE_MAX   INT16_MAX
#    define ZIS_SSIZE_PRId  PRId16
#    define ZIS_SSIZE_PRIi  PRIi16
#    define ZIS_SSIZE_SCNd  SCNd16
#    define ZIS_SSIZE_SCNi  SCNi16
#elif SIZE_MAX <= UINT8_MAX
    typedef int8_t          zis_ssize_t;
#    define ZIS_SSIZE_MIN   INT8_MIN
#    define ZIS_SSIZE_MAX   INT8_MAX
#    define ZIS_SSIZE_PRId  PRId8
#    define ZIS_SSIZE_PRIi  PRIi8
#    define ZIS_SSIZE_SCNd  SCNd8
#    define ZIS_SSIZE_SCNi  SCNi8
#else
#    error "zis_ssize_t is not defined"
#endif
