#include "exceptobj.h"

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

struct zis_exception_obj *zis_exception_obj_new(
    struct zis_context *z,
    struct zis_object *type, struct zis_object *what, struct zis_object *data
) {
    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
    tmp_regs[0] = type ? type : nil;
    tmp_regs[1] = what ? what : nil;
    tmp_regs[2] = data ? data : nil;
    struct zis_exception_obj *self = zis_exception_obj_Mi_new(z, tmp_regs);
    zis_callstack_frame_free_temp(z, 3);
    return self;
}

struct zis_exception_obj *zis_exception_obj_Mi_new(
    struct zis_context *z, struct zis_object *regs[ZIS_PARAMARRAY_STATIC 3]
) {
    // ~~ regs[0] = type, regs[1] = what, regs[2] = data ~~

    struct zis_exception_obj *const self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Exception),
        struct zis_exception_obj
    );
    self->type = regs[0];
    self->what = regs[1];
    self->data = regs[2];

    return self;
}

ZIS_NATIVE_NAME_LIST_DEF(
    Exception_slots,
    "type",
    "what",
    "data",
);

ZIS_NATIVE_TYPE_DEF_NB(
    Exception, struct zis_exception_obj,
    ZIS_NATIVE_NAME_LIST_VAR(Exception_slots),
    NULL,
    NULL
);
