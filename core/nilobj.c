#include "nilobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "exceptobj.h"
#include "stringobj.h"

struct zis_nil_obj {
    ZIS_OBJECT_HEAD
};

struct zis_nil_obj *_zis_nil_obj_new(struct zis_context *z) {
    struct zis_nil_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Nil, 0, 0),
        struct zis_nil_obj
    );
    return self;
}

#define assert_arg1_Nil(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Nil)))

static int T_Nil_M_operator_equ(struct zis_context *z) {
#define T_Nil_Md_operator_equ { "==", {2, 0, 2}, T_Nil_M_operator_equ }
    /*#DOCSTR# func Nil:\'=='(other) :: Bool
    Operator ==. */
    assert_arg1_Nil(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const bool result = frame[1] == frame[2];
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

static int T_Nil_M_operator_cmp(struct zis_context *z) {
#define T_Nil_Md_operator_cmp { "<=>", {2, 0, 2}, T_Nil_M_operator_cmp }
    /*#DOCSTR# func Nil:\'<=>'(other) :: Int
    Operator <=>. */
    assert_arg1_Nil(z);
    struct zis_object **frame = z->callstack->frame;
    if (frame[1] != frame[2]) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            (struct zis_exception_obj_format_common_char4){"<=>"}, frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    frame[0] = zis_smallint_to_ptr(0);
    return ZIS_OK;
}

static int T_Nil_M_hash(struct zis_context *z) {
#define T_Nil_Md_hash { "hash", {1, 0, 1}, T_Nil_M_hash }
    /*#DOCSTR# func Nil:hash() :: Int
    Returns -1. */
    assert_arg1_Nil(z);
    zis_context_set_reg0(z, zis_smallint_to_ptr(-1));
    return ZIS_OK;
}

static int T_Nil_M_to_string(struct zis_context *z) {
#define T_Nil_Md_to_string { "to_string", {1, 1, 2}, T_Nil_M_to_string }
    /*#DOCSTR# func Nil:to_string(?fmt) :: String
    Returns "nil". */
    assert_arg1_Nil(z);
    zis_context_set_reg0(z, zis_object_from(zis_string_obj_new(z, "nil", 3)));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_LIST_DEF(
    nil_methods,
    T_Nil_Md_operator_equ,
    T_Nil_Md_operator_cmp,
    T_Nil_Md_hash,
    T_Nil_Md_to_string,
);

ZIS_NATIVE_TYPE_DEF_NB(
    Nil,
    struct zis_nil_obj,
    NULL,
    ZIS_NATIVE_FUNC_LIST_VAR(nil_methods),
    NULL
);
