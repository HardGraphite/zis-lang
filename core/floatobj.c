#include "floatobj.h"

#include <fenv.h>
#include <math.h>
#include <stdio.h>

#include "algorithm.h"
#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "exceptobj.h"
#include "intobj.h"
#include "stringobj.h"
#include "tupleobj.h"

struct zis_float_obj *zis_float_obj_new(struct zis_context *z, double val) {
    struct zis_float_obj *const self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Float),
        struct zis_float_obj
    );
    self->_value = val;
    return self;
}

#define assert_arg1_Float(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Float)))

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_pos, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:\'+#'() :: Float
    Returns `+ self`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    frame[0] = frame[1];
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_neg, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:\'-#'() :: Float
    Returns `- self`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = -self_value;
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

/// Read the second argument as an Int or a Float.
static bool float_obj_bin_op_other_value(struct zis_context *z, double *value) {
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_object *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    double other_value;
    if (!other_type)
        other_value = (double)zis_smallint_from_ptr(other_v);
    else if(other_type == g->type_Float)
        other_value = zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj));
    else if (other_type == g->type_Int)
        other_value = zis_int_obj_value_f(zis_object_cast(other_v, struct zis_int_obj));
    else
        return false;

    *value = other_value;
    return true;
}

zis_cold_fn zis_noinline
static int float_obj_bin_op_unsupported_error(struct zis_context *z, const char *restrict op) {
    struct zis_object **frame = z->callstack->frame;
    frame[0] = zis_object_from(zis_exception_obj_format_common(
        z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
        op, frame[1], frame[2]
    ));
    return ZIS_THR;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_add, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'+'(other :: Float|Int) :: Float
    Returns `self + other`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "+");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = self_value + other_value;
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_sub, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'-'(other :: Float|Int) :: Float
    Returns `self - other`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "-");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = self_value - other_value;
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_mul, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'*'(other :: Float|Int) :: Float
    Returns `self * other`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "*");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = self_value * other_value;
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_div, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'/'(other :: Float|Int) :: Float
    Returns `self / other`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "/");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = self_value / other_value;
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_rem, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'%'(other :: Float|Int) :: Float
    Returns `self % other`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "%");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = fmod(self_value, other_value);
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_pow, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'**'(other :: Float|Int) :: Float
    Returns `self ** other`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "**");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = pow(self_value, other_value);
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'=='(other :: Float|Int) :: Bool
    Operator ==. */
    assert_arg1_Float(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    bool result;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    struct zis_object *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type)
        result = self_value == (double)zis_smallint_from_ptr(other_v);
    else if(other_type == g->type_Float)
        result = self_value == zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj));
    else if (other_type == g->type_Int)
        result = self_value == zis_int_obj_value_f(zis_object_cast(other_v, struct zis_int_obj));
    else
        result = false;

    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:\'<=>'(other :: Float|Int) :: Int
    Operator <=>. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "<=>");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const zis_smallint_t cmp_result =
        self_value == other_value ? 0 : self_value < other_value ? -1 : 1;
    frame[0] = zis_smallint_to_ptr(cmp_result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:hash() :: Int
    Generates a hash code for the floating-point number. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;

    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const size_t hash =  zis_hash_float(self_value);

    frame[0] = zis_smallint_to_ptr((zis_smallint_t)hash);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Float:to_string(?fmt) :: String
    Generates a string representation of the floating-point number. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;

    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    char buffer[80];
    snprintf(buffer, sizeof buffer, "%f", self_value);
    // TODO: support formatting-specifications

    frame[0] = zis_object_from(zis_string_obj_new(z, buffer, (size_t)-1));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_is_nan, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:is_nan() :: Bool
    Determines whether this is a NaN value. */
    assert_arg1_Float(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const bool result = isnan(self_value);
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_is_inf, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:is_inf() :: Bool
    Determines whether this is an infinity value. */
    assert_arg1_Float(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const bool result = isinf(self_value);
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_is_neg, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:is_neg() :: Bool
    Determines whether this is a negative value. */
    assert_arg1_Float(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const bool result = self_value <= -0.0;
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_abs, z, {1, 0, 1}) {
    /*#DOCSTR# func Float:abs() :: Float
    Computes the absolute value. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double result = fabs(self_value);
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_div, z, {2, 0, 2}) {
    /*#DOCSTR# func Float:div(other :: Float|Int) :: Tuple[Float, Float]
    Returns `(self / other, self % other)`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    double other_value;
    if (zis_unlikely(!float_obj_bin_op_other_value(z, &other_value)))
        return float_obj_bin_op_unsupported_error(z, "/");
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    const double rem = fmod(self_value, other_value), quot = (self_value - rem) / other_value;
    frame[1] = zis_object_from(zis_float_obj_new(z, quot));
    frame[2] = zis_object_from(zis_float_obj_new(z, rem));
    frame[0] = zis_object_from(zis_tuple_obj_new(z, frame + 1, 2));
    return ZIS_OK;
}

static double _round(double x) {
    const int round = fegetround();
    if (round != FE_TONEAREST)
        fesetround(FE_TONEAREST);
    const double result = nearbyint(x);
    if (!(round == FE_TONEAREST || round < 0))
        fesetround(round);
    return result;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_round, z, {1, 1, 2}) {
    /*#DOCSTR# func Float:round(?a :: Float|Int) :: Float
    Rounds to the nearest integer or the nearest multiple of argument `a`.
    A number of the form ?.??5 will be rounded to the nearest even integer,
    aka. statistician's rounding or bankers' rounding. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    double a, result;
    if (float_obj_bin_op_other_value(z, &a))
        result = _round(self_value / a) * a;
    else
        result = _round(self_value);
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_floor, z, {1, 1, 2}) {
    /*#DOCSTR# func Float:floor(?a :: Float|Int) :: Float
    Finds the largest integer not greater than this value.
    Argument `a` is similar to that in `Float:round()`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    double a, result;
    if (float_obj_bin_op_other_value(z, &a))
        result = floor(self_value / a) * a;
    else
        result = floor(self_value);
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Float_M_ceil, z, {1, 1, 2}) {
    /*#DOCSTR# func Float:ceil(?a :: Float|Int) :: Float
    Finds the smallest integer not less than this value.
    Argument `a` is similar to that in `Float:round()`. */
    assert_arg1_Float(z);
    struct zis_object **frame = z->callstack->frame;
    const double self_value =
        zis_float_obj_value(zis_object_cast(frame[1], struct zis_float_obj));
    double a, result;
    if (float_obj_bin_op_other_value(z, &a))
        result = ceil(self_value / a) * a;
    else
        result = ceil(self_value);
    frame[0] = zis_object_from(zis_float_obj_new(z, result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_float_D_methods,
    { "+#"         , &T_Float_M_operator_pos  },
    { "-#"         , &T_Float_M_operator_neg  },
    { "+"          , &T_Float_M_operator_add  },
    { "-"          , &T_Float_M_operator_sub  },
    { "*"          , &T_Float_M_operator_mul  },
    { "/"          , &T_Float_M_operator_div  },
    { "%"          , &T_Float_M_operator_rem  },
    { "**"         , &T_Float_M_operator_pow  },
    { "=="         , &T_Float_M_operator_equ  },
    { "<=>"        , &T_Float_M_operator_cmp  },
    { "hash"       , &T_Float_M_hash          },
    { "to_string"  , &T_Float_M_to_string     },
    { "is_nan"     , &T_Float_M_is_nan        },
    { "is_inf"     , &T_Float_M_is_inf        },
    { "is_neg"     , &T_Float_M_is_neg        },
    { "abs"        , &T_Float_M_abs           },
    { "div"        , &T_Float_M_div           },
    { "round"      , &T_Float_M_round         },
    { "floor"      , &T_Float_M_floor         },
    { "ceil"       , &T_Float_M_ceil          },
);

ZIS_NATIVE_VAR_DEF_LIST(
    T_float_D_statics,
    { "NAN"       , { 'f', .f = NAN         } },
);

ZIS_NATIVE_TYPE_DEF(
    Float,
    struct zis_float_obj, _value,
    NULL, T_float_D_methods, T_float_D_statics
);
