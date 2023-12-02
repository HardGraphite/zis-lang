#include "floatobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

void zis_float_obj_new(struct zis_context *z, struct zis_object **ret, double val) {
    struct zis_object *const obj = zis_objmem_alloc(z, z->globals->type_Float);
    *ret = obj;
    struct zis_float_obj *const self = zis_object_cast(obj, struct zis_float_obj);
    self->_value = val;
}

ZIS_NATIVE_TYPE_DEF(
    Float,
    struct zis_float_obj, _value,
    NULL, NULL, NULL
);
