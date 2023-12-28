#include "mapobj.h"

#include <math.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

/* ----- hashmap bucket node ------------------------------------------------ */

struct zis_hashmap_bucket_node_obj *zis_hashmap_bucket_node_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2], size_t key_hash
) {
    struct zis_hashmap_bucket_node_obj *const self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Map_Node),
        struct zis_hashmap_bucket_node_obj
    );
    self->_next_node = zis_hashmap_bucket_node_obj_null();
    self->_key = regs[0];
    self->_value = regs[1];
    self->key_hash = key_hash;
    return self;
}

/// Get next node in the bucket node list, or `NULL` if this is the last one.
zis_static_force_inline struct zis_hashmap_bucket_node_obj *
zis_hashmap_bucket_node_obj_next_node(const struct zis_hashmap_bucket_node_obj *bn) {
    struct zis_object *const next = bn->_next_node;
    if (zis_hashmap_bucket_node_obj_is_null(next))
        return NULL;
    assert(!zis_object_is_smallint(next));
    return zis_object_cast(next, struct zis_hashmap_bucket_node_obj);
}

/// Get `n`-th node in the bucket node list, or `NULL` if there is no such node.
static struct zis_hashmap_bucket_node_obj *
zis_hashmap_bucket_node_obj_nth_node(struct zis_hashmap_bucket_node_obj *bn, size_t n) {
    while (n--) {
        struct zis_object *const next = bn->_next_node;
        if (zis_unlikely(zis_hashmap_bucket_node_obj_is_null(next)))
            return NULL;
        assert(!zis_object_is_smallint(next));
        bn = zis_object_cast(next, struct zis_hashmap_bucket_node_obj);
    }
    return bn;
}

ZIS_NATIVE_TYPE_DEF(
    Map_Node,
    struct zis_hashmap_bucket_node_obj,
    key_hash,
    NULL, NULL, NULL
);

/* ----- hashmap bucket operations ------------------------------------------ */

zis_hashmap_buckets_obj_t *zis_hashmap_buckets_obj_new(struct zis_context *z, size_t n) {
    zis_hashmap_buckets_obj_t *buckets = zis_array_slots_obj_new(z, NULL, n);
    for (size_t i = 0; i < n; i++)
        buckets->_data[i] = zis_hashmap_bucket_node_obj_null();
    return buckets;
}

/// Get number of buckets.
zis_static_force_inline size_t
zis_hashmap_buckets_length(const zis_hashmap_buckets_obj_t *mb) {
    return zis_array_slots_obj_length(mb);
}

/// Get bucket by hash. No bounds checking. Return NULL if the bucket is empty.
zis_static_force_inline struct zis_hashmap_bucket_node_obj *
zis_hashmap_buckets_get_bucket(const zis_hashmap_buckets_obj_t *mb, size_t key_hash) {
    const size_t bkt_count = zis_hashmap_buckets_length(mb);
    if (zis_unlikely(!bkt_count))
        return NULL;
    const size_t bkt_index = key_hash % bkt_count;
    struct zis_object *const node = zis_array_slots_obj_get(mb, bkt_index);
    if (zis_hashmap_bucket_node_obj_is_null(node))
        return NULL;
    return zis_object_cast(node, struct zis_hashmap_bucket_node_obj);
}

/// Find a bucket node by its key.
/// R = { [0] = buckets / out_node, [1] = key }.
/// Returns whether found.
static bool zis_hashmap_buckets_get_node_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2], size_t key_hash
) {
    // ~~ regs[0] = buckets, regs[1] = key ~~

    struct zis_hashmap_bucket_node_obj *node;
    {
        assert(zis_object_type(regs[0]) == z->globals->type_Array_Slots);
        struct zis_array_slots_obj *const buckets =
            zis_object_cast(regs[0], struct zis_array_slots_obj);
        node = zis_hashmap_buckets_get_bucket(buckets, key_hash);
    }

    // ~~ regs[0] = node, regs[1] = key ~~

    for (; node; node = zis_hashmap_bucket_node_obj_next_node(node)) {
        if (key_hash != node->key_hash)
            continue;
        regs[0] = zis_object_from(node);
        const bool eq = zis_object_equals(z, regs[1], node->_key);
        if (eq)
            return true;
        node = zis_object_cast(regs[0], struct zis_hashmap_bucket_node_obj);
    }

    return false; // not found
}

