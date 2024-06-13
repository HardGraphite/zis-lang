#include "arrayobj.h"

#include "context.h"
#include "globals.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "exceptobj.h"
#include "stringobj.h"

/* ----- array slots -------------------------------------------------------- */

/// Allocate but do not initialize slots.
static struct zis_array_slots_obj *
array_slots_obj_alloc(struct zis_context *z, size_t n) {
    struct zis_array_slots_obj *const self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Array_Slots, 1 + n, 0
        ),
        struct zis_array_slots_obj
    );
    assert(zis_array_slots_obj_length(self) == n);
    return self;
}

struct zis_array_slots_obj *zis_array_slots_obj_new(
    struct zis_context *z, struct zis_object *v[], size_t n
) {
    if (zis_unlikely(!n))
        return z->globals->val_empty_array_slots;

    struct zis_array_slots_obj *const self = array_slots_obj_alloc(z, n);

    if (v) {
        zis_object_vec_copy(self->_data, v, n);
        zis_object_write_barrier_n(self, v, n);
    } else {
        zis_object_vec_zero(self->_data, n);
    }

    return self;
}

struct zis_array_slots_obj *zis_array_slots_obj_new2(
    struct zis_context *z, size_t len,
    struct zis_array_slots_obj *_other_slots
) {
    if (zis_unlikely(!len))
        return z->globals->val_empty_array_slots;

    zis_locals_decl_1(z, var, struct zis_array_slots_obj *other_slots);
    var.other_slots = _other_slots;

    struct zis_array_slots_obj *const self = array_slots_obj_alloc(z, len);
    assert(zis_array_slots_obj_length(self) == len);

    struct zis_object **const v = var.other_slots->_data;
    size_t n = zis_array_slots_obj_length(var.other_slots);
    if (n > len)
        n = len;
    zis_object_vec_copy(self->_data, v, n);
    zis_object_write_barrier_n(self, v, n);
    zis_object_vec_zero(self->_data + n, len - n);

    zis_locals_drop(z, var);
    return self;
}

struct zis_array_slots_obj *_zis_array_slots_obj_new_empty(struct zis_context *z) {
    return array_slots_obj_alloc(z, 0);
}

ZIS_NATIVE_TYPE_DEF_XS_NB(
    Array_Slots,
    struct zis_array_slots_obj,
    NULL, NULL, NULL
);

/* ----- array -------------------------------------------------------------- */

struct zis_array_obj *zis_array_obj_new(
    struct zis_context *z,
    struct zis_object *v[], size_t n
) {
    struct zis_array_obj *self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Array),
        struct zis_array_obj
    );
    self->_data = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier(self);
    self->length = n;
    if (n) {
        zis_locals_decl_1(z, var, struct zis_array_obj *self);
        var.self = self;
        struct zis_array_slots_obj *const data = zis_array_slots_obj_new(z, v, n);
        self = var.self;
        zis_locals_drop(z, var);
        self->_data = data;
        zis_object_assert_no_write_barrier(self);
    }
    return self;
}

struct zis_array_obj *zis_array_obj_new2(
    struct zis_context *z,
    size_t reserve, struct zis_object *v[], size_t n
) {
    struct zis_array_obj *self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Array),
        struct zis_array_obj
    );
    self->_data = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier(self);
    self->length = n;
    if (reserve < n)
        reserve = n;
    if (reserve) {
        zis_locals_decl_1(z, var, struct zis_array_obj *self);
        var.self = self;
        struct zis_array_slots_obj *data;
        if (v) {
            data = array_slots_obj_alloc(z, reserve);
            zis_object_vec_copy(data->_data, v, n);
            zis_object_vec_zero(data->_data + n, reserve - n);
        } else {
            data = zis_array_slots_obj_new(z, NULL, reserve);
        }
        self = var.self;
        zis_locals_drop(z, var);
        self->_data = data;
        zis_object_assert_no_write_barrier(self);
    }
    return self;
}

struct zis_array_obj *zis_array_obj_concat(
    struct zis_context *z,
    struct zis_array_obj *v[], size_t n
) {
    assert(v && n != (size_t)-1);

    size_t new_array_len = 0;
    for (size_t i = 0; i < n; i++) {
        struct zis_array_obj *array_i = v[i];
        const size_t array_i_len = zis_array_obj_length(array_i);
        new_array_len += array_i_len;
    }

    if (zis_unlikely(!new_array_len))
        return zis_array_obj_new(z, NULL, 0);

    struct zis_array_obj *new_array = zis_array_obj_new2(z, new_array_len, NULL, 0);
    new_array->length = new_array_len;
    for (size_t i = 0, copied_count = 0; i < n; i++) {
        struct zis_array_obj *array_i = v[i];
        const size_t array_i_len = zis_array_obj_length(array_i);
        assert(copied_count + array_i_len <= new_array_len);
        zis_object_vec_copy(new_array->_data->_data + copied_count, array_i->_data->_data, array_i_len);
        zis_object_write_barrier_n(new_array, array_i->_data->_data, array_i_len);
        copied_count += array_i_len;
    }

    return new_array;
}

