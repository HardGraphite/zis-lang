/// Object memory management.

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "algorithm.h" // zis_unreachable()
#include "attributes.h"
#include "object.h"
#include "smallint.h"

struct zis_context;
struct zis_object;
struct zis_type_obj;

/* ----- object GC states --------------------------------------------------- */

/// Object GC state.
enum zis_objmem_obj_state {

    /**
     * @enum zis_objmem_obj_state
     * ## Object States
     *
     * ```
     * new space   old space     big space
     * .........   ..........   ..........
     * . [NEW] .   .        .   .        .
     * .   |   .  +->[OLD]  .   .        .
     * .   v   . / .        .   .  [BIG] .
     * . [MID]--+  .        .   .        .
     * .........   ..........   ..........
     *
     * `-------'   `---------------------'
     *  young gen       old generation
     * ```
     */

    ZIS_OBJMEM_OBJ_NEW = 0, // 0b00
    ZIS_OBJMEM_OBJ_MID = 1, // 0b01
    ZIS_OBJMEM_OBJ_OLD = 2, // 0b10
    ZIS_OBJMEM_OBJ_BIG = 3, // 0b11
};

/// Check whether object is not young.
#define zis_object_meta_is_not_young(meta) \
    zis_object_meta_get_gc_state_bit1(meta)

/// Check whether object is young (NEW or MID).
#define zis_object_meta_is_young(meta) \
    (!zis_object_meta_is_not_young(meta))

/// Check whether young object is not NEW (aka MID).
#define zis_object_meta_young_is_not_new(meta) \
    (assert(zis_object_meta_is_young(meta)), zis_object_meta_get_gc_state_bit0(meta))

/// Check whether young object is NEW.
#define zis_object_meta_young_is_new(meta) \
    (!zis_object_meta_young_is_not_new(meta))

/// Check whether non-young object is BIG.
#define zis_object_meta_old_is_big(meta) \
    (assert(zis_object_meta_is_not_young(meta)), zis_object_meta_get_gc_state_bit0(meta))

/// Check whether non-young object is not BIG (aka OLD from MID).
#define zis_object_meta_old_is_not_big(meta) \
    (!zis_object_meta_old_is_big(meta))

/// Record an old object that stores an young object.
/// `parent_obj` must be in old generation and contains young object.
void zis_objmem_record_o2y_ref(struct zis_object *parent_obj);

/* ----- object write barrier ----------------------------------------------- */

/// Object write barrier. Place this after where a value is stored into an object.
#define zis_object_write_barrier(obj, val) \
do {                                       \
    struct zis_object *const __wb_obj = zis_object_from((obj)); \
    if (zis_likely(zis_object_meta_is_young(__wb_obj->_meta)))  \
        break;                             \
    struct zis_object *const __wb_val = zis_object_from((val)); \
    if (zis_object_is_smallint(__wb_val) || zis_object_meta_is_not_young(__wb_val->_meta)) \
        break;                             \
    zis_objmem_record_o2y_ref(__wb_obj);   \
} while (0)                                \
// ^^^ zis_object_write_barrier() ^^^

/// Object write barrier for an array of values. See `zis_object_write_barrier()`.
#define zis_object_write_barrier_n(obj, val_arr, var_arr_len) \
do {                                                          \
    struct zis_object *const __wb_obj = zis_object_from((obj)); \
    if (zis_likely(zis_object_meta_is_young(__wb_obj->_meta)))\
        break;                                                \
    _zis_object_write_barrier_n(__wb_obj, val_arr, var_arr_len);\
} while (0)                                                   \
// ^^^ zis_object_write_barrier_n() ^^^
void _zis_object_write_barrier_n(struct zis_object *, struct zis_object *[], size_t);

/// Assert that no write barrier is needed.
#define zis_object_assert_no_write_barrier(__obj) \
    assert(zis_object_meta_is_young((__obj)->_meta))

/// Assert that no write barrier is needed.
#define zis_object_assert_no_write_barrier_2(__obj, __val) \
    assert(                                                \
        zis_object_meta_is_young((__obj)->_meta) ||        \
        zis_object_is_smallint((__val)) ||                 \
        zis_object_meta_is_not_young((__val)->_meta)       \
    )                                                      \
// ^^^ zis_object_assert_no_write_barrier_2() ^^^

/* ----- object memory context ---------------------------------------------- */

/// Context of object memory management.
struct zis_objmem_context;

/// Create a memory context.
struct zis_objmem_context *zis_objmem_context_create(void);

/// Destroy a memory context. (All objects will be finalized.)
void zis_objmem_context_destroy(struct zis_objmem_context *ctx);