/// Add a bucket node without checking whether the key exists.
static void zis_hashmap_buckets_put_node(
    zis_hashmap_buckets_obj_t *buckets,
    struct zis_hashmap_bucket_node_obj *node
) {
    const size_t bkt_count = zis_hashmap_buckets_length(buckets);
    assert(bkt_count);
    const size_t bkt_index = node->key_hash % bkt_count;
    struct zis_object *const bkt = zis_array_slots_obj_get(buckets, bkt_index);
    if (zis_hashmap_bucket_node_obj_is_null(bkt)) {
        node->_next_node = zis_hashmap_bucket_node_obj_null();
    } else {
        node->_next_node = bkt;
        zis_object_write_barrier(node, bkt);
    }
    zis_array_slots_obj_set(buckets, bkt_index, zis_object_from(node));
}

/// Delete a bucket node.
/// R = { [0] = buckets, [1] = key }.
/// Returns whether found.
static bool zis_hashmap_buckets_del_node_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2],
    size_t key_hash
) {
    // ~~ regs[0] = buckets, regs[1] = key ~~

    for (size_t i = 0;; i++) {
        struct zis_array_slots_obj *buckets;
        struct zis_hashmap_bucket_node_obj *bkt_head_node, *node;

        assert(zis_object_type(regs[0]) == z->globals->type_Array_Slots);
        buckets = zis_object_cast(regs[0], struct zis_array_slots_obj);
        bkt_head_node = zis_hashmap_buckets_get_bucket(buckets, key_hash);
        node = zis_hashmap_bucket_node_obj_nth_node(bkt_head_node, i);

        if (!node)
            break;
        if (key_hash != node->key_hash)
            continue;
        const bool eq = zis_object_equals(z, regs[1], node->_key);
        if (!eq)
            continue;

        assert(zis_object_type(regs[0]) == z->globals->type_Array_Slots);
        buckets = zis_object_cast(regs[0], struct zis_array_slots_obj);
        bkt_head_node = zis_hashmap_buckets_get_bucket(buckets, key_hash);
        if (i == 0) {
            node = bkt_head_node;
            zis_array_slots_obj_set(buckets, key_hash, node->_next_node); // See `zis_hashmap_buckets_put_node()`.
        } else {
            struct zis_hashmap_bucket_node_obj * prev_node =
                zis_hashmap_bucket_node_obj_nth_node(bkt_head_node, i - 1);
            node = zis_hashmap_bucket_node_obj_next_node(prev_node);
            assert(node);
            prev_node->_next_node = node->_next_node;
            zis_object_write_barrier(prev_node, prev_node->_next_node);
        }
        return true;
    }

    return false; // not found
}

/// Delete all bucket nodes.
static void zis_hashmap_buckets_clear(zis_hashmap_buckets_obj_t *mb) {
    const size_t n = zis_hashmap_buckets_length(mb);
    for (size_t i = 0; i < n; i++)
        mb->_data[i] = zis_hashmap_bucket_node_obj_null();
}

/// Resize the bucket container.
/// R = { [0] = buckets, [1] = out_new_buckets, [2] = tmp }.
static void zis_hashmap_buckets_rehash_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 3],
    size_t new_length
) {
    assert(zis_object_type(regs[0]) == z->globals->type_Array_Slots);
    assert(new_length || !zis_hashmap_buckets_length(zis_object_cast(regs[0], zis_hashmap_buckets_obj_t)));

    // ~~ regs[0] = buckets, regs[1] = new_buckets, regs[2] = tmp ~~

    zis_hashmap_buckets_obj_t *new_mb = zis_hashmap_buckets_obj_new(z, new_length);
    regs[1] = zis_object_from(new_mb);

    zis_hashmap_buckets_foreach_node_r(regs[0], regs[2], node, {
        assert(zis_object_type(regs[1]) == z->globals->type_Array_Slots);
        new_mb = zis_object_cast(regs[1], zis_hashmap_buckets_obj_t);
        zis_hashmap_buckets_put_node(new_mb, node);
    });

    zis_hashmap_buckets_clear(zis_object_cast(regs[0], zis_hashmap_buckets_obj_t));
    regs[2] = zis_object_from(z->globals->val_nil);
}

