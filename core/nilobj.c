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

ZIS_NATIVE_FUNC_DEF(T_Nil_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Nil:\'=='(other :: Nil) :: Bool
    Operator ==. */
    assert_arg1_Nil(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const bool result = frame[1] == frame[2];
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Nil_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Nil:\'<=>'(other :: Nil) :: Int
    Operator <=>. */
    assert_arg1_Nil(z);
    struct zis_object **frame = z->callstack->frame;
    if (frame[1] != frame[2]) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "<=>", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    frame[0] = zis_smallint_to_ptr(0);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Nil_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Nil:hash() :: Int
    Returns -1. */
    assert_arg1_Nil(z);
    zis_context_set_reg0(z, zis_smallint_to_ptr(-1));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Nil_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Nil:to_string(?fmt) :: String
    Returns "nil". */
    assert_arg1_Nil(z);
    zis_context_set_reg0(z, zis_object_from(zis_string_obj_new(z, "nil", 3)));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_Nil_D_methods,
    { "=="          , &T_Nil_M_operator_equ   },
    { "<=>"         , &T_Nil_M_operator_cmp   },
    { "hash"        , &T_Nil_M_hash           },
    { "to_string"   , &T_Nil_M_to_string      },
);

ZIS_NATIVE_TYPE_DEF_NB(
    Nil,
    struct zis_nil_obj,
    NULL, T_Nil_D_methods, NULL
);
