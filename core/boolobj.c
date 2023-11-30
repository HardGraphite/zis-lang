#include "boolobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

struct zis_bool_obj *_zis_bool_obj_new(struct zis_context *z, bool v) {
    struct zis_bool_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Bool, 0, 0),
        struct zis_bool_obj
    );
    self->_value = v;
    return self;
}

ZIS_NATIVE_TYPE_DEF(
    Bool,
    struct zis_bool_obj, _value,
    NULL, NULL, NULL
);
