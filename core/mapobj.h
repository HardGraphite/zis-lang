/// The `Map` type.

#pragma once

#include "attributes.h"
#include "compat.h"
#include "object.h"
#include "objmem.h" // zis_object_write_barrier()

#include "arrayobj.h"

struct zis_context;
struct zis_symbol_obj;

/* ----- hashmap bucket node ------------------------------------------------ */

/// `Map.BucketNode` object.
struct zis_hashmap_bucket_node_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *_next_node;
    struct zis_object *_key;
    struct zis_object *_value;
    // --- BYTES ---
    size_t key_hash;
};

#define zis_hashmap_bucket_node_obj_null() \
    (zis_smallint_to_ptr(0))

#define zis_hashmap_bucket_node_obj_is_null(node_obj) \
    ((node_obj) == zis_hashmap_bucket_node_obj_null())

/// Create a hashmap bucket node.
/// R = { [0] = key, [1] = value }.
struct zis_hashmap_bucket_node_obj *zis_hashmap_bucket_node_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2], size_t key_hash
);

/* ----- hashmap bucket operations ------------------------------------------ */

/// Map buckets container type.
typedef struct zis_array_slots_obj zis_hashmap_buckets_obj_t;

/// Create a bucket container.
zis_hashmap_buckets_obj_t *zis_hashmap_buckets_obj_new(struct zis_context *z, size_t n);

/// Iterate over bucket nodes.
#define zis_hashmap_buckets_foreach_node_r(reg_buckets, reg_tmp, node_var, stmt) \
do {                                                                             \
    zis_hashmap_buckets_obj_t *__bkt_arr =                                       \
        zis_object_cast((reg_buckets), struct zis_array_slots_obj);              \
    const size_t __bkt_num = zis_array_slots_obj_length(__bkt_arr);              \
    for (size_t __bkt_i = 0; __bkt_i < __bkt_num; __bkt_i ++) {                  \
        __bkt_arr = zis_object_cast((reg_buckets), struct zis_array_slots_obj);  \
        struct zis_object *__node_obj = zis_array_slots_obj_get(__bkt_arr, __bkt_i); \
    __loop_bkt_nodes:                                                            \
        { /* Avoid using a breakable block here. */                              \
            if (zis_hashmap_bucket_node_obj_is_null(__node_obj))                 \
                goto __loop_bkt_nodes_end;                                       \
            struct zis_hashmap_bucket_node_obj *const (node_var) =               \
                zis_object_cast(__node_obj, struct zis_hashmap_bucket_node_obj); \
            reg_tmp = (node_var)->_next_node;                                    \
            { stmt }                                                             \
            __node_obj = reg_tmp;                                                \
            goto __loop_bkt_nodes;                                               \
        }                                                                        \
    __loop_bkt_nodes_end:;                                                       \
    }                                                                            \
} while (0)                                                                      \
// ^^^ zis_hashmap_buckets_foreach_node() ^^^

/* ----- map object --------------------------------------------------------- */

/// `Map` object. Hash map.
struct zis_map_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_array_slots_obj *_buckets;
    // --- BYTES ---
    size_t node_count;
    size_t node_count_threshold;
    float  load_factor; // node_count_threshold / bucket_count
};

/// Create an empty `Map`. Assign `load_factor = 0.0f` to use default load factor.
/// R = { [0] = out_map }.
struct zis_map_obj *zis_map_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 1],
    float load_factor, size_t reserve
);

/// Get number of elements.
zis_static_force_inline size_t zis_map_obj_length(const struct zis_map_obj *self) {
    return self->node_count;
}

/// Rehash.
/// R = { [0] = map, [1] = tmp, [2] = tmp2, [3] = tmp3 }.
void zis_map_obj_rehash_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 4], size_t n_buckets
);

/// Reserve buckets for more elements.
/// R = { [0] = map, [1] = tmp, [2] = tmp2, [3] = tmp3 }.
void zis_map_obj_reserve_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 4], size_t n
);

/// Delete all elements.
void zis_map_obj_clear(struct zis_map_obj *self);

/// Get value in the map by key.
/// R = { [0] = map / out_value, [1] = key }.
/// Returns `ZIS_OK`, `ZIS_THR` (throw REG-0), or `ZIS_E_ARG` (not found).
int zis_map_obj_get_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]
);

/// Add or update value in the map by key.
/// R = { [0] = map, [1] = key, [2] = value, [3] = tmp }.
/// Returns `ZIS_OK` or `ZIS_THR` (throw REG-0).
int zis_map_obj_set_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 4]
);

/// Delete an element in the map.
/// R = { [0] = map, [1] = key, [2] = tmp }.
/// Returns `ZIS_OK`, `ZIS_THR` (throw REG-0), or `ZIS_E_ARG` (not found).
int zis_map_obj_unset_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 3]
);

/// Get value by a symbol key. Return NULL if not found.
struct zis_object *zis_map_obj_sym_get(
    struct zis_map_obj *self,
    struct zis_symbol_obj *key
);

/// Set value by a symbol key.
void zis_map_obj_sym_set(
    struct zis_context *z, struct zis_map_obj *self,
    struct zis_symbol_obj *key, struct zis_object *value
);