/* ----- map object --------------------------------------------------------- */

zis_static_force_inline size_t
zis_map_obj_min_bkt_cnt(float load_factor, size_t min_node_cnt) {
    return (size_t)ceil((double)min_node_cnt / load_factor);
}

struct zis_map_obj *zis_map_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 1],
    float load_factor, size_t reserve
) {
    struct zis_map_obj *self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Map),
        struct zis_map_obj
    );
    regs[0] = zis_object_from(self);
    self->_buckets = z->globals->val_empty_array_slots;
    self->node_count = 0;
    self->node_count_threshold = 0;
    self->load_factor = load_factor > 0.0f ? load_factor : 0.9f;
    if (reserve) {
        zis_hashmap_buckets_obj_t *buckets =
            zis_hashmap_buckets_obj_new(z, zis_map_obj_min_bkt_cnt(self->load_factor, reserve));
        assert(zis_object_type(regs[0]) == z->globals->type_Map);
        self = zis_object_cast(regs[0], struct zis_map_obj);
        self->_buckets = buckets;
        self->node_count_threshold = reserve;
    }
    return self;
}

void zis_map_obj_rehash_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 4], size_t n_buckets
) {
    // ~~ regs[0] = map, regs[1] = buckets, regs[2] = new_buckets, regs[3] = tmp ~~

    assert(zis_object_type(regs[0]) == z->globals->type_Map);
    struct zis_map_obj *self = zis_object_cast(regs[0], struct zis_map_obj);
    regs[1] = zis_object_from(self->_buckets);
    zis_hashmap_buckets_rehash_r(z, regs + 1, n_buckets);

    assert(zis_object_type(regs[0]) == z->globals->type_Map);
    self = zis_object_cast(regs[0], struct zis_map_obj);
    assert(zis_object_type(regs[2]) == z->globals->type_Array_Slots);
    self->_buckets = zis_object_cast(regs[2], zis_hashmap_buckets_obj_t);
    zis_object_write_barrier(self, regs[2]);

    const double load_factor = self->load_factor;
    self->node_count_threshold = (size_t)((double)n_buckets * load_factor);
}

void zis_map_obj_reserve_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 4], size_t n
) {
    // ~~ regs[0] = map, regs[1] = tmp, regs[2] = tmp2, regs[3] = tmp3 ~~

    assert(zis_object_type(regs[0]) == z->globals->type_Map);
    struct zis_map_obj *self = zis_object_cast(regs[0], struct zis_map_obj);
    const size_t bkt_num = zis_hashmap_buckets_length(self->_buckets);
    assert(self->load_factor > 0.0f);
    const size_t bkt_num_min = zis_map_obj_min_bkt_cnt(self->load_factor, n);
    if (bkt_num < bkt_num_min) {
        zis_map_obj_rehash_r(z, regs, bkt_num_min);
        assert(zis_object_type(regs[0]) == z->globals->type_Map);
        assert(zis_object_cast(regs[0], struct zis_map_obj)->node_count_threshold >= n);
    }
}

void zis_map_obj_clear(struct zis_map_obj *self) {
    zis_hashmap_buckets_clear(self->_buckets);
}

