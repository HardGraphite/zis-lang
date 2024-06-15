#include "symbolobj.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "algorithm.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "platform.h"
#include "stack.h"

#include "exceptobj.h"
#include "stringobj.h"

/* ----- symbol ------------------------------------------------------------- */

#define SYM_OBJ_BYTES_FIXED_SIZE \
    (ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_symbol_obj, _bytes_size))

/// Create a `String` object from UTF-8 string `s`.
static struct zis_symbol_obj *zis_symbol_obj_new(
    struct zis_context *z,
    const char *s, size_t n
) {
    struct zis_symbol_obj *const self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Symbol,
            0U, SYM_OBJ_BYTES_FIXED_SIZE + n
        ),
        struct zis_symbol_obj
    );

    self->_registry_next = NULL;
    self->hash = zis_hash_bytes(s, n);

    assert(self->_bytes_size >= SYM_OBJ_BYTES_FIXED_SIZE + n);
#if ZIS_WORDSIZE == 64
    *(uint64_t *)(self->data + (self->_bytes_size - SYM_OBJ_BYTES_FIXED_SIZE - 8)) = 0U;
#else
    static_assert(ZIS_WORDSIZE <= 32);
    *(uint32_t *)(self->data + (self->_bytes_size - SYM_OBJ_BYTES_FIXED_SIZE - 4)) = 0U;
#endif
    memcpy(self->data, s, n);
    assert(zis_symbol_obj_data_size(self) == n);

    return self;
}

size_t zis_symbol_obj_data_size(const struct zis_symbol_obj *self) {
    size_t n = self->_bytes_size - SYM_OBJ_BYTES_FIXED_SIZE;
    if (zis_unlikely(!n))
        return 0;
    assert(n >= ZIS_WORDSIZE / 8);
    const char *p =
#if ZIS_WORDSIZE == 64
    memchr(self->data + (n - 8), 0, 8);
#else
    static_assert(ZIS_WORDSIZE <= 32);
    memchr(self->data + (n - 4), 0, 4);
#endif
    return p ? (size_t)(p - self->data) : n;
}

#define assert_arg1_Symbol(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Symbol)))

