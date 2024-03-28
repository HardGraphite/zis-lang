/// Object definition and interface.

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h> // uintptr_t

#include "platform.h"
#include "smallint.h"

struct zis_type_obj;
struct zis_object;

/* ----- object meta -------------------------------------------------------- */

/// Object meta data.
struct zis_object_meta {

    static_assert(ZIS_WORDSIZE == 64 || ZIS_WORDSIZE == 32, "");

    /**
     * @struct zis_object_meta
     * ## Object Meta Layout
     *
     * ```
     *      W-1    ...      2     1        0     (W = width of uintptr_t)
     *      +----------------+-----------------+
     * [_1] |    TYPE_PTR    |     GC_STATE    |
     *      +----------------+-----------------+
     *      +----------------+--------+--------+
     * [_2] |     GC_PTR     |(unused)| GC_MARK|
     *      +----------------+--------+--------+
     * ```
     */

    uintptr_t _1, _2;
};

/// Initialize meta.
#define zis_object_meta_init(meta, gc_state, gc_ptr, type_ptr) \
    do { (meta)._1 = (uintptr_t)(type_ptr) | (uintptr_t)(gc_state), (meta)._2 = (uintptr_t)gc_ptr; } while (0)

/// Check whether a ptr value can be stored into the meta.
#define zis_object_meta_assert_ptr_fits(ptr) \
    do { static_assert(sizeof(ptr) <= sizeof(uintptr_t), ""); assert(!((uintptr_t)(ptr) & 3U)); } while (0)

/// Set object meta TYPE_PTR.
#define zis_object_meta_set_type_ptr(meta, ptr) \
    do { (meta)._1 = (uintptr_t)(ptr) | ((meta)._1 & (uintptr_t)3U); } while (0)
/// Get object meta TYPE_PTR.
#define zis_object_meta_get_type_ptr(meta) \
    ((struct zis_type_obj *)((meta)._1 & ~(uintptr_t)3U))

/// Set object meta GC_PTR.
#define zis_object_meta_set_gc_ptr(meta, ptr) \
    do { (meta)._2 = (uintptr_t)(ptr) | ((meta)._2 & (uintptr_t)3U); } while (0)
/// Get object meta GC_PTR.
#define zis_object_meta_get_gc_ptr(meta, ret_type) \
    ((ret_type)((meta)._2 & ~(uintptr_t)3U))

/// Set object meta GC_STATE. See `enum zis_objmem_obj_state`.
#define zis_object_meta_set_gc_state(meta, type) \
    do { (meta)._1 = (uintptr_t)(type) | ((meta)._1 & ~(uintptr_t)3U); } while (0)
/// Get object meta GC_STATE. See `enum zis_objmem_obj_state`.
#define zis_object_meta_get_gc_state(meta) \
    ((meta)._1 & (uintptr_t)3U)
/// Get object meta GC_STATE bit-0.
#define zis_object_meta_get_gc_state_bit0(meta) \
    ((meta)._1 & (uintptr_t)1U)
/// Get object meta GC_STATE bit-1.
#define zis_object_meta_get_gc_state_bit1(meta) \
    ((meta)._1 & (uintptr_t)2U)

/// Set object meta GC_MARK to true.
#define zis_object_meta_set_gc_mark(meta) \
    do { (meta)._2 |= (uintptr_t)1U; } while (0)
/// Set object meta GC_MARK to false.
#define zis_object_meta_reset_gc_mark(meta) \
    do { (meta)._2 &= ~(uintptr_t)1U; } while (0)
/// Get object meta GC_MARK.
#define zis_object_meta_test_gc_mark(meta) \
    ((meta)._2 & (uintptr_t)1U)

/* ----- object basics ------------------------------------------------------ */

/// Common head of any object struct.
#define ZIS_OBJECT_HEAD \
    struct zis_object_meta _meta; \
// ^^^ ZIS_OBJECT_HEAD ^^^

/// Size of `ZIS_OBJECT_HEAD`.
#define ZIS_OBJECT_HEAD_SIZE sizeof(struct zis_object_meta)

/// Object. Instances of structs.
struct zis_object {

