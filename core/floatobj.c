#include "floatobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

struct zis_float_obj *zis_float_obj_new(struct zis_context *z, double val) {
    struct zis_float_obj *const self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Float),
        struct zis_float_obj
    );
    self->_value = val;
    return self;
}

ZIS_NATIVE_TYPE_DEF(
    Float,
    struct zis_float_obj, _value,
    NULL, NULL, NULL
);
