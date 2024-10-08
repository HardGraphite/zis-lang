#include "tupleobj.h"

#include <string.h>

#include "algorithm.h"
#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "objvec.h"
#include "stack.h"

#include "exceptobj.h"
#include "stringobj.h"

/// Allocate.
struct zis_tuple_obj *tuple_obj_alloc(struct zis_context *z, size_t n) {
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Tuple, 1 + n, 0
    );
    struct zis_tuple_obj *const self = zis_object_cast(obj, struct zis_tuple_obj);
    assert(zis_tuple_obj_length(self) == n);
    return self;
}

struct zis_tuple_obj *zis_tuple_obj_new(
    struct zis_context *z,
    struct zis_object *v[], size_t n
) {
    assert(n != (size_t)-1);

    if (zis_unlikely(!n))
        return z->globals->val_empty_tuple;

    struct zis_tuple_obj *const self = tuple_obj_alloc(z, n);
    if (zis_likely(v)) {
        zis_object_vec_copy(self->_data, v, n);
        zis_object_write_barrier_n(self, v, n);
    } else {
        zis_object_vec_zero(self->_data, n);
    }
    return self;
}

struct zis_tuple_obj *_zis_tuple_obj_new_empty(struct zis_context *z) {
    return tuple_obj_alloc(z, 0U);
}

struct zis_tuple_obj *zis_tuple_obj_concat(
    struct zis_context *z,
    struct zis_object_vec_view tuples
) {
    size_t new_tuple_len = 0;

    // Check types and compute the total length.
    {
        struct zis_type_obj *const type_Tuple = z->globals->type_Tuple;
        zis_object_vec_view_foreach_unchanged(tuples, item, {
            if (!zis_object_type_is(item, type_Tuple))
                zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL); // TODO: handles type error.
            struct zis_tuple_obj *tuple = zis_object_cast(item, struct zis_tuple_obj);
            const size_t tuple_len = zis_tuple_obj_length(tuple);
            new_tuple_len += tuple_len;
        });
    }

    if (zis_unlikely(!new_tuple_len)) {
        return z->globals->val_empty_tuple;
    }
    if (zis_unlikely(zis_object_vec_view_length(tuples) == 1)) {
        struct zis_object *item = zis_object_vec_view_data(tuples)[0];
        return zis_object_cast(item, struct zis_tuple_obj);
    }

    struct zis_tuple_obj *new_tuple = tuple_obj_alloc(z, new_tuple_len);

    {
        size_t copied_count = 0;
        zis_object_vec_view_foreach_unchanged(tuples, item, {
            assert(zis_object_type_is(item, z->globals->type_Tuple));
            struct zis_tuple_obj *tuple = zis_object_cast(item, struct zis_tuple_obj);
            const size_t tuple_len = zis_tuple_obj_length(tuple);
            assert(copied_count + tuple_len <= new_tuple_len);
            zis_object_vec_copy(new_tuple->_data + copied_count, tuple->_data, tuple_len);
            zis_object_write_barrier_n(new_tuple, tuple->_data, tuple_len);
            copied_count += tuple_len;
        });
        assert(copied_count == new_tuple_len);
    }

    return new_tuple;
}

struct zis_tuple_obj *zis_tuple_obj_slice(
    struct zis_context *z,
    struct zis_tuple_obj *_tuple, size_t start, size_t length
) {
    const size_t tuple_len = zis_tuple_obj_length(_tuple);
    if (zis_unlikely(start + length < start || start + length > tuple_len))
        return NULL;
    if (zis_unlikely(start == 0 && length == tuple_len))
        return _tuple;
    if (zis_unlikely(!tuple_len))
        return z->globals->val_empty_tuple;

    zis_locals_decl_1(z, var, struct zis_tuple_obj *source_tuple);
    var.source_tuple = _tuple;
    struct zis_tuple_obj *new_tuple = tuple_obj_alloc(z, length);
    zis_object_vec_copy(new_tuple->_data, var.source_tuple->_data + start, length);
    zis_object_write_barrier_n(new_tuple, var.source_tuple->_data + start, length);
    zis_locals_drop(z, var);
    return new_tuple;
}

