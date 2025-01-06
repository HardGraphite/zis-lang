#include "object.h"

#include "attributes.h"
#include "context.h"
#include "globals.h"
#include "invoke.h"
#include "locals.h"

#include "boolobj.h"
#include "exceptobj.h"
#include "intobj.h"
#include "rangeobj.h"
#include "stringobj.h"
#include "symbolobj.h"
#include "typeobj.h"

bool zis_object_hash(
    size_t *restrict hash_code,
    struct zis_context *z, struct zis_object *obj
) {
    if (zis_object_is_smallint(obj)) {
        *hash_code = zis_smallint_hash(zis_smallint_from_ptr(obj));
        return true;
    }
    if (zis_object_type(obj) == z->globals->type_Symbol) {
        *hash_code = zis_symbol_obj_hash(zis_object_cast(obj, struct zis_symbol_obj));
        return true;
    }

    struct zis_object *ret;
    zis_context_set_reg0(z, zis_object_from(z->globals->sym_hash));
    if (zis_unlikely(zis_invoke_vn(z, &ret, NULL, (struct zis_object *[]){obj}, 1)))
        return false;

    if (zis_object_is_smallint(ret)) {
        const zis_smallint_t h = zis_smallint_from_ptr(ret);
        *hash_code = h >= (zis_smallint_t)0 ? h : -h;
        return true;
    }
    if (zis_object_type(ret) == z->globals->type_Int) {
        const int64_t h =
            zis_int_obj_value_trunc_i(zis_object_cast(ret, struct zis_int_obj));
        *hash_code = (size_t)(h >= INT64_C(0) ? h : -h);
        return true;
    }

    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, "type", ret, "method `%s()' returned a non-%s value", "hash", "integer"
    )));
    return false;
}

enum zis_object_ordering zis_object_compare(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
) {
    if (lhs == rhs)
        return ZIS_OBJECT_EQ;

    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs);
        const zis_smallint_t rhs_v = zis_smallint_from_ptr(rhs);
        assert(lhs_v != rhs_v);
        return lhs_v <  rhs_v ? ZIS_OBJECT_LT : ZIS_OBJECT_GT;
    }

    struct zis_object *ret;
    zis_context_set_reg0(z, zis_object_from(z->globals->sym_operator_cmp));
    if (zis_unlikely(zis_invoke_vn(z, &ret, NULL, (struct zis_object *[]){lhs, rhs}, 2)))
        return ZIS_OBJECT_IC;

    if (zis_object_is_smallint(ret)) {
        const zis_smallint_t x = zis_smallint_from_ptr(ret);
        return x == 0 ? ZIS_OBJECT_EQ : x < 0 ? ZIS_OBJECT_LT : ZIS_OBJECT_GT;
    }
    if (zis_object_type(ret) == z->globals->type_Int) {
        const bool neg = zis_int_obj_sign(zis_object_cast(ret, struct zis_int_obj));
        return neg ? ZIS_OBJECT_LT : ZIS_OBJECT_GT;
    }

    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, "type", ret, "method `%s()' returned a non-%s value", "<=>", "integer"
    )));
    return false;
}

bool zis_object_equals(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
) {
    if (lhs == rhs)
        return true;

    {
        struct zis_type_obj *const type_sym = z->globals->type_Symbol;
        if (
            (zis_object_is_smallint(lhs) || zis_object_type(lhs) == type_sym) &&
            (zis_object_is_smallint(rhs) || zis_object_type(rhs) == type_sym)
        ) {
            return false;
        }
    }

    struct zis_context_globals *const g = z->globals;
    struct zis_type_obj *const lhs_type =
        zis_likely(!zis_object_is_smallint(lhs)) ? zis_object_type(lhs) : g->type_Int;
    struct zis_object *ret, *method;
    // ==
    if (zis_likely((method = zis_type_obj_get_method(lhs_type, g->sym_operator_equ)))) {
        if (zis_unlikely(zis_invoke_vn(z, &ret, method, (struct zis_object *[]){lhs, rhs}, 2)))
            return false; // FIXME: REG-0 may have been modified.
        return ret == zis_object_from(z->globals->val_true);
    }
    // <=>
    if (zis_likely((method = zis_type_obj_get_method(lhs_type, g->sym_operator_cmp)))) {
        if (zis_unlikely(zis_invoke_vn(z, &ret, method, (struct zis_object *[]){lhs, rhs}, 2)))
            return false; // FIXME: REG-0 may have been modified.
        return ret == zis_smallint_to_ptr(0);
    }
    // ===
    return lhs == rhs;
}

