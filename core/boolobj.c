#include "boolobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "exceptobj.h"
#include "stringobj.h"

struct zis_bool_obj *_zis_bool_obj_new(struct zis_context *z, bool v) {
    struct zis_bool_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Bool, 0, 0),
        struct zis_bool_obj
    );
    self->_value = v;
    return self;
}

#define assert_arg1_Bool(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Bool)))

ZIS_NATIVE_FUNC_DEF(T_Bool_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Bool:\'=='(other :: Bool) :: Bool
    Operator ==. */
    assert_arg1_Bool(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    const bool result = frame[1] == frame[2];
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Bool_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Bool:\'<=>'(other :: Bool) :: Int
    Operator <=>. */
    assert_arg1_Bool(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    if (frame[1] == frame[2]) {
        frame[0] = zis_smallint_to_ptr(0);
    } else if (zis_unlikely(!zis_object_type_is(frame[2], g->type_Bool))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "<=>", frame[1], frame[2]
        ));
        return ZIS_THR;
    } else {
        const bool self_value = zis_object_cast(frame[1], struct zis_bool_obj)->_value;
        frame[0] = zis_smallint_to_ptr(self_value ? 1 : -1);
    }
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Bool_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Bool:hash() :: Int
    Returns 0 for false and 1 for true. */
    assert_arg1_Bool(z);
    struct zis_object **frame = z->callstack->frame;
    const bool self_value = zis_object_cast(frame[1], struct zis_bool_obj)->_value;
    frame[0] = zis_smallint_to_ptr(self_value ? 1 : 0);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Bool_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Bool:to_string(?fmt) :: String
    Returns "false" for false and "true" for true. */
    assert_arg1_Bool(z);
    struct zis_object **frame = z->callstack->frame;
    const bool self_value = zis_object_cast(frame[1], struct zis_bool_obj)->_value;
    frame[0] = zis_object_from(
        self_value ?
            zis_string_obj_new(z, "true", 4) :
            zis_string_obj_new(z, "false", 5)
    );
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_bool_D_methods,
    { "=="          , &T_Bool_M_operator_equ },
    { "<=>"         , &T_Bool_M_operator_cmp },
    { "hash"        , &T_Bool_M_hash         },
    { "to_string"   , &T_Bool_M_to_string    },
);

ZIS_NATIVE_TYPE_DEF(
    Bool,
    struct zis_bool_obj, _value,
    NULL, T_bool_D_methods, NULL
);