#define assert_arg1_Tuple(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Tuple)))

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_operator_add, z, {2, 0, 2}) {
    /*#DOCSTR# func Tuple:\'+'(other :: Tuple) :: Tuple
    Concatenates two tuples. */
    assert_arg1_Tuple(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_context_globals *g = z->globals;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Tuple))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "+", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    struct zis_tuple_obj *result =
        zis_tuple_obj_concat(z, zis_object_vec_view_from_frame(frame, 1, 2));
    frame[0] = zis_object_from(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_operator_get_elem, z, {2, 0, 2}) {
    /*#DOCSTR# func Tuple:\'[]'(index :: Int) :: Any
    Gets an element by index. */
    assert_arg1_Tuple(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_context_globals *g = z->globals;

    struct zis_object *result;
    struct zis_tuple_obj *self = zis_object_cast(frame[1], struct zis_tuple_obj);
    if (zis_object_is_smallint(frame[2])) {
        const size_t index = zis_object_index_convert(
            zis_tuple_obj_length(self),
            zis_smallint_from_ptr(frame[2])
        );
        if (zis_unlikely(index == (size_t)-1))
            goto thr_index_out_of_range;
        result = zis_tuple_obj_get(self, index);
    } else if (zis_object_type(frame[2]) == g->type_Int) {
    thr_index_out_of_range:
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, frame[2]
        ));
        return ZIS_THR;
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_SUBS,
            "[]", frame[1], frame[2]
        ));
        return ZIS_THR;
    }

    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Tuple:\'=='(other :: Tuple) :: Bool
    Operator ==. */
    assert_arg1_Tuple(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    bool equals;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Tuple))) {
        equals = false;
    } else if (
        zis_tuple_obj_length(zis_object_cast(frame[1], struct zis_tuple_obj)) !=
        zis_tuple_obj_length(zis_object_cast(frame[2], struct zis_tuple_obj))
    ) {
        equals = false;
    } else {
        for (size_t i = 0; ; i++) {
            struct zis_tuple_obj *lhs = zis_object_cast(frame[1], struct zis_tuple_obj);
            struct zis_object *lhs_elem = zis_tuple_obj_get_checked(lhs, i);
            struct zis_tuple_obj *rhs = zis_object_cast(frame[2], struct zis_tuple_obj);
            struct zis_object *rhs_elem = zis_tuple_obj_get_checked(rhs, i);
            if (!lhs_elem) {
                equals = rhs_elem ? false : true;
                break;
            } else if (!rhs_elem) {
                equals = false;
                break;
            }
            equals = zis_object_equals(z, lhs_elem, rhs_elem);
            if (!equals)
                break;
        }
    }

    frame[0] = zis_object_from(equals ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Tuple:\'<=>'(other :: Tuple) :: Int
    Operator <=>. */
    assert_arg1_Tuple(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Tuple))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "<=>", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    int result;
    for (size_t i = 0; ; i++) {
        struct zis_tuple_obj *lhs = zis_object_cast(frame[1], struct zis_tuple_obj);
        struct zis_object *lhs_elem = zis_tuple_obj_get_checked(lhs, i);
        struct zis_tuple_obj *rhs = zis_object_cast(frame[2], struct zis_tuple_obj);
        struct zis_object *rhs_elem = zis_tuple_obj_get_checked(rhs, i);
        if (!lhs_elem) {
            result = rhs_elem ? -1 : 0;
            break;
        } else if (!rhs_elem) {
            result = 1;
            break;
        }
        const enum zis_object_ordering o = zis_object_compare(z, lhs_elem, rhs_elem);
        if (o == ZIS_OBJECT_IC)
            return ZIS_THR;
        if (o != ZIS_OBJECT_EQ) {
            assert(o == ZIS_OBJECT_LT || ZIS_OBJECT_GT);
            static_assert(ZIS_OBJECT_LT < 0 && ZIS_OBJECT_GT > 0, "");
            result = o;
            break;
        }
    }

    frame[0] = zis_smallint_try_to_ptr(result);
    if (zis_unlikely(!frame[0]))
        frame[0] = zis_smallint_to_ptr(result / 8);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_length, z, {1, 0, 1}) {
    /*#DOCSTR# func Tuple:length() :: Int
    Returns the total number of elements. */
    assert_arg1_Tuple(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_tuple_obj *self = zis_object_cast(frame[1], struct zis_tuple_obj);
    const size_t len = zis_tuple_obj_length(self);
    assert(len <= ZIS_SMALLINT_MAX);
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)len);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Tuple:hash() :: Int
    Generates a hash code for this tuple. */
    assert_arg1_Tuple(z);
    struct zis_object **frame = z->callstack->frame;

    size_t hash_code = 1;
    for (size_t i = 0; ; i++) {
        struct zis_tuple_obj *tuple = zis_object_cast(frame[1], struct zis_tuple_obj);
        struct zis_object *element = zis_tuple_obj_get_checked(tuple, i);
        if (!element)
            break;
        size_t element_hash_code;
        if (zis_unlikely(!zis_object_hash(&element_hash_code, z, element)))
            return ZIS_THR;
        zis_hash_combine(&hash_code, element_hash_code);
    }

    const zis_smallint_t result = (zis_smallint_t)zis_hash_truncate(hash_code);
    frame[0] = zis_smallint_to_ptr(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Tuple_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Tuple:to_string(?fmt) :: String
    Returns string representation for this tuple. */
    assert_arg1_Tuple(z);
    struct zis_object **frame = z->callstack->frame;

    struct zis_string_obj **str_obj_p = (struct zis_string_obj **)(frame + 2);
    *str_obj_p = zis_string_obj_new(z, "(", 1);
    for (size_t i = 0; ; i++) {
        struct zis_tuple_obj *tuple = zis_object_cast(frame[1], struct zis_tuple_obj);
        if (i >= zis_tuple_obj_length(tuple))
            break;
        if (i)
            *str_obj_p = zis_string_obj_concat(z, *str_obj_p, zis_string_obj_new(z, ", ", 2));
        *str_obj_p = zis_string_obj_concat(
            z, *str_obj_p,
            zis_object_to_string(z, zis_tuple_obj_get(tuple, i), true, NULL)
        );
    }
    const bool has_one_element =
        zis_tuple_obj_length(zis_object_cast(frame[1], struct zis_tuple_obj)) == 1;
    *str_obj_p = zis_string_obj_concat(
        z, *str_obj_p,
        zis_string_obj_new(z, ",)" + (ptrdiff_t)(has_one_element ? 0 : 1), (size_t)-1)
    );

    assert(zis_object_type_is(zis_object_from(*str_obj_p), z->globals->type_String));
    frame[0] = zis_object_from(*str_obj_p);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_tuple_D_methods,
    { "+"           , &T_Tuple_M_operator_add      },
    { "[]"          , &T_Tuple_M_operator_get_elem },
    { "=="          , &T_Tuple_M_operator_equ      },
    { "<=>"         , &T_Tuple_M_operator_cmp      },
    { "length"      , &T_Tuple_M_length            },
    { "hash"        , &T_Tuple_M_hash              },
    { "to_string"   , &T_Tuple_M_to_string         },
);

ZIS_NATIVE_TYPE_DEF_XS_NB(
    Tuple, struct zis_tuple_obj,
    NULL, T_tuple_D_methods, NULL
);