ZIS_NATIVE_FUNC_DEF(T_Symbol_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Symbol:\'=='(other :: Symbol) :: Bool
    Operator ==. */
    assert_arg1_Symbol(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const bool result = frame[1] == frame[2];
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Symbol_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Symbol:\'<=>'(other :: Symbol) :: Int
    Operator <=>. */
    assert_arg1_Symbol(z);
    struct zis_object **frame = z->callstack->frame;
    int result;
    if (frame[1] == frame[2]) {
        result = 0;
    } else if (!zis_object_type_is(frame[2], z->globals->type_Symbol)) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "<=>", frame[1], frame[2]
        ));
        return ZIS_THR;
    } else {
        struct zis_symbol_obj *lhs = zis_object_cast(frame[1], struct zis_symbol_obj);
        struct zis_symbol_obj *rhs = zis_object_cast(frame[2], struct zis_symbol_obj);
        const size_t lhs_size = zis_symbol_obj_data_size(lhs);
        const char *const lhs_data = zis_symbol_obj_data(lhs);
        const size_t rhs_size = zis_symbol_obj_data_size(rhs);
        const char *const rhs_data = zis_symbol_obj_data(rhs);
        if (lhs_size <= rhs_size) {
            result = memcmp(lhs_data, rhs_data, lhs_size);
            if (result == 0 && lhs_size != rhs_size)
                result = -(int)(unsigned char)rhs_data[lhs_size];
        } else {
            result = memcmp(lhs_data, rhs_data, rhs_size);
            if (result == 0)
                result = (unsigned char)lhs_data[rhs_size];
        }
#if INT_MAX > ZIS_SMALLINT_MAX
        if (result > ZIS_SMALLINT_MAX || result < ZIS_SMALLINT_MIN)
            result /= 4;
#endif
    }
    frame[0] = zis_smallint_to_ptr(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Symbol_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Symbol:hash() :: Int
    Generates hash code. */
    assert_arg1_Symbol(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_symbol_obj *self = zis_object_cast(frame[1], struct zis_symbol_obj);
    const size_t h = zis_hash_bytes(zis_symbol_obj_data(self), zis_symbol_obj_data_size(self));
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)h);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Symbol_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Symbol:to_string(?fmt) :: String
    Generates a string representation. */
    assert_arg1_Symbol(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_symbol_obj *self = zis_object_cast(frame[1], struct zis_symbol_obj);
    struct zis_string_obj **result_p = (struct zis_string_obj **)frame;
    *result_p = zis_string_obj_new(z, "\\<Symbol ", 9);
    struct zis_string_obj *sym_as_str =
        zis_string_obj_new(z, zis_symbol_obj_data(self), zis_symbol_obj_data_size(self));
    if (!sym_as_str)
        sym_as_str = zis_string_obj_new(z, "??", 2);
    *result_p = zis_string_obj_concat(z, *result_p, sym_as_str);
    *result_p = zis_string_obj_concat(z, *result_p, zis_string_obj_new(z, ">", 1));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Symbol_F_for, z, {1, 0, 1}) {
    /*#DOCSTR# func Symbol.\'for'(name :: String) :: Symbol
    Retrieves or creates a symbol by name. */
    struct zis_object **frame = z->callstack->frame;
    if (!zis_object_type_is(frame[1], z->globals->type_String)) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE, "name", frame[1]
        ));
        return ZIS_THR;
    }
    struct zis_symbol_obj *const result =
        zis_symbol_registry_gets(z, zis_object_cast(frame[1], struct zis_string_obj));
    frame[0] = zis_object_from(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_Symbol_D_methods,
    { "=="          , &T_Symbol_M_operator_equ  },
    { "<=>"         , &T_Symbol_M_operator_cmp  },
    { "hash"        , &T_Symbol_M_hash          },
    { "to_string"   , &T_Symbol_M_to_string     },
);

ZIS_NATIVE_VAR_DEF_LIST(
    T_Symbol_D_statics,
    { "for"         , { '^', .F = &T_Symbol_F_for   }},
);

ZIS_NATIVE_TYPE_DEF_XB(
    Symbol,
    struct zis_symbol_obj, _bytes_size,
    NULL, T_Symbol_D_methods, T_Symbol_D_statics
);

/* ----- symbol registry ---------------------------------------------------- */

struct zis_symbol_registry {
    // NOTE: Due to the limitations of object memory design, it is not possible
    // to use a Map object here. Instead, a stand-alone hash set is implemented.

    struct zis_symbol_obj **buckets;
    size_t bucket_count;
    size_t symbol_count, symbol_count_threshold;
};

#define SYM_REG_LOAD_FACTOR    0.9
#define SYM_REG_INIT_CAPACITY  500

static void symbol_registry_resize(struct zis_symbol_registry *sr, size_t new_sym_cnt_max) {
    const size_t new_bkt_cnt = (size_t)ceil((double)new_sym_cnt_max / SYM_REG_LOAD_FACTOR);
    const size_t new_bkts_sz = sizeof(void *) * new_bkt_cnt;
    struct zis_symbol_obj **const new_buckets = zis_mem_alloc(new_bkts_sz);
    memset(new_buckets, 0, new_bkts_sz);

    struct zis_symbol_obj **const old_buckets = sr->buckets;
    const size_t old_bkt_cnt = sr->bucket_count;
    for (size_t i = 0; i < old_bkt_cnt; i++) {
        struct zis_symbol_obj *node = old_buckets[i];
        while (node) {
            struct zis_symbol_obj *const next_node = node->_registry_next;
            const size_t node_new_bkt_idx = node->hash % new_bkt_cnt;
            node->_registry_next = new_buckets[node_new_bkt_idx];
            new_buckets[node_new_bkt_idx] = node;
            node = next_node;
        }
    }
    zis_mem_free(old_buckets);

    sr->buckets = new_buckets;
    sr->bucket_count = new_bkt_cnt;
    sr->symbol_count_threshold = new_sym_cnt_max;

    zis_debug_log(INFO, "Symbol", "symbol registry hash set resized (max=%zu)", new_sym_cnt_max);
}