int zis_map_obj_get_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]
) {
    // ~~ regs[0] = map, regs[1] = key ~~

    size_t key_hash;
    if (zis_unlikely(!zis_object_hash(&key_hash, z, regs[1])))
        return ZIS_THR;

    assert(zis_object_type(regs[0]) == z->globals->type_Map);
    regs[0] = zis_object_from(zis_object_cast(regs[0], struct zis_map_obj)->_buckets);

    // ~~ regs[0] = buckets ~~

    const bool ok = zis_hashmap_buckets_get_node_r(z, regs, key_hash);
    if (zis_unlikely(!ok))
        return ZIS_E_ARG;

    // ~~ regs[0] = node / out_value ~~

    assert(zis_object_type(regs[0]) == z->globals->type_Map_Node);
    regs[0] = zis_object_cast(regs[0], struct zis_hashmap_bucket_node_obj)->_value;
    return ZIS_OK;
}

int zis_map_obj_set_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 4]
) {
    // ~~ regs[0] = map, regs[1] = key, regs[2] = value, regs[3] = tmp ~~

    size_t key_hash;
    if (zis_unlikely(!zis_object_hash(&key_hash, z, regs[1])))
        return ZIS_THR;

    // ~~ regs[0] = buckets, regs[3] = map ~~

    regs[3] = regs[0];
    assert(zis_object_type(regs[0]) == z->globals->type_Map);
    regs[0] = zis_object_from(zis_object_cast(regs[0], struct zis_map_obj)->_buckets);
    const bool ok = zis_hashmap_buckets_get_node_r(z, regs, key_hash);

    // ~~ regs[0] = node, regs[1] = key, regs[2] = value, regs[3] = map ~~

    if (ok) { // Node exists. Do update.
        assert(zis_object_type(regs[0]) == z->globals->type_Map_Node);
        zis_object_cast(regs[0], struct zis_hashmap_bucket_node_obj)->_value = regs[2];
    } else { // Node does not exist. Insert a new one.
        assert(zis_object_type(regs[3]) == z->globals->type_Map);
        struct zis_map_obj *self = zis_object_cast(regs[3], struct zis_map_obj);
        const size_t orig_node_count = self->node_count;
        if (
            orig_node_count >= self->node_count_threshold && // too many nodes
            (zis_hashmap_buckets_get_bucket(self->_buckets, key_hash) || !orig_node_count) // hash collision
        ) {
            struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
            tmp_regs[0] = regs[3];
            zis_map_obj_reserve_r(z, tmp_regs, orig_node_count > 4 ? orig_node_count * 2 : 6);
            zis_callstack_frame_free_temp(z, 3);
        }
        struct zis_hashmap_bucket_node_obj *const node =
            zis_hashmap_bucket_node_obj_new_r(z, regs + 1, key_hash);
        assert(zis_object_type(regs[3]) == z->globals->type_Map);
        self = zis_object_cast(regs[3], struct zis_map_obj);
        assert(self->node_count == orig_node_count && orig_node_count < SIZE_MAX);
        self->node_count = orig_node_count + 1;
        zis_hashmap_buckets_put_node(self->_buckets, node);
    }
    return ZIS_OK;
}

int zis_map_obj_unset_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 3]
) {
    // ~~ regs[0] = map, regs[1] = key, regs[2] = tmp ~~

    size_t key_hash;
    if (zis_unlikely(!zis_object_hash(&key_hash, z, regs[1])))
        return ZIS_THR;

    // ~~ regs[0] = buckets, regs[1] = key, regs[2] = map ~~

    assert(zis_object_type(regs[0]) == z->globals->type_Map);
    regs[2] = regs[0];
    struct zis_map_obj *self = zis_object_cast(regs[0], struct zis_map_obj);
    regs[0] = zis_object_from(self->_buckets);

    if (zis_hashmap_buckets_del_node_r(z, regs, key_hash)) {
        assert(zis_object_type(regs[2]) == z->globals->type_Map);
        self = zis_object_cast(regs[2], struct zis_map_obj);
        assert(self->node_count);
        self->node_count--;
        return ZIS_OK;
    }

    return ZIS_E_ARG;
}

ZIS_NATIVE_TYPE_DEF(
    Map,
    struct zis_map_obj,
    node_count,
    NULL, NULL, NULL
);