/// Print object memory usage to a file or to stderr (`NULL`).
/// Only available when compile with `ZIS_DEBUG` being true.
void zis_objmem_print_usage(struct zis_objmem_context *ctx, void *FILE_ptr);

/* ----- object allocation -------------------------------------------------- */

/// Memory allocation options.
enum zis_objmem_alloc_type {
    ZIS_OBJMEM_ALLOC_AUTO, ///< Decide automatically.
    ZIS_OBJMEM_ALLOC_SURV, ///< Assume the object has survived from a few GCs.
    ZIS_OBJMEM_ALLOC_HUGE, ///< Treat object as a large object.
};

/// Allocate memory for an object. Only the head of the object is initialized.
/// This function may call `zis_objmem_gc()` if necessary.
struct zis_object *zis_objmem_alloc(
    struct zis_context *z, struct zis_type_obj *obj_type
);

/// Allocate object memory like `zis_objmem_alloc()`, but provides more options.
/// Type objects must be allocated with type `ZIS_OBJMEM_ALLOC_SURV`.
/// Params `ext_slots` (count) and `ext_bytes` (size) are for extendable objects;
/// `ext_bytes` is always rounded up to a multiplication of `sizeof(void*)` inside.
struct zis_object *zis_objmem_alloc_ex(
    struct zis_context *z, enum zis_objmem_alloc_type alloc_type,
    struct zis_type_obj *obj_type, size_t ext_slots, size_t ext_bytes
);

/* ----- garbage collection ------------------------------------------------- */

/// GC options.
enum zis_objmem_gc_type {
    ZIS_OBJMEM_GC_NONE = -1,
    ZIS_OBJMEM_GC_AUTO,
    ZIS_OBJMEM_GC_FAST,
    ZIS_OBJMEM_GC_FULL,
};

/// Run garbage collection.
int zis_objmem_gc(struct zis_context *z, enum zis_objmem_gc_type type);

/// Get current GC type. Returning `ZIS_OBJMEM_GC_NONE` means GC is not running.
enum zis_objmem_gc_type zis_objmem_current_gc(struct zis_context *z);

/* ----- GC roots and weak-ref containers ----------------------------------- */

/// See `zis_objmem_object_visitor_t`.
enum zis_objmem_obj_visit_op {
    ZIS_OBJMEM_OBJ_VISIT_MARK, ///< mark reachable object and its slots recursively
    ZIS_OBJMEM_OBJ_VISIT_MARK_Y, ///< mark reachable young object and its slots recursively
    ZIS_OBJMEM_OBJ_VISIT_MOVE, ///< update reference to moved object
};

/// GC: object scanning function used by a GC root. Visit each object in the
/// GC root with macro `zis_objmem_visit_object()` or function `zis_objmem_visit_object_vec()`.
typedef void (*zis_objmem_object_visitor_t)(void *, enum zis_objmem_obj_visit_op);

/// GC: visit an object in a GC root. Parameter `obj` must be an object in the root,
/// and be the variable itself, so that it is assignable and can be updated correctly.
/// If `obj` is a small integer, it will be ignored.
/// Usually used in a `zis_objmem_object_visitor_t` function.
#define zis_objmem_visit_object(obj, op) \
do {                                     \
    if (zis_unlikely(zis_object_is_smallint(zis_object_from(obj)))) \
        break;                           \
    if (op == ZIS_OBJMEM_OBJ_VISIT_MARK_Y)                          \
        _zis_objmem_mark_object_rec_y_((struct zis_object *)(obj)); \
    else if (op == ZIS_OBJMEM_OBJ_VISIT_MOVE)                       \
        _zis_objmem_move_object_((struct zis_object **)&(obj));     \
    else if (op == ZIS_OBJMEM_OBJ_VISIT_MARK)                       \
        _zis_objmem_mark_object_rec_x_((struct zis_object *)(obj)); \
    else                                 \
        zis_unreachable();               \
} while (0)                              \
// ^^^ zis_objmem_visit_object() ^^^

/// Apply `zis_objmem_visit_object()` to a vector of objects in range `[begin, end)`.
void zis_objmem_visit_object_vec(
    struct zis_object **begin, struct zis_object **end,
    enum zis_objmem_obj_visit_op op
);

/// Add a GC root.
void zis_objmem_add_gc_root(
    struct zis_context *z, void *root, zis_objmem_object_visitor_t fn
);

/// Remove a GC root added with `zis_objmem_add_gc_root()`.
bool zis_objmem_remove_gc_root(struct zis_context *z, void *root);

