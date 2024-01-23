/// Local references to objects in native functions.

/// @note Include "context.h" to use the macros defined in this file.

#pragma once

#include <assert.h>
#include <stddef.h>

struct zis_context;

/// Declare local variables that can hold references to objects.
/// The parameter `...` is a list of object pointer variable declarations.
/// Any other type is illegal and can cause unexpected result.
/// After declaration, initialize them using `zis_locals_zero()` or one by one.
/// Macro `zis_locals_drop()` must be used to finalize these variables.
/// An example:
/// ```
/// zis_locals_decl(
///   z, var,
///   struct zis_string_obj *s1, *s2;
///   struct zis_object     *obj;
/// );
/// ```
#define zis_locals_decl(__z, __namespace, ...) \
    _zis_locals_drop_check_decl_(__namespace)  \
    struct { struct _zis_locals_head _head; __VA_ARGS__ } __namespace; \
do {                                           \
    __namespace._head._next = (__z)->locals_root._list;                \
    (__z)->locals_root._list = &__namespace._head;                     \
    __namespace._head._size = sizeof __namespace;                      \
} while (0)                                    \
// ^^^ zis_locals_decl() ^^^

/// Initialize the references like `zis_object_vec_zero()`.
#define zis_locals_zero(__z, __namespace) \
do {                                      \
    static_assert(sizeof __namespace >= sizeof(void *), ""); \
    assert(sizeof __namespace == __namespace._head._size);   \
    const size_t n = (sizeof __namespace - sizeof(struct _zis_locals_head)) / sizeof(void *); \
    _zis_locals_block_zero(&__namespace._head, n);           \
} while (0)                               \
// ^^^ zis_locals_zero() ^^^

/// Un-declare local variables declared with `zis_locals_decl()`.
/// The variables must be dropped in the reverse order of declaration.
#define zis_locals_drop(__z, __namespace) \
do {                                      \
    _zis_locals_drop_check_drop_(__namespace)               \
    assert((__z)->locals_root._list == &__namespace._head); \
    (__z)->locals_root._list = __namespace._head._next;     \
} while (0)                               \
// ^^^ zis_locals_drop() ^^^

/* ----- internal implementations ------------------------------------------- */

struct _zis_locals_head {
    struct _zis_locals_head *_next;
    size_t                   _size; // size of the block (including the head) in bytes
};

struct _zis_locals_root {
    struct _zis_locals_head *_list;
};

void _zis_locals_root_init(struct _zis_locals_root *lr, struct zis_context *z);

void _zis_locals_root_fini(struct _zis_locals_root *lr, struct zis_context *z);

#ifdef NDEBUG
#    define _zis_locals_drop_check_decl_(__namespace)
#    define _zis_locals_drop_check_drop_(__namespace)
#else // NDEBUG
#    define _zis_locals_drop_check_decl_(__namespace)  int __##__namespace##_not_dropped;
#    define _zis_locals_drop_check_drop_(__namespace)  ((void)__##__namespace##_not_dropped);
#endif // NDEBUG

void _zis_locals_block_zero(struct _zis_locals_head *h, size_t n);