static void symbol_registry_add(struct zis_symbol_registry *sr, struct zis_symbol_obj *sym) {
    assert(zis_object_meta_is_not_young(sym->_meta));
    assert(sr->bucket_count);
    size_t bkt_idx = sym->hash % sr->bucket_count;

    const size_t orig_sym_cnt = sr->symbol_count;
    if (zis_unlikely(orig_sym_cnt >= sr->symbol_count_threshold && sr->buckets[bkt_idx])) {
        symbol_registry_resize(sr, sr->symbol_count_threshold * 2);
        bkt_idx = sym->hash % sr->bucket_count; // Re-calculate index.
    }

    assert(!sym->_registry_next);
    sym->_registry_next = sr->buckets[bkt_idx];
    sr->buckets[bkt_idx] = sym;
    sr->symbol_count = orig_sym_cnt + 1;

    zis_debug_log(
        TRACE, "Symbol", "new symbol: `%.*s`",
        (int)zis_symbol_obj_data_size(sym), zis_symbol_obj_data(sym)
    );
}

static struct zis_symbol_obj *
symbol_registry_find(struct zis_symbol_registry *sr, const char *str, size_t str_len) {
    assert(sr->bucket_count);
    const size_t str_hash = zis_hash_bytes(str, str_len);
    size_t bkt_idx = str_hash % sr->bucket_count;
    for (struct zis_symbol_obj *sym = sr->buckets[bkt_idx]; sym; sym = sym->_registry_next) {
        if (
            sym->hash == str_hash &&
            zis_symbol_obj_data_size(sym) == str_len &&
            memcmp(sym->data, str, str_len) == 0
        ) {
            return sym;
        }
    }
    return NULL;
}

static void symbol_registry_wr_visitor(void *_sr, enum zis_objmem_weak_ref_visit_op op) {
    if (op == ZIS_OBJMEM_WEAK_REF_VISIT_FINI_Y)
        return; // Symbol objects are always old.

    struct zis_symbol_registry *const sr = _sr;
    size_t delete_count = 0;

    struct zis_symbol_obj **const buckets = sr->buckets;
    const size_t bucket_count = sr->bucket_count;
    for (size_t bucket_i = 0; bucket_i < bucket_count; bucket_i++) {
        struct zis_symbol_obj *this_node = buckets[bucket_i];
        struct zis_symbol_obj *prev_node =
            (void *)((char *)(buckets + bucket_i) - offsetof(struct zis_symbol_obj, _registry_next));
        assert(&prev_node->_registry_next == &buckets[bucket_i]);
        while (this_node) {
            bool delete_this_node = false;

#if defined(__GNUC__) && !defined(__clang__)
            static_assert(zis_struct_maybe_object(struct zis_symbol_obj), "");
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wstrict-aliasing"
            // The following code breaks the strict-aliasing rule. GCC wisely points this out.
            // But stupid me could not think of a better solution.
#endif // GCC

#define WEAK_REF_FINI(the_obj)  (delete_this_node = true)
            zis_objmem_visit_weak_ref(prev_node->_registry_next, op);
#undef WEAK_REF_FINI

#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#endif // GCC

            if (zis_unlikely(delete_this_node)) {
                zis_debug_log(
                    TRACE, "Symbol", "free symbol: `%.*s`",
                    (int)zis_symbol_obj_data_size(this_node), zis_symbol_obj_data(this_node)
                );
                delete_count++;
                this_node = this_node->_registry_next;
                prev_node->_registry_next = this_node;
            } else {
                prev_node = this_node;
                this_node = this_node->_registry_next;
            }
        }
    }

    if (delete_count) {
        assert(sr->symbol_count >= delete_count);
        sr->symbol_count -= delete_count;
        zis_debug_log(INFO, "Symbol", "%zu freed, %zu left", delete_count, sr->symbol_count);
    }
}