struct zis_string_obj *zis_object_to_string(
    struct zis_context *z,
    struct zis_object *obj, bool represent, const char *restrict format /* = NULL */
) {
    struct zis_object *to_string_format;
    if (represent) {
        to_string_format = zis_object_from(z->globals->val_true);
    } else if (format) {
        zis_locals_decl(
            z, var,
            struct zis_object *arg_obj;
            struct zis_string_obj *arg_fmt;
        );
        zis_locals_zero(var);
        var.arg_obj = obj;
        var.arg_fmt = zis_string_obj_new(z, format, (size_t)-1);
        zis_locals_drop(z, var);
        obj = var.arg_obj;
        to_string_format = zis_object_from(var.arg_fmt);
    } else {
        to_string_format = NULL;
    }

    struct zis_object *to_string_method = zis_type_obj_get_method(
        !zis_object_is_smallint(obj) ? zis_object_type(obj) : z->globals->type_Int,
        zis_symbol_registry_get(z, "to_string", 9)
    );
    if (to_string_method) {
        struct zis_object *ret;
        if (zis_unlikely(zis_invoke_vn(
            z, &ret, to_string_method,
            (struct zis_object *[]){obj, to_string_format}, to_string_format ? 2 : 1)
        )) {
            return NULL;
        }

        if (zis_object_type_is(ret, z->globals->type_String))
            return zis_object_cast(ret, struct zis_string_obj);

        zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
            z, "type", ret, "method `%s()' returned a non-%s value", "to_string", "string"
        )));
        return NULL;
    } else {
        // Default representation. For those who do not provide `to_string()` method.
        zis_locals_decl_1(z, var, struct zis_string_obj *result);
        zis_locals_zero_1(var, result);
        var.result = zis_context_guess_variable_name(z, zis_object_from(zis_object_type(obj)));
        if (!var.result)
            var.result = zis_string_obj_new(z, "\?\?", 2);
        var.result = zis_string_obj_concat2(z, zis_string_obj_new(z, "\\<", 2), var.result);
        var.result = zis_string_obj_concat2(z, var.result, zis_string_obj_new(z, ">", 1));
        zis_locals_drop(z, var);
        return var.result;
    }
}

struct zis_object *zis_object_get_element(
    struct zis_context *z,
    struct zis_object *obj, struct zis_object *key
) {
    struct zis_object *ret;
    zis_context_set_reg0(z, zis_object_from(z->globals->sym_operator_get_element));
    if (zis_unlikely(zis_invoke_vn(z, &ret, NULL, (struct zis_object *[]){obj, key}, 2)))
        return NULL;
    return ret;
}

int zis_object_set_element(
    struct zis_context *z,
    struct zis_object *obj, struct zis_object *key, struct zis_object *value
) {
    zis_context_set_reg0(z, zis_object_from(z->globals->sym_operator_set_element));
    return zis_invoke_vn(z, NULL, NULL, (struct zis_object *[]){obj, key, value}, 3);
}

zis_noinline size_t _zis_object_index_convert_slow(
    zis_smallint_unsigned_t length, zis_smallint_t index
) {
    assert(index <= 0 || (zis_smallint_unsigned_t)index > length); // Handled in `zis_object_index_convert()`.

    if (zis_unlikely(index >= 0))
        return (size_t)-1;

    assert(length <= ZIS_SMALLINT_MAX);
    index += (zis_smallint_t)length;
    if (zis_unlikely((zis_smallint_unsigned_t)index >= length))
        return (size_t)-1;

    assert(index >= 0 && (zis_smallint_unsigned_t)index < length);
    return (zis_smallint_unsigned_t)index;
}

bool zis_object_index_range_convert(struct zis_object_index_range_convert_args *restrict args) {
    const size_t length = args->length;
    const size_t begin = zis_object_index_convert(length, args->range->begin);
    if (zis_unlikely(begin == (size_t)-1))
        return false;
    const size_t end = zis_object_index_convert(length, args->range->end);
    if (zis_unlikely(end == (size_t)-1 || begin > end)) {
        if (end != (size_t)-1 ? end : zis_object_index_convert((zis_smallint_unsigned_t)-1, args->range->end) == begin - 1) {
            args->offset = begin;
            args->count = 0;
            return true;
        }
        return false;
    }
    args->offset = begin;
    args->count = end - begin + 1;
    return true;
}