void zis_array_obj_reserve(struct zis_context *z, struct zis_array_obj *self, size_t new_cap) {
    struct zis_array_slots_obj *self_data = self->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);

    if (old_cap >= new_cap)
        return;

    zis_locals_decl_1(z, var, struct zis_array_obj *self);
    var.self = self;
    self_data = zis_array_slots_obj_new2(z, new_cap, self_data);
    self = var.self;
    zis_locals_drop(z, var);

    self->_data = self_data;
    zis_object_write_barrier(self, self_data);
}

void zis_array_obj_clear(struct zis_array_obj *self) {
    zis_object_vec_zero(self->_data->_data, self->length);
    self->length = 0;
}

void zis_array_obj_append(
    struct zis_context *z, struct zis_array_obj *self, struct zis_object *v
) {
    struct zis_array_slots_obj *self_data = self->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);
    const size_t old_len = self->length;

    assert(old_len <= old_cap);
    if (zis_unlikely(old_len == old_cap)) {
        const size_t new_cap = old_cap >= 2 ? old_cap * 2 : 4;

        zis_locals_decl(z, var, struct zis_array_obj *self; struct zis_object *v;);
        var.self = self, var.v = v;
        self_data = zis_array_slots_obj_new2(z, new_cap, self_data);
        self = var.self, v = var.v;
        zis_locals_drop(z, var);

        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
    }

    zis_array_slots_obj_set(self_data, old_len, v);
    self->length = old_len + 1;
}

struct zis_object *zis_array_obj_pop(struct zis_array_obj *self) {
    struct zis_array_slots_obj *self_data = self->_data;
    const size_t old_len = self->length;
    assert(old_len <= zis_array_slots_obj_length(self_data));

    if (zis_unlikely(!old_len))
        return NULL; // empty

    const size_t new_len = old_len - 1;
    self->length = new_len;
    struct zis_object *const elem = zis_array_slots_obj_get(self_data, new_len);
    self_data->_data[new_len] = (void *)(uintptr_t)-1;
    assert(zis_object_is_smallint(self_data->_data[new_len]));

    return elem;
}

bool zis_array_obj_insert(
    struct zis_context *z,
    struct zis_array_obj *self, size_t pos, struct zis_object *v
) {
    const size_t old_len = self->length;
    if (zis_unlikely(pos >= old_len)) {
        if (zis_unlikely(pos > old_len))
            return false;
        zis_array_obj_append(z, self, v);
        return true;
    }

    struct zis_array_slots_obj *self_data = self->_data;
    struct zis_object **old_data = self_data->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);

    assert(old_len <= old_cap);
    if (zis_unlikely(old_len == old_cap)) {
        const size_t new_cap = old_cap >= 2 ? old_cap * 2 : 4;

        zis_locals_decl(z, var, struct zis_array_obj *self; struct zis_object *v;);
        var.self = self, var.v = v;
        self_data = array_slots_obj_alloc(z, new_cap);
        self = var.self, v = var.v;
        zis_locals_drop(z, var);
        old_data = self->_data->_data;

        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
        zis_object_vec_copy(self_data->_data, old_data, pos);
        zis_object_write_barrier_n(self_data, old_data, pos);
    }
    zis_object_vec_move(self_data->_data + pos + 1, old_data + pos, old_len - pos);
    zis_array_slots_obj_set(self_data, pos, v);
    self->length = old_len + 1;
    return true;
}

bool zis_array_obj_remove(
    struct zis_context *z,
    struct zis_array_obj *self, size_t pos
) {
    const size_t old_len = self->length;
    if (zis_unlikely(!old_len || pos >= old_len))
        return false;
    if (zis_unlikely(pos == old_len - 1)) {
        zis_array_obj_pop(self);
        return true;
    }

    struct zis_array_slots_obj *self_data = self->_data;
    struct zis_object **old_data = self_data->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);

    if (zis_unlikely(old_len <= old_cap / 2 && old_len >= 16)) {
        const size_t new_cap = old_len;

        zis_locals_decl_1(z, var, struct zis_array_obj *self);
        var.self = self;
        self_data = array_slots_obj_alloc(z, new_cap);
        self = var.self;
        zis_locals_drop(z, var);
        old_data = self->_data->_data;

        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
        zis_object_vec_copy(self_data->_data, old_data, pos);
        zis_object_write_barrier_n(self_data, old_data, pos);
    }
    const size_t new_len = old_len - 1;
    zis_object_vec_move(self_data->_data + pos, old_data + pos + 1, new_len - pos);
    self->length = new_len;
    self_data->_data[new_len] = (void *)(uintptr_t)-1;
    assert(zis_object_is_smallint(self_data->_data[new_len]));
    return true;
}

