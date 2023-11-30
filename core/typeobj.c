#include "typeobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

/// Allocate but do not initialize.
static struct zis_type_obj *type_obj_alloc(struct zis_context *z, struct zis_object **ret) {
    struct zis_object *const obj =
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Type, 0, 0);
    *ret = obj;
    return zis_object_cast(obj, struct zis_type_obj);
}

void zis_type_obj_from_native_def(
    struct zis_context *z,
    struct zis_object **ret, const struct zis_native_type_def *def
) {
    struct zis_type_obj *const self = type_obj_alloc(z, ret);
    self->_slots_num = def->slots_num;
    self->_bytes_len = def->bytes_size;
    const bool extendable = self->_slots_num == (size_t)-1 || self->_bytes_len == (size_t)-1;
    self->_obj_size = extendable ? 0 : ZIS_OBJECT_HEAD_SIZE + self->_slots_num + self->_bytes_len;
    // TODO: create slot table, methods, and static members.
}

ZIS_NATIVE_FUNC_LIST_DEF(
    type_methods,
);

ZIS_NATIVE_FUNC_LIST_DEF(
    type_statics,
);

ZIS_NATIVE_TYPE_DEF(
    Type,
    struct zis_type_obj, _slots_num,
    NULL,
    ZIS_NATIVE_FUNC_LIST_VAR(type_methods),
    ZIS_NATIVE_FUNC_LIST_VAR(type_statics)
);