    /**
     * @struct zis_object
     * ## Object Layout
     *
     * ```
     * +--------+ ---
     * |        |  ^
     * |  META  | head   // META: Object metadata like type and GC info.
     * |        |  v
     * +--------+ ---
     * |        |  ^
     * | SLOTS  |  |
     * |(object |  |     // SLOTS: A vector of object pointers (or small integers).
     * | vector)|  |
     * |        |  |
     * +--------+ body
     * |        |  |
     * | BYTES  |  |
     * |(native |  |     // BYTES : Native data. Must NOT store objects here.
     * |   data)|  |
     * |        |  v
     * +--------+ ---
     * ```
     *
     * ## Extendable SLOTS and BYTES
     *
     * Usually, SLOTS and BYTES have fixed sizes, which are stored in the
     * associated type object (see `struct zis_type_obj`).   But any of them
     * can be extendable, in which case the sizes are stored at the beginning
     * of their storages.
     *
     * ```
     * [SLOTS]
     * +-----------+
     * |  <Int:N>  | SLOTS[0]    <=== Here is the total number of slots in SLOTS,
     * |-----------|                  which must be a small integer.
     * |  field-1  | SLOTS[1]
     * |-----------|
     * |    ...    |   ...
     * |-----------|
     * |field-(N-1)| SLOTS[N-1]
     * +-----------+
     *
     * [BYTES]
     * +-----------+
     * | size_t M  | BYTES[0:W-1]  <=== Here is the size (in bytes) of BYTES.
     * |-----------|                    Symbol `W`, the size of the size number,
     * |   data    |                    is `sizeof(size_t)`.
     * |    ...    | BYTES[W:M-1]
     * +-----------+
     * ```
     */

    ZIS_OBJECT_HEAD  // head: META
    char _body[];    // body: SLOTS & BYTES
};

/**
 * @struct zis_object
 * ## Small integer as object pointer
 *
 * A `struct zis_object *` variable does not always hold a pointer to an object.
 * If the LSB of a `struct zis_object *` variable `x` is `1`, then it actually holds
 * a small int, and its values is `(intptr_t)x >> 1`. See `zis_object_is_smallint()`.
 */

/// Cast an object struct pointer to `struct zis_object *`.
#define zis_object_from(obj_ptr) \
    ((struct zis_object *)(obj_ptr))

/// Cast a `struct zis_object *` to an object struct pointer.
#define zis_object_cast(obj_ptr, type) \
    ((type *)(obj_ptr))

/// Get type of an object. The `obj` must not be a small integer.
#define zis_object_type(obj) \
    (assert(!zis_object_is_smallint(obj)), zis_object_meta_get_type_ptr((obj)->_meta))

/// Get type of an object. The `obj` can be a small integer, in which case NULL is returned.
/// @warning This function may return NULL!
/// @note `zis_object_type()` should be used instead if `obj` is definitely not a small integer.
zis_static_force_inline struct zis_type_obj *zis_object_type_1(struct zis_object *obj) {
    if (zis_unlikely(zis_object_is_smallint(obj)))
        return NULL;
    return zis_object_type(obj);
}

/// Check the type of an object. The `obj` can be a small integer, in which case always returns false.
zis_static_force_inline bool zis_object_type_is(struct zis_object *obj, struct zis_type_obj *type) {
    if (zis_unlikely(zis_object_is_smallint(obj)))
        return false;
    return zis_object_type(obj) == type;
}

/// Get field in SLOTS by index. No bounds checking for the index.
#define zis_object_get_slot(obj, index) \
    (assert(!zis_object_is_smallint(obj)), ((struct zis_object **)(obj)->_body)[(index)])

/// Set field in SLOTS by index with write barrier (header file "objmem.h" required).
/// No bounds checking for the index.
#define zis_object_set_slot(obj, index, value) \
do {                                           \
    struct zis_object *const __obj = (obj);    \
    struct zis_object *const __val = (value);  \
    assert(!zis_object_is_smallint(obj));      \
    ((struct zis_object **)__obj->_body)[(index)] = __val; \
    zis_object_write_barrier(__obj, __val);    \
} while (0)                                    \
// ^^^ zis_object_set_slot() ^^^

/// Access BYTES.
#define zis_object_ref_bytes(obj, slot_cnt) \
    (assert(!zis_object_is_smallint(obj)), (obj)->_body + sizeof(void *) * (slot_cnt))

/* ----- object common methods ---------------------------------------------- */

struct zis_context;

/// Calculate the hash code of an object.
/// On success, stores the hash code to `*hash_code` and returns true.
/// On failure, stores the thrown object to REG-0 and returns false.
bool zis_object_hash(
    size_t *restrict hash_code,
    struct zis_context *z, struct zis_object *obj
);

/// Object comparison result.
enum zis_object_ordering {
    ZIS_OBJECT_IC = -2, ///< incomparable
    ZIS_OBJECT_LT = -1, ///< less than
    ZIS_OBJECT_EQ =  0, ///< equal to
    ZIS_OBJECT_GT =  1, ///< greater than
};

/// Compare two objects.
/// On success, returns `ZIS_OBJECT_LT`, `ZIS_OBJECT_EQ`, or `ZIS_OBJECT_GT`.
/// On failure, stores the thrown object to REG-0 and returns `ZIS_OBJECT_IC`.
enum zis_object_ordering zis_object_compare(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
);

/// Check whether two objects equal.
/// This operation never fails.
bool zis_object_equals(
    struct zis_context *z,
    struct zis_object *obj1, struct zis_object *obj2
);
