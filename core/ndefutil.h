/// Utilities for handling definitions of native functions, types, and modules.

#pragma once

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "attributes.h"
#include "object.h"
#include "zis.h" // zis_native_*

/* ----- object structs ----------------------------------------------------- */

/// Check whether a C struct may be an object struct
#define zis_struct_maybe_object(struct_type) ( \
    sizeof(struct_type) >= ZIS_OBJECT_HEAD_SIZE && \
    offsetof(struct_type, _meta) == 0 &&       \
    _Generic(((struct_type *)0)->_meta, struct zis_object_meta: 1, default: 0) \
)                                              \
// ^^^ zis_struct_maybe_object() ^^^

/* ----- convenience macros to define native things ------------------------- */

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
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
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
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
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
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
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
static_assert(zis_struct_maybe_object(STRUCT), "not an object-struct: " #STRUCT); \
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

/* ----- functions to operate a vector of objects --------------------------- */

/// Copy a vector of object pointers like `memcpy()`.
zis_static_force_inline struct zis_object **
zis_object_vec_copy(struct zis_object **restrict dst, struct zis_object *const *restrict src, size_t n) {
    return memcpy(dst, src, n * sizeof(struct zis_object *));
}

/// Copy a vector of object pointers like `memmove()`.
zis_static_force_inline struct zis_object **
zis_object_vec_move(struct zis_object **restrict dst, struct zis_object *const *restrict src, size_t n) {
    return memmove(dst, src, n * sizeof(struct zis_object *));
}

/// Fill a vector of object pointers with small integers like `memset()`.
zis_static_force_inline struct zis_object **
zis_object_vec_zero(struct zis_object **restrict vec, size_t n) {
    return memset(vec, 0xff, n * sizeof(struct zis_object *));
}

/// Fill a vector of object pointers with a specific object.
zis_static_force_inline void
zis_object_vec_fill(struct zis_object **restrict vec, struct zis_object *val, size_t n) {
    for (size_t i = 0; i < n; i++) vec[i] = val;
}
