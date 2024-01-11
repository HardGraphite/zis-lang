#include "nilobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

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

ZIS_NATIVE_TYPE_DEF_NB(
    Nil,
    struct zis_nil_obj,
    NULL, NULL, NULL
);
