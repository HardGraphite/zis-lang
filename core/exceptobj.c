#include "exceptobj.h"

#include <stdio.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "arrayobj.h"
#include "funcobj.h"
#include "streamobj.h"
#include "stringobj.h"
#include "symbolobj.h"

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
    self->_stack_trace = zis_object_from(z->globals->val_nil);
    zis_object_write_barrier(self, regs[0]);
    zis_object_write_barrier(self, regs[1]);
    zis_object_write_barrier(self, regs[2]);

    return self;
}

struct zis_exception_obj *zis_exception_obj_format(
    struct zis_context *z,
    const char *type, struct zis_object *data,
    const char *restrict what_fmt, ...
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
    const char *restrict what_fmt, va_list what_args
) {
    // See `zis_exception_obj_new()`.

    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
    tmp_regs[0] = nil; // type
    tmp_regs[1] = nil; // what
    tmp_regs[2] = data ? data : nil;

    if (type) {
        struct zis_symbol_obj *const type_sym_obj =
            zis_symbol_registry_get(z, type, (size_t)-1);
        assert(type_sym_obj);
        tmp_regs[0] = zis_object_from(type_sym_obj);
    }

    if (what_fmt) {
        char buffer[256];
        const int n = vsnprintf(buffer, sizeof buffer, what_fmt, what_args);
        if (zis_unlikely(n < 0))
            zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT);
        struct zis_string_obj *const what_str_obj =
            zis_string_obj_new(z, buffer, (size_t)n);
        assert(what_str_obj);
        tmp_regs[1] = zis_object_from(what_str_obj);
    }

    struct zis_exception_obj *const self = zis_exception_obj_new_r(z, tmp_regs);
    zis_callstack_frame_free_temp(z, 3);
    return self;
}

void zis_exception_obj_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    struct zis_func_obj *func_obj, const void *ip
) {
    unsigned int ip_offset;
    zis_func_obj_bytecode_word_t *func_p = func_obj->bytecode;
    zis_func_obj_bytecode_word_t *func_p_end = func_p + zis_func_obj_bytecode_length(func_obj);
    if (ip < (void *)func_p || ip >= (void *)func_p_end)
        ip_offset = 0;
    else
        ip_offset = (zis_func_obj_bytecode_word_t *)ip - func_p;

    size_t tmp_regs_n = 3;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, tmp_regs_n);
    tmp_regs[0] = zis_object_from(self), tmp_regs[1] = zis_object_from(func_obj),
    tmp_regs[2] = zis_smallint_to_ptr((zis_smallint_t)ip_offset);
    if (zis_object_type(self->_stack_trace) != z->globals->type_Array) {
        struct zis_object *x = zis_object_from(zis_array_obj_new(z, tmp_regs + 1, 2));
        zis_object_cast(tmp_regs[0], struct zis_exception_obj)->_stack_trace = x;
        zis_object_write_barrier(tmp_regs[0], x);
    } else {
        struct zis_array_obj *x = zis_object_cast(self->_stack_trace, struct zis_array_obj);
        zis_array_obj_append(z, x, tmp_regs[1]);
        x = zis_object_cast(zis_object_cast(tmp_regs[0], struct zis_exception_obj)->_stack_trace, struct zis_array_obj);
        zis_array_obj_append(z, x, tmp_regs[2]);
    }
    zis_callstack_frame_free_temp(z, tmp_regs_n);
}

size_t zis_exception_obj_stack_trace_length(
    struct zis_context *z, const struct zis_exception_obj *self
) {
    if (zis_object_type(self->_stack_trace) != z->globals->type_Array)
        return 0;
    const size_t n =
        zis_array_obj_length(zis_object_cast(self->_stack_trace, struct zis_array_obj));
    assert(!(n & 1));
    return n / 2;
}

int zis_exception_obj_walk_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    int (*fn)(unsigned int index, struct zis_func_obj *func_obj, unsigned int instr_offset, void *arg),
    void *fn_arg
) {
    const size_t n = zis_exception_obj_stack_trace_length(z, self);
    if (!n)
        return 0;
    int fn_ret = 0;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 1);
    tmp_regs[0] = self->_stack_trace;
    for (size_t i = 0; i < n; i++) {
        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Array);
        struct zis_object *const *const v =
            zis_array_obj_data(zis_object_cast(tmp_regs[0], struct zis_array_obj));
        assert(zis_object_type(v[i * 2]) == z->globals->type_Function);
        assert(zis_object_is_smallint(v[i * 2 + 1]));
        fn_ret = fn(
            i,
            zis_object_cast(v[i * 2], struct zis_func_obj),
            (unsigned int)zis_smallint_from_ptr(v[i * 2 + 1]),
            fn_arg
        );
        if (fn_ret)
            break;
    }
    zis_callstack_frame_free_temp(z, 1);
    return fn_ret;
}

static int _print_stack_trace_fn(
    unsigned int index, struct zis_func_obj *func_obj, unsigned int instr_offset, void *_arg
) {
    struct zis_stream_obj *out_stream = _arg;
    zis_unused_var(out_stream);
    char buffer[80];
    // TODO: print pretty function name and source position.
    snprintf(buffer, sizeof buffer, " [%u] <Function@%p>+%u\n", index, (void *)func_obj, instr_offset);
    // TODO: print to the `out_stream`.
    fputs(buffer, stdout);
    return 0;
}

int zis_exception_obj_print_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    struct zis_stream_obj *out_stream /* = NULL */
) {
    return zis_exception_obj_walk_stack_trace(z, self, _print_stack_trace_fn, out_stream);
}

int zis_exception_obj_print(
    struct zis_context *z, struct zis_exception_obj *self,
    struct zis_stream_obj *out_stream /* = NULL */
) {
    // TODO: print to the `out_stream`.
    fputs("Exception: ", stdout);
    if (zis_object_type(self->what) == z->globals->type_String) {
        char buffer[80];
        buffer[zis_string_obj_value(zis_object_cast(self->what, struct zis_string_obj), buffer, sizeof buffer)] = 0;
        fputs(buffer, stdout);
    }
    // TODO: print the `type` and `data` fields.
    fputc('\n', stdout);
    if (zis_exception_obj_stack_trace_length(z, self)) {
        fputs("Stack trace:\n", stdout);
        return zis_exception_obj_print_stack_trace(z, self, out_stream);
    }
    return 0;
}

ZIS_NATIVE_NAME_LIST_DEF(
    Exception_slots,
    "type",
    "what",
    "data",
    NULL,
);

ZIS_NATIVE_TYPE_DEF_NB(
    Exception, struct zis_exception_obj,
    ZIS_NATIVE_NAME_LIST_VAR(Exception_slots),
    NULL,
    NULL
);