#define assert_arg1_Array(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Array)))

ZIS_NATIVE_FUNC_DEF(T_Array_M_operator_add, z, {2, 0, 2}) {
    /*#DOCSTR# func Array:\'+'(other :: Array) :: Array
    Concatenates two arrays. */
    assert_arg1_Array(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_context_globals *g = z->globals;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Array))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "+", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    struct zis_array_obj *result =
        zis_array_obj_concat(z, (struct zis_array_obj **)(frame + 1), 2);
    frame[0] = zis_object_from(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_operator_get_elem, z, {2, 0, 2}) {
    /*#DOCSTR# func Array:\'[]'(index :: Int) :: Any
    Gets an element by index. */
    assert_arg1_Array(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_context_globals *g = z->globals;

    struct zis_object *result;
    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    if (zis_object_is_smallint(frame[2])) {
        const size_t index = zis_object_index_convert(
            zis_array_obj_length(self),
            zis_smallint_from_ptr(frame[2])
        );
        if (zis_unlikely(index == (size_t)-1))
            goto thr_index_out_of_range;
        result = zis_array_obj_get(self, index);
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

ZIS_NATIVE_FUNC_DEF(T_Array_M_operator_set_elem, z, {3, 0, 3}) {
    /*#DOCSTR# func Array:\'[]='(index :: Int, value :: Any)
    Sets an element by index. */
    assert_arg1_Array(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_context_globals *g = z->globals;

    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    if (zis_object_is_smallint(frame[2])) {
        const size_t index = zis_object_index_convert(
            zis_array_obj_length(self),
            zis_smallint_from_ptr(frame[2])
        );
        if (zis_unlikely(index == (size_t)-1))
            goto thr_index_out_of_range;
        zis_array_obj_set(self, index, frame[3]);
    } else if (zis_object_type(frame[2]) == g->type_Int) {
    thr_index_out_of_range:
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, frame[2]
        ));
        return ZIS_THR;
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_SUBS,
            "[]=", frame[1], frame[2]
        ));
        return ZIS_THR;
    }

    frame[0] = frame[3];
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Array:\'=='(other :: Array) :: Bool
    Operator ==. */
    assert_arg1_Array(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    bool equals;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Array))) {
        equals = false;
    } else if (
        zis_array_obj_length(zis_object_cast(frame[1], struct zis_array_obj)) !=
        zis_array_obj_length(zis_object_cast(frame[2], struct zis_array_obj))
    ) {
        equals = false;
    } else {
        for (size_t i = 0; ; i++) {
            struct zis_array_obj *lhs = zis_object_cast(frame[1], struct zis_array_obj);
            struct zis_object *lhs_elem = zis_array_obj_get_checked(lhs, i);
            struct zis_array_obj *rhs = zis_object_cast(frame[2], struct zis_array_obj);
            struct zis_object *rhs_elem = zis_array_obj_get_checked(rhs, i);
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

ZIS_NATIVE_FUNC_DEF(T_Array_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Array:\'<=>'(other :: Array) :: Int
    Operator <=>. */
    assert_arg1_Array(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Array))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "<=>", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    int result;
    for (size_t i = 0; ; i++) {
        struct zis_array_obj *lhs = zis_object_cast(frame[1], struct zis_array_obj);
        struct zis_object *lhs_elem = zis_array_obj_get_checked(lhs, i);
        struct zis_array_obj *rhs = zis_object_cast(frame[2], struct zis_array_obj);
        struct zis_object *rhs_elem = zis_array_obj_get_checked(rhs, i);
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

ZIS_NATIVE_FUNC_DEF(T_Array_M_length, z, {1, 0, 1}) {
    /*#DOCSTR# func Array:length() :: Int
    Returns the total number of elements. */
    assert_arg1_Array(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    const size_t len = zis_array_obj_length(self);
    assert(len <= ZIS_SMALLINT_MAX);
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)len);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Array:to_string(?fmt) :: String
    Returns string representation for this array. */
    assert_arg1_Array(z);
    struct zis_object **frame = z->callstack->frame;

    struct zis_string_obj **str_obj_p = (struct zis_string_obj **)(frame + 2);
    *str_obj_p = zis_string_obj_new(z, "[", 1);
    for (size_t i = 0; ; i++) {
        struct zis_array_obj *array = zis_object_cast(frame[1], struct zis_array_obj);
        if (i >= zis_array_obj_length(array))
            break;
        if (i)
            *str_obj_p = zis_string_obj_concat(z, *str_obj_p, zis_string_obj_new(z, ", ", 2));
        *str_obj_p = zis_string_obj_concat(
            z, *str_obj_p,
            zis_object_to_string(z, zis_array_obj_get(array, i), true, NULL)
        );
    }
    *str_obj_p = zis_string_obj_concat(z, *str_obj_p, zis_string_obj_new(z, "]" , 1));

    assert(zis_object_type_is(zis_object_from(*str_obj_p), z->globals->type_String));
    frame[0] = zis_object_from(*str_obj_p);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_append, z, {2, 0, 2}) {
    /*#DOCSTR# func Array:append(value :: Any)
    Inserts an element to the end. */
    assert_arg1_Array(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    zis_array_obj_append(z, self, frame[2]);
    frame[0] = zis_object_from(g->val_nil);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_pop, z, {1, 0, 1}) {
    /*#DOCSTR# func Array:pop() :: Any
    Deletes and returns the last element. */
    assert_arg1_Array(z);
    struct zis_object **frame = z->callstack->frame;

    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    struct zis_object *value = zis_array_obj_pop(self);
    if (!value) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, zis_smallint_to_ptr(-1)
        ));
        return ZIS_THR;
    }
    frame[0] = value;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_insert, z, {3, 0, 3}) {
    /*#DOCSTR# func Array:insert(position :: Int, value :: Any)
    Inserts an element. */
    assert_arg1_Array(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    if (zis_object_is_smallint(frame[2])) {
        zis_smallint_t index_smi = zis_smallint_from_ptr(frame[2]);
        assert(zis_array_obj_length(self) <= ZIS_SMALLINT_MAX);
        const zis_smallint_t len_smi = (zis_smallint_t)(zis_smallint_unsigned_t)zis_array_obj_length(self);
        // Not using `zis_object_index_convert()` here because the index can be greater than the length.
        if (index_smi > 0) {
            index_smi--;
            if (zis_unlikely(index_smi > len_smi))
                goto thr_index_out_of_range;
        } else {
            if (zis_unlikely(index_smi == 0))
                goto thr_index_out_of_range;
            index_smi += len_smi + 1;
            if (zis_unlikely((zis_smallint_unsigned_t)index_smi > (zis_smallint_unsigned_t)len_smi))
                goto thr_index_out_of_range;
        }
        assert(index_smi >= 0 && index_smi <= len_smi);
        const size_t index = (zis_smallint_unsigned_t)index_smi;
        zis_array_obj_insert(z, self, index, frame[3]);
    } else if (zis_object_type(frame[2]) == g->type_Int) {
    thr_index_out_of_range:
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, frame[2]
        ));
        return ZIS_THR;
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE,
            "position", frame[2]
        ));
        return ZIS_THR;
    }

    frame[0] = zis_object_from(g->val_nil);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Array_M_remove, z, {2, 0, 2}) {
    /*#DOCSTR# func Array:remove(position :: Int)
    Removes an element. */
    assert_arg1_Array(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_array_obj *self = zis_object_cast(frame[1], struct zis_array_obj);
    if (zis_object_is_smallint(frame[2])) {
        const size_t index = zis_object_index_convert(
            zis_array_obj_length(self),
            zis_smallint_from_ptr(frame[2])
        );
        if (zis_unlikely(index == (size_t)-1))
            goto thr_index_out_of_range;
        zis_array_obj_remove(z, self, index);
    } else if (zis_object_type(frame[2]) == g->type_Int) {
    thr_index_out_of_range:
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, frame[2]
        ));
        return ZIS_THR;
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE,
            "position", frame[2]
        ));
        return ZIS_THR;
    }

    frame[0] = zis_object_from(g->val_nil);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_array_D_methods,
    { "+"           , &T_Array_M_operator_add      },
    { "[]"          , &T_Array_M_operator_get_elem },
    { "[]="         , &T_Array_M_operator_set_elem },
    { "=="          , &T_Array_M_operator_equ      },
    { "<=>"         , &T_Array_M_operator_cmp      },
    { "length"      , &T_Array_M_length            },
    { "to_string"   , &T_Array_M_to_string         },
    { "append"      , &T_Array_M_append            },
    { "pop"         , &T_Array_M_pop               },
    { "insert"      , &T_Array_M_insert            },
    { "remove"      , &T_Array_M_remove            },
);

ZIS_NATIVE_TYPE_DEF(
    Array,
    struct zis_array_obj, length,
    NULL, T_array_D_methods, NULL
);
