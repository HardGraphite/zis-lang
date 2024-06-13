/// The `Map` type.

#pragma once

#include "attributes.h"
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
struct zis_map_obj *zis_map_obj_new(
    struct zis_context *z,
    float load_factor, size_t reserve
);

/// Combine a vector of maps. On failure, returns NULL (throw REG-0).
struct zis_map_obj *zis_map_obj_combine(
    struct zis_context *z,
    struct zis_map_obj *v[], size_t n
);

/// Get number of elements.
zis_static_force_inline size_t zis_map_obj_length(const struct zis_map_obj *self) {
    return self->node_count;
}

/// Rehash.
void zis_map_obj_rehash(
    struct zis_context *z,
    struct zis_map_obj *self, size_t n_buckets
);

/// Reserve buckets for more elements.
void zis_map_obj_reserve(
    struct zis_context *z,
    struct zis_map_obj *self, size_t n
);

/// Delete all elements.
void zis_map_obj_clear(struct zis_map_obj *self);

/// Get value in the map by key. `out_value` is optional.
/// Returns `ZIS_OK`, `ZIS_THR` (throw REG-0), or `ZIS_E_ARG` (not found).
int zis_map_obj_get(
    struct zis_context *z,
    struct zis_map_obj *self, struct zis_object *key,
    struct zis_object ** out_value /* = NULL */
);

/// Add or update value in the map by key.
/// Returns `ZIS_OK` or `ZIS_THR` (throw REG-0).
int zis_map_obj_set(
    struct zis_context *z,
    struct zis_map_obj *self, struct zis_object *key, struct zis_object *value
);

/// Delete an element in the map.
/// Returns `ZIS_OK`, `ZIS_THR` (throw REG-0), or `ZIS_E_ARG` (not found).
int zis_map_obj_unset(
    struct zis_context *z,
    struct zis_map_obj *self, struct zis_object *key
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

/// Visit each key-value pair.
int zis_map_obj_foreach(
    struct zis_context *z, struct zis_map_obj *self,
    int (*fn)(struct zis_object *key, struct zis_object *val, void *arg), void *fn_arg
);

/// Find a key by its associated value.
/// Returns NULL if not found.
struct zis_object *zis_map_obj_reverse_lookup(
    struct zis_context *z, struct zis_map_obj *self,
    struct zis_object *value
);
