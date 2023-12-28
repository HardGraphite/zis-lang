#include "exceptobj.h"

#include <stdio.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "stringobj.h"

struct zis_exception_obj *zis_exception_obj_new(
    struct zis_context *z,
    struct zis_object *type, struct zis_object *what, struct zis_object *data
) {
    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
    tmp_regs[0] = type ? type : nil;
    tmp_regs[1] = what ? what : nil;
    tmp_regs[2] = data ? data : nil;
    struct zis_exception_obj *self = zis_exception_obj_new_r(z, tmp_regs);
    zis_callstack_frame_free_temp(z, 3);
    return self;
}

struct zis_exception_obj *zis_exception_obj_new_r(
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

struct zis_exception_obj *zis_exception_obj_format(
    struct zis_context *z,
    const char *type, struct zis_object *data,
    const char *what_fmt, ...
) {
    va_list ap;
    va_start(ap, what_fmt);
    struct zis_exception_obj *const self =
        zis_exception_obj_vformat(z, type, data, what_fmt, ap);
    va_end(ap);
    return self;
}

struct zis_exception_obj *zis_exception_obj_vformat(
    struct zis_context *z,
    const char *type, struct zis_object *data,
    const char *what_fmt, va_list what_args
) {
    // See `zis_exception_obj_new()`.

    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
    tmp_regs[0] = nil; // type
    tmp_regs[1] = nil; // what
    tmp_regs[2] = data ? data : nil;

    if (type) {
        struct zis_string_obj *const type_str_obj =
            zis_string_obj_new(z, type, (size_t)-1);
        assert(type_str_obj);
        tmp_regs[0] = zis_object_from(type_str_obj);
    }

    if (what_fmt) {
        char buffer[256];
        const int n = vsnprintf(buffer, sizeof buffer, what_fmt, what_args);
        if (zis_unlikely(n < 0))
            zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT);
        struct zis_string_obj *const what_str_obj =
            zis_string_obj_new(z, buffer, (size_t)n);
        assert(what_str_obj);
        tmp_regs[0] = zis_object_from(what_str_obj);
    }

    struct zis_exception_obj *const self = zis_exception_obj_new_r(z, tmp_regs);
    zis_callstack_frame_free_temp(z, 3);
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
