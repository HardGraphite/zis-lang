/// Utilities for handling definitions of native functions, types, and modules.

#pragma once

#include <assert.h>
#include <stddef.h>

#include "object.h"
#include "zis.h" // zis_native_*

/* ----- macros ------------------------------------------------------------- */

#define ZIS_IDENTIFIER_TO_STR(X)  _ZIS_IDENTIFIER_TO_STR(X)
#define _ZIS_IDENTIFIER_TO_STR(X) #X

/* ----- object structs ----------------------------------------------------- */

/// Check whether a C struct may be an object struct
#define zis_struct_maybe_object(struct_type) ( \
    sizeof(struct_type) >= ZIS_OBJECT_HEAD_SIZE && \
    offsetof(struct_type, _meta) == 0 &&       \
    _Generic(((struct_type *)0)->_meta, struct zis_object_meta: 1, default: 0) \
)                                              \
// ^^^ zis_struct_maybe_object() ^^^

/* ----- convenience macros to define native things ------------------------- */

/// Variable name for `ZIS_NATIVE_TYPE_DEF()`.
#define ZIS_NATIVE_TYPE_VAR(NAME)  __zis__type_ ## NAME

/// Generate a non-static `zis_native_type_def` variable.
#define ZIS_NATIVE_TYPE_DEF( \
    NAME, STRUCT, BYTES_FIRST_VAR, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                            \
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {            \
    .slots_num = (offsetof(STRUCT, BYTES_FIRST_VAR) - ZIS_OBJECT_HEAD_SIZE) / sizeof(void *), \
    .bytes_size = sizeof(STRUCT) - offsetof(STRUCT, BYTES_FIRST_VAR),       \
    .fields = SLOT_NAME_LIST,\
    .methods = METHOD_LIST,  \
    .statics = STATIC_LIST,  \
}                            \
// ^^^ ZIS_NATIVE_TYPE_DEF() ^^^

/// Generate a non-static `zis_native_type_def` variable with no BYTES part.
#define ZIS_NATIVE_TYPE_DEF_NB( \
    NAME, STRUCT, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                               \
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {  \
    .slots_num = (sizeof(STRUCT) - ZIS_OBJECT_HEAD_SIZE) / sizeof(void *), \
    .bytes_size = 0,            \
    .fields = SLOT_NAME_LIST,   \
    .methods = METHOD_LIST,     \
    .statics = STATIC_LIST,     \
}                               \
// ^^^ ZIS_NATIVE_TYPE_DEF_NB() ^^^

/// Generate a non-static `zis_native_type_def` variable with extendable SLOTS part and no BYTES part.
#define ZIS_NATIVE_TYPE_DEF_XS_NB( \
    NAME, STRUCT, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                               \
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {  \
    .slots_num = (size_t)-1,    \
    .bytes_size = 0,            \
    .fields = SLOT_NAME_LIST,   \
    .methods = METHOD_LIST,     \
    .statics = STATIC_LIST,     \
} // ^^^ ZIS_NATIVE_TYPE_DEF_XB() ^^^

/// Generate a non-static `zis_native_type_def` variable with extendable BYTES part.
#define ZIS_NATIVE_TYPE_DEF_XB( \
    NAME, STRUCT, BYTES_SIZE_VAR, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                               \
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {  \
    .slots_num = (offsetof(STRUCT, BYTES_SIZE_VAR) - ZIS_OBJECT_HEAD_SIZE) / sizeof(void *), \
    .bytes_size = (size_t)-1,   \
    .fields = SLOT_NAME_LIST,   \
    .methods = METHOD_LIST,     \
    .statics = STATIC_LIST,     \
} // ^^^ ZIS_NATIVE_TYPE_DEF_XB() ^^^

/// Size of fixed part of extendable BYTES part of a native object based on the C struct.
#define ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(STRUCT, BYTES_SIZE_VAR) \
    (sizeof(STRUCT) - offsetof(STRUCT, BYTES_SIZE_VAR))

/// See `ZIS_NATIVE_MODULE__VAR()`.
#define ZIS_NATIVE_MODULE_VARNAME(MOD_NAME)  ZIS_NATIVE_MODULE__VAR(MOD_NAME)

/// Prefix of `ZIS_NATIVE_MODULE_VARNAME()` defined variables as a string.
#define ZIS_NATIVE_MODULE_VARNAME_PREFIX_STR  ZIS_IDENTIFIER_TO_STR(ZIS_NATIVE_MODULE_VARNAME())
