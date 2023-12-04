#include "tupleobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

/// Allocate.
struct zis_tuple_obj *tuple_obj_alloc(
    struct zis_context *z, struct zis_object **ret, size_t n
) {
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Tuple, 1 + n, 0
    );
    *ret = obj;
    struct zis_tuple_obj *const self = zis_object_cast(obj, struct zis_tuple_obj);
    assert(zis_tuple_obj_length(self) == n);
    return self;
}

void zis_tuple_obj_new(
    struct zis_context *z, struct zis_object **ret,
    struct zis_object *v[], size_t n
) {
    struct zis_tuple_obj *const self = tuple_obj_alloc(z, ret, n);
    for (size_t i = 0; i < n; i++)
        self->_data[i] = v[i];
    zis_object_assert_no_write_barrier(self);
}

struct zis_tuple_obj *_zis_tuple_obj_new_empty(struct zis_context *z) {
    struct zis_object *o;
    return tuple_obj_alloc(z, &o, 0);
}

ZIS_NATIVE_TYPE_DEF_XS_NB(
    Tuple, struct zis_tuple_obj,
    NULL, NULL, NULL
);
