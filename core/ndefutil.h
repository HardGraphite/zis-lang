/// Utilities for handling definitions of native functions, types, and modules.

#pragma once

#include <stddef.h>

#include "object.h"
#include "zis.h" // zis_native_*

/// Variable name for `ZIS_NATIVE_NAME_LIST_DEF()`.
#define ZIS_NATIVE_NAME_LIST_VAR(NAME)  _name_list_ ## NAME

/// Generate a `const char * []` variable.
#define ZIS_NATIVE_NAME_LIST_DEF(NAME, ...) \
static const char *const ZIS_NATIVE_NAME_LIST_VAR( NAME ) [] = { \
    __VA_ARGS__                             \
    NULL,                                   \
}                                           \
// ^^^ ZIS_NATIVE_FUNC_LIST_DEF() ^^^

/// Variable name for `ZIS_NATIVE_FUNC_LIST_DEF()`.
#define ZIS_NATIVE_FUNC_LIST_VAR(NAME)  _func_list_ ## NAME

/// Generate a `struct zis_native_func_def []` variable.
#define ZIS_NATIVE_FUNC_LIST_DEF(NAME, ...) \
static const struct zis_native_func_def ZIS_NATIVE_FUNC_LIST_VAR( NAME ) [] = { \
    __VA_ARGS__                             \
    { NULL, { 0, 0, 0 }, NULL },            \
}                                           \
// ^^^ ZIS_NATIVE_FUNC_LIST_DEF() ^^^

/// Variable name for `ZIS_NATIVE_TYPE_DEF()`.
#define ZIS_NATIVE_TYPE_VAR(NAME)  __zis__type_ ## NAME

/// Generate a non-static `zis_native_type_def` variable.
#define ZIS_NATIVE_TYPE_DEF( \
    NAME, STRUCT, BYTES_FIRST_VAR, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                            \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {            \
    .name = #NAME,           \
    .slots_num = (offsetof(STRUCT, BYTES_FIRST_VAR) - ZIS_OBJECT_HEAD_SIZE) / sizeof(void *), \
    .bytes_size = sizeof(STRUCT) - offsetof(STRUCT, BYTES_FIRST_VAR),       \
    .slots = SLOT_NAME_LIST, \
    .methods = METHOD_LIST,  \
    .statics = STATIC_LIST,  \
}                            \
// ^^^ ZIS_NATIVE_TYPE_DEF() ^^^

/// Generate a non-static `zis_native_type_def` variable with no BYTES part.
#define ZIS_NATIVE_TYPE_DEF_NB( \
    NAME, STRUCT, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                               \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {  \
    .name = #NAME,              \
    .slots_num = (sizeof(STRUCT) - ZIS_OBJECT_HEAD_SIZE) / sizeof(void *), \
    .bytes_size = 0,            \
    .slots = SLOT_NAME_LIST,    \
    .methods = METHOD_LIST,     \
    .statics = STATIC_LIST,     \
}                               \
// ^^^ ZIS_NATIVE_TYPE_DEF_NB() ^^^

/// Generate a non-static `zis_native_type_def` variable with extendable SLOTS part and no BYTES part.
#define ZIS_NATIVE_TYPE_DEF_XS_NB( \
    NAME, STRUCT, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                               \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {  \
    .name = #NAME,              \
    .slots_num = (size_t)-1, \
    .bytes_size = 0,   \
    .slots = SLOT_NAME_LIST,    \
    .methods = METHOD_LIST,     \
    .statics = STATIC_LIST,     \
} // ^^^ ZIS_NATIVE_TYPE_DEF_XB() ^^^

/// Generate a non-static `zis_native_type_def` variable with extendable BYTES part.
#define ZIS_NATIVE_TYPE_DEF_XB( \
    NAME, STRUCT, BYTES_SIZE_VAR, SLOT_NAME_LIST, METHOD_LIST, STATIC_LIST \
)                               \
const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( NAME ) = {  \
    .name = #NAME,              \
    .slots_num = (offsetof(STRUCT, BYTES_SIZE_VAR) - ZIS_OBJECT_HEAD_SIZE) / sizeof(void *), \
    .bytes_size = (size_t)-1,   \
    .slots = SLOT_NAME_LIST,    \
    .methods = METHOD_LIST,     \
    .statics = STATIC_LIST,     \
} // ^^^ ZIS_NATIVE_TYPE_DEF_XB() ^^^

/// Size of fixed part of extendable BYTES part of a native object based on the C struct.
#define ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(STRUCT, BYTES_SIZE_VAR) \
    (sizeof(STRUCT) - offsetof(STRUCT, BYTES_SIZE_VAR))
