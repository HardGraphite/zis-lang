#include "mapobj.h"

#include <math.h>

#include "context.h"
#include "globals.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"

#include "symbolobj.h"

/* ----- hashmap bucket node ------------------------------------------------ */

struct hashmap_bucket_node_obj_new_locals {
    struct zis_object *key, *value;
};

static struct zis_hashmap_bucket_node_obj *zis_hashmap_bucket_node_obj_new(
    struct zis_context *z,
    struct hashmap_bucket_node_obj_new_locals *locals, size_t key_hash
) {
    struct zis_hashmap_bucket_node_obj *const self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Map_Node),
        struct zis_hashmap_bucket_node_obj
    );
    self->_next_node = zis_hashmap_bucket_node_obj_null();
    self->_key = locals->key;
    self->_value = locals->value;
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

struct hashmap_buckets_get_node_locals {
    union {
        zis_hashmap_buckets_obj_t *buckets;
        struct zis_hashmap_bucket_node_obj *node;
    };
    struct zis_object *key;
};

/// Find a bucket node by its key.
/// Returns NULL if not found.
static struct zis_hashmap_bucket_node_obj *zis_hashmap_buckets_get_node(
    struct zis_context *z,
    struct hashmap_buckets_get_node_locals *locals, size_t key_hash
) {
    assert(zis_object_type_is(zis_object_from(locals->buckets), z->globals->type_Array_Slots));
    struct zis_hashmap_bucket_node_obj *node =
        zis_hashmap_buckets_get_bucket(locals->buckets, key_hash);

    for (; node; node = zis_hashmap_bucket_node_obj_next_node(node)) {
        if (key_hash != node->key_hash)
            continue;
        locals->node = node;
        const bool eq = zis_object_equals(z, locals->key, node->_key);
        node = locals->node;
        if (eq)
            return node;
    }

    return NULL; // not found
}