/// See `zis_objmem_weak_refs_visitor_t`.
enum zis_objmem_weak_ref_visit_op {
    ZIS_OBJMEM_WEAK_REF_VISIT_FINI, ///< finalize reference
    ZIS_OBJMEM_WEAK_REF_VISIT_FINI_Y, ///< finalize reference to young object
    ZIS_OBJMEM_WEAK_REF_VISIT_MOVE, ///< update reference to moved object
};

/// GC: weak reference container scanning functions. Visit each object in the
/// container with macro `zis_objmem_visit_weak_ref()`.
typedef void (*zis_objmem_weak_refs_visitor_t)(void *, enum zis_objmem_weak_ref_visit_op);

/// GC: visit a weak reference in its container. Parameter `obj` must be a reference,
/// and be the variable itself, so that it is assignable and can be updated correctly.
/// To use this macro, a function-like macro called `WEAK_REF_FINI()` must be defined,
/// which takes an argument that is the object (reference) to finalize.
/// Usually used in a Usually used in a `zis_objmem_weak_refs_visitor_t` function.
#define zis_objmem_visit_weak_ref(obj, op) \
do {                                       \
    assert(!zis_object_is_smallint(zis_object_from(obj))); \
    if (op != ZIS_OBJMEM_WEAK_REF_VISIT_MOVE) {            \
        assert(op == ZIS_OBJMEM_WEAK_REF_VISIT_FINI || op == ZIS_OBJMEM_WEAK_REF_VISIT_FINI_Y); \
        if (op == ZIS_OBJMEM_WEAK_REF_VISIT_FINI_Y && zis_object_meta_is_not_young(obj->_meta)) \
            break;                         \
        if (!zis_object_meta_test_gc_mark(obj->_meta))     \
            WEAK_REF_FINI( (obj) );        \
    } else {                               \
        _zis_objmem_move_object_((struct zis_object **)&(obj));                                 \
    }                                      \
} while (0)                                \
// ^^^ zis_objmem_visit_weak_ref() ^^^

/// Record a weak reference container.
void zis_objmem_register_weak_ref_collection(
    struct zis_context *z,
    void *ref_container, zis_objmem_weak_refs_visitor_t fn
);

/// Remove a weak reference container record.
bool zis_objmem_unregister_weak_ref_collection(struct zis_context *z, void *ref_container);

/* -------------------------------------------------------------------------- */

zis_noinline void _zis_objmem_mark_object_slots_rec_x(struct zis_object *);
zis_noinline void _zis_objmem_mark_object_slots_rec_y(struct zis_object *);
zis_noinline void _zis_objmem_mark_object_slots_rec_o2x(struct zis_object *);
zis_noinline void _zis_objmem_mark_object_slots_rec_o2y(struct zis_object *);
zis_noinline void _zis_objmem_move_object_slots(struct zis_object *);

// See `_zis_objmem_mark_object_rec_x()`.
#define _zis_objmem_mark_object_rec_x_(obj) \
do {                                        \
    struct zis_object *__obj = (obj);       \
    if (zis_object_meta_test_gc_mark(__obj->_meta)) \
        break;                              \
    zis_object_meta_set_gc_mark(__obj->_meta);      \
    if (zis_object_meta_get_gc_state(__obj->_meta) == ZIS_OBJMEM_OBJ_NEW) \
        _zis_objmem_mark_object_slots_rec_x(__obj); \
    else                                    \
        _zis_objmem_mark_object_slots_rec_o2x(__obj); \
} while (0)                                 \
// ^^^ _zis_objmem_mark_object_rec_x_() ^^^

// See `_zis_objmem_mark_object_rec_y()`.
#define _zis_objmem_mark_object_rec_y_(obj) \
do {                                        \
    struct zis_object *__obj = (obj);       \
    if (zis_object_meta_is_not_young(__obj->_meta) || zis_object_meta_test_gc_mark(__obj->_meta)) \
        break;                              \
    zis_object_meta_set_gc_mark(__obj->_meta);      \
    if (zis_object_meta_young_is_new(__obj->_meta)) \
        _zis_objmem_mark_object_slots_rec_y(__obj); \
    else                                    \
        _zis_objmem_mark_object_slots_rec_o2y(__obj); \
} while (0)                                 \
// ^^^ _zis_objmem_mark_object_rec_y_() ^^^

// See `_zis_objmem_move_object()`.
#define _zis_objmem_move_object_(obj_ref) \
do {                                      \
    struct zis_object *__obj = *(obj_ref);\
    if (!zis_object_meta_test_gc_mark(__obj->_meta)) \
        break;                            \
    *(obj_ref) = zis_object_meta_get_gc_ptr(__obj->_meta, struct zis_object *); \
} while (0)                               \
// ^^^ _zis_objmem_move_object_()^^^


