#include "rangeobj.h"

#include <stdio.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "exceptobj.h"
#include "stringobj.h"

struct zis_range_obj *zis_range_obj_new(
    struct zis_context *z,
    zis_ssize_t begin, zis_ssize_t end
) {
    struct zis_object *const obj = zis_objmem_alloc(z, z->globals->type_Range);
    struct zis_range_obj *const range = zis_object_cast(obj, struct zis_range_obj);
    range->begin = begin, range->end = end;
    return range;
}

struct zis_range_obj *zis_range_obj_new_ob(
    struct zis_context *z,
    struct zis_object *begin_obj, struct zis_object *end_obj, bool exclude_end
) {
    if (!zis_object_is_smallint(begin_obj)) {
        zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
            z, NULL, begin_obj, "illegal index"
        )));
        return NULL;
    }
    if (!zis_object_is_smallint(end_obj)) {
        zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
            z, NULL, end_obj, "illegal index"
        )));
        return NULL;
    }
    zis_ssize_t begin =  zis_smallint_from_ptr(begin_obj), end = zis_smallint_from_ptr(end_obj);
    return zis_range_obj_new(z, begin, exclude_end ? end - 1 : end);
}

#define assert_arg1_Range(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Range)))

ZIS_NATIVE_FUNC_DEF(T_Range_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Range:to_string(?fmt) :: String
    */
    assert_arg1_Range(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_range_obj *range = zis_object_cast(frame[1], struct zis_range_obj);
    char buffer[64];
    snprintf(buffer, sizeof buffer, "(%" ZIS_SSIZE_PRIi "...%" ZIS_SSIZE_PRIi ")", range->begin, range->end);
    frame[0] = zis_object_from(zis_string_obj_new(z, buffer, (size_t)-1));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_range_D_methods,
    { "to_string"  , &T_Range_M_to_string     },
);

ZIS_NATIVE_TYPE_DEF(
    Range,
    struct zis_range_obj, begin,
    NULL, T_range_D_methods, NULL
);