struct zis_symbol_registry *zis_symbol_registry_create(struct zis_context *z) {
    struct zis_symbol_registry *const sr =
        zis_mem_alloc(sizeof(struct zis_symbol_registry));
    sr->buckets = NULL;
    sr->bucket_count = 0, sr->symbol_count = 0, sr->symbol_count_threshold = 0;
    symbol_registry_resize(sr, SYM_REG_INIT_CAPACITY);
    zis_objmem_register_weak_ref_collection(z, sr, symbol_registry_wr_visitor);
    return sr;
}

void zis_symbol_registry_destroy(struct zis_symbol_registry *sr, struct zis_context *z) {
    zis_objmem_unregister_weak_ref_collection(z, sr);
    zis_mem_free(sr->buckets);
    zis_mem_free(sr);
}

struct zis_symbol_obj *zis_symbol_registry_get(
    struct zis_context *z, const char *s, size_t n
) {
    struct zis_symbol_registry *const sr = z->symbol_registry;
    if (zis_unlikely(n == (size_t)-1))
        n = strlen(s);
    struct zis_symbol_obj *sym = symbol_registry_find(sr, s, n);
    if (zis_unlikely(!sym)) {
        sym = zis_symbol_obj_new(z, s, n);
        symbol_registry_add(sr, sym);
    }
    return sym;
}

struct zis_symbol_obj *zis_symbol_registry_get2(
    struct zis_context *z,
    const char *s1, size_t n1 /* = -1 */, const char *s2, size_t n2 /* = -1 */
) {
    if (zis_unlikely(n1 == (size_t)-1))
        n1 = strlen(s1);
    if (zis_unlikely(n2 == (size_t)-1))
        n2 = strlen(s2);
    const size_t n = n1 + n2;
    if (n <= 64) {
        char buffer[64];
        assert(n <= sizeof buffer);
        memcpy(buffer, s1, n1);
        memcpy(buffer + n1, s2, n2);
        return zis_symbol_registry_get(z, buffer, n);
    }
    char *s = zis_mem_alloc(n);
    memcpy(s, s1, n1);
    memcpy(s + n1, s2, n2);
    struct zis_symbol_obj *sym = zis_symbol_registry_get(z, s, n);
    zis_mem_free(s);
    return sym;
}

struct zis_symbol_obj *zis_symbol_registry_gets(
    struct zis_context *z,
    struct zis_string_obj *str
) {
    const char *str_data = zis_string_obj_data_utf8(str);
    if (str_data) {
        const size_t str_size = zis_string_obj_length(str);
        return zis_symbol_registry_get(z, str_data, str_size);
    }
    size_t n = zis_string_obj_value(str, NULL, 0);
    if (n <= 64) {
        char buffer[64];
        assert(n <= sizeof buffer);
        n = zis_string_obj_value(str, buffer, n);
        assert(n != (size_t)-1);
        return zis_symbol_registry_get(z, buffer, n);
    }
    char *s = zis_mem_alloc(n);
    n = zis_string_obj_value(str, s, n);
    assert(n != (size_t)-1);
    struct zis_symbol_obj *sym = zis_symbol_registry_get(z, s, n);
    zis_mem_free(s);
    return sym;
}

struct zis_symbol_obj *zis_symbol_registry_find(
    struct zis_context *z, const char *s, size_t n /* = -1 */
) {
    struct zis_symbol_registry *const sr = z->symbol_registry;
    if (zis_unlikely(n == (size_t)-1))
        n = strlen(s);
    return symbol_registry_find(sr, s, n);
}