/// Find a bucket node by a Symbol key. Returns NULL if not found.
zis_static_force_inline struct zis_hashmap_bucket_node_obj *
zis_hashmap_buckets_sym_get_node(
    const zis_hashmap_buckets_obj_t *mb, struct zis_symbol_obj *key
) {
    // See `zis_hashmap_buckets_get_node()`.

    const size_t key_hash = zis_symbol_obj_hash(key);

    for (
        struct zis_hashmap_bucket_node_obj *node
            = zis_hashmap_buckets_get_bucket(mb, key_hash);
        node;
        node = zis_hashmap_bucket_node_obj_next_node(node)
    ) {
        if (key_hash == node->key_hash && zis_object_from(key) == node->_key)
            return node;
    }

    return NULL; // Not found.
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

struct hashmap_buckets_del_node_locals {
    zis_hashmap_buckets_obj_t *buckets;
    struct zis_object *key;
};

/// Delete a bucket node.
/// Returns whether found.
static bool zis_hashmap_buckets_del_node(
    struct zis_context *z,
    struct hashmap_buckets_del_node_locals *locals,
    size_t key_hash
) {
    for (size_t i = 0;; i++) {
        struct zis_hashmap_bucket_node_obj *bkt_head_node, *node;
        bkt_head_node = zis_hashmap_buckets_get_bucket(locals->buckets, key_hash);
        node = zis_hashmap_bucket_node_obj_nth_node(bkt_head_node, i);

        if (!node)
            break;
        if (key_hash != node->key_hash)
            continue;
        const bool eq = zis_object_equals(z, locals->key, node->_key);
        if (!eq)
            continue;

        bkt_head_node = zis_hashmap_buckets_get_bucket(locals->buckets, key_hash);
        if (i == 0) {
            node = bkt_head_node;
            zis_array_slots_obj_set(locals->buckets, key_hash, node->_next_node); // See `zis_hashmap_buckets_put_node()`.
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

/* ----- map object --------------------------------------------------------- */

zis_static_force_inline size_t
zis_map_obj_min_bkt_cnt(float load_factor, size_t min_node_cnt) {
    return (size_t)ceil((double)min_node_cnt / load_factor);
}

struct zis_map_obj *zis_map_obj_new(
    struct zis_context *z,
    float load_factor, size_t reserve
) {
    struct zis_map_obj *self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Map),
        struct zis_map_obj
    );
    self->_buckets = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier_2(self, zis_object_from(self->_buckets));
    self->node_count = 0;
    self->node_count_threshold = 0;
    self->load_factor = load_factor > 0.0f ? load_factor : 0.9f;
    if (reserve) {
        zis_locals_decl_1(z, var, struct zis_map_obj *self);
        var.self = self;
        zis_hashmap_buckets_obj_t *buckets =
            zis_hashmap_buckets_obj_new(z, zis_map_obj_min_bkt_cnt(self->load_factor, reserve));
        var.self->_buckets = buckets;
        zis_object_write_barrier(var.self, buckets);
        var.self->node_count_threshold = reserve;
        zis_locals_drop(z, var);
        return var.self;
    }
    return self;
}

void zis_map_obj_rehash(
    struct zis_context *z,
    struct zis_map_obj *_self, size_t n_buckets
) {
    assert(n_buckets || !zis_hashmap_buckets_length(_self->_buckets));

    zis_locals_decl(
        z, var,
        struct zis_map_obj *self;
        zis_hashmap_buckets_obj_t *buckets;
        zis_hashmap_buckets_obj_t *new_buckets;
        struct zis_object *temp;
    );
    var.self = _self;
    var.buckets = _self->_buckets;
    var.temp = zis_smallint_to_ptr(0);
    var.new_buckets = zis_hashmap_buckets_obj_new(z, n_buckets);

    zis_hashmap_buckets_foreach_node_r(var.buckets, var.temp, node, {
        zis_hashmap_buckets_put_node(var.new_buckets, node);
    });
    zis_hashmap_buckets_clear(var.buckets);

    var.self->_buckets = var.new_buckets;
    zis_object_write_barrier(var.self, var.new_buckets);

    const double load_factor = var.self->load_factor;
    var.self->node_count_threshold = (size_t)((double)n_buckets * load_factor);

    zis_locals_drop(z, var);
}

void zis_map_obj_reserve(
    struct zis_context *z,
    struct zis_map_obj *self, size_t n
) {
    const size_t bkt_num = zis_hashmap_buckets_length(self->_buckets);
    assert(self->load_factor > 0.0f);
    const size_t bkt_num_min = zis_map_obj_min_bkt_cnt(self->load_factor, n);
    if (bkt_num < bkt_num_min)
        zis_map_obj_rehash(z, self, bkt_num_min);
}

void zis_map_obj_clear(struct zis_map_obj *self) {
    zis_hashmap_buckets_clear(self->_buckets);
    self->node_count = 0;
}

int zis_map_obj_get(
    struct zis_context *z,
    struct zis_map_obj *_self, struct zis_object *_key,
    struct zis_object ** out_value /* = NULL */
) {
    zis_locals_decl(
        z, var,
        struct zis_map_obj *self;
        struct hashmap_buckets_get_node_locals l_gn;
    );
    var.self = _self;
    var.l_gn.buckets = _self->_buckets;
    var.l_gn.key = _key;

    size_t key_hash;
    if (zis_unlikely(!zis_object_hash(&key_hash, z, var.l_gn.key))) {
        zis_locals_drop(z, var);
        return ZIS_THR;
    }

    struct zis_hashmap_bucket_node_obj *node =
        zis_hashmap_buckets_get_node(z, &var.l_gn, key_hash);
    if (zis_unlikely(!node)) {
        zis_locals_drop(z, var);
        return ZIS_E_ARG;
    }

    if (out_value)
        *out_value = node->_value;

    zis_locals_drop(z, var);
    return ZIS_OK;
}

int zis_map_obj_set(
    struct zis_context *z,
    struct zis_map_obj *_self, struct zis_object *_key, struct zis_object *_value
) {
    zis_locals_decl(
        z, var,
        struct zis_map_obj *self;
        struct hashmap_buckets_get_node_locals l_gn;
        struct hashmap_bucket_node_obj_new_locals l_nn;
    );
    var.self = _self;
    var.l_gn.buckets = _self->_buckets;
    var.l_gn.key = _key;
    var.l_nn.key = _key;
    var.l_nn.value = _value;

    size_t key_hash;
    if (zis_unlikely(!zis_object_hash(&key_hash, z, var.l_gn.key))) {
        zis_locals_drop(z, var);
        return ZIS_THR;
    }

    struct zis_hashmap_bucket_node_obj *node =
        zis_hashmap_buckets_get_node(z, &var.l_gn, key_hash);

    if (node) { // Node exists. Do update.
        node->_value = var.l_nn.value;
        zis_object_write_barrier(node, var.l_nn.value);
    } else { // Node does not exist. Insert a new one.
        const size_t orig_node_count = var.self->node_count;
        if (
            orig_node_count >= var.self->node_count_threshold && // too many nodes
            (zis_hashmap_buckets_get_bucket(var.l_gn.buckets, key_hash) || !orig_node_count) // hash collision
        ) {
            zis_map_obj_reserve(z, var.self, orig_node_count > 4 ? orig_node_count * 2 : 6);
            var.l_gn.buckets = var.self->_buckets;
        }
        struct zis_hashmap_bucket_node_obj *const new_node =
            zis_hashmap_bucket_node_obj_new(z, &var.l_nn, key_hash);
        assert(var.self->node_count == orig_node_count && orig_node_count < SIZE_MAX);
        zis_hashmap_buckets_put_node(var.l_gn.buckets, new_node);
        var.self->node_count = orig_node_count + 1;
    }

    zis_locals_drop(z, var);
    return ZIS_OK;
}

int zis_map_obj_unset(
    struct zis_context *z,
    struct zis_map_obj *_self, struct zis_object *_key
) {
    zis_locals_decl(
        z, var,
        struct zis_map_obj *self;
        struct hashmap_buckets_del_node_locals l_dn;
    );
    var.self = _self;
    var.l_dn.buckets = _self->_buckets;
    var.l_dn.key = _key;

    size_t key_hash;
    if (zis_unlikely(!zis_object_hash(&key_hash, z, var.l_dn.key))) {
        zis_locals_drop(z, var);
        return ZIS_THR;
    }

    if (!zis_hashmap_buckets_del_node(z, &var.l_dn, key_hash)) {
        zis_locals_drop(z, var);
        return ZIS_E_ARG;
    }

    assert(var.self->node_count);
    var.self->node_count--;

    zis_locals_drop(z, var);
    return ZIS_OK;
}

struct zis_object *zis_map_obj_sym_get(
    struct zis_map_obj *self,
    struct zis_symbol_obj *key
) {
    struct zis_hashmap_bucket_node_obj *const node =
        zis_hashmap_buckets_sym_get_node(self->_buckets, key);
    if (zis_likely(node))
        return node->_value;
    return NULL;
}

void zis_map_obj_sym_set(
    struct zis_context *z, struct zis_map_obj *self,
    struct zis_symbol_obj *key, struct zis_object *value
) {
    struct zis_hashmap_bucket_node_obj *const node =
        zis_hashmap_buckets_sym_get_node(self->_buckets, key);
    if (zis_likely(node)) {
        node->_value = value;
        zis_object_write_barrier(node, value);
        return;
    }

    const int status = zis_map_obj_set(z, self, zis_object_from(key), value);
    assert(status == ZIS_OK), zis_unused_var(status);
}

int zis_map_obj_foreach(
    struct zis_context *z, struct zis_map_obj *_self,
    int (*fn)(struct zis_object *key, struct zis_object *val, void *arg), void *fn_arg
) {
    zis_locals_decl(
        z, var,
        zis_hashmap_buckets_obj_t *buckets;
        struct zis_object *temp;
    );
    var.buckets = _self->_buckets;
    var.temp = zis_smallint_to_ptr(0);

    int fn_ret = 0;
    zis_hashmap_buckets_foreach_node_r(var.buckets, var.temp, node, {
        fn_ret = fn(node->_key, node->_value, fn_arg);
        if (fn_ret)
            goto break_loop;
    });
break_loop:
    zis_locals_drop(z, var);
    return fn_ret;
}

struct _reverse_lookup_state {
    struct zis_context *z;
    struct zis_object *value;
    struct zis_object *found_key;
};

static int _reverse_lookup_fn(struct zis_object *_key, struct zis_object *_val, void *_arg) {
    struct _reverse_lookup_state *const state = _arg;
    if (_val == state->value) {
        state->found_key = _key;
        return 1;
    }
    return 0;
}

struct zis_object *zis_map_obj_reverse_lookup(
    struct zis_context *z, struct zis_map_obj *self,
    struct zis_object *value
) {
    struct _reverse_lookup_state state = { .z = z, .value = value, .found_key = NULL };
    if (zis_map_obj_foreach(z, self, _reverse_lookup_fn, &state))
        return state.found_key;
    return NULL;
}

ZIS_NATIVE_TYPE_DEF(
    Map,
    struct zis_map_obj,
    node_count,
    NULL, NULL, NULL
);
