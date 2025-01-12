#include "exceptobj.h"

#include <stdio.h>
#include <string.h>

#include "context.h"
#include "globals.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"

#include "arrayobj.h"
#include "funcobj.h"
#include "streamobj.h"
#include "stringobj.h"
#include "symbolobj.h"

struct zis_exception_obj *zis_exception_obj_new(
    struct zis_context *z,
    struct zis_object *type, struct zis_object *what, struct zis_object *data
) {
    zis_locals_decl(
        z, args,
        struct zis_object *type, *what, *data;
    );
    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    args.type = type ? type : nil;
    args.what = what ? what : nil;
    args.data = data ? data : nil;

    struct zis_exception_obj *const self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Exception),
        struct zis_exception_obj
    );
    self->type = args.type;
    self->what = args.what;
    self->data = args.data;
    self->_stack_trace = zis_object_from(z->globals->val_nil);
    zis_object_write_barrier(self, args.type);
    zis_object_write_barrier(self, args.what);
    zis_object_write_barrier(self, args.data);

    zis_locals_drop(z, args);
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

    zis_locals_decl(
        z, args,
        struct zis_object *type, *what, *data;
    );
    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    args.type = nil;
    args.what = nil;
    args.data = data ? data : nil;

    if (type) {
        struct zis_symbol_obj *const type_sym_obj =
            zis_symbol_registry_get(z, type, (size_t)-1);
        assert(type_sym_obj);
        args.type = zis_object_from(type_sym_obj);
    }

    if (what_fmt) {
        char buffer[256];
        const int n = vsnprintf(buffer, sizeof buffer, what_fmt, what_args);
        if (zis_unlikely(n < 0))
            zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT);
        struct zis_string_obj *const what_str_obj =
            zis_string_obj_new(z, buffer, (size_t)n);
        assert(what_str_obj);
        args.what = zis_object_from(what_str_obj);
    }

    struct zis_exception_obj *const self =
        zis_exception_obj_new(z, args.type, args.what, args.data);
    zis_locals_drop(z, args);

    return self;
}

static void _represent_type_of_obj(
    struct zis_context *z, struct zis_object *obj,
    char *restrict buf, size_t buf_sz
) {
    struct zis_type_obj *obj_type = zis_object_type_1(obj);
    if (!obj_type)
        obj_type = z->globals->type_Int;
    struct zis_string_obj *represent =
        zis_context_guess_variable_name(z, zis_object_from(obj_type));
    if (represent) {
        const size_t n = zis_string_obj_to_u8str(represent, buf, buf_sz - 1);
        if (n != (size_t)-1) {
            buf[n] = 0;
            return;
        }
    }
    assert(buf_sz > 3);
    memcpy(buf, "??", 3);
}

struct zis_exception_obj *zis_exception_obj_format_common(
    struct zis_context *z, int _template, ...
) {
    const enum zis_exception_obj_format_common_template template =
        (enum zis_exception_obj_format_common_template)_template;
    zis_locals_decl(
        z, var,
        struct zis_exception_obj *result;
        struct zis_object *args[2];
    );
    zis_locals_zero(var);
    va_list ap;
    va_start(ap, _template);

    switch (template) {
    case ZIS_EXC_FMT_UNSUPPORTED_OPERATION_UN: {
        char buffer[1][80];
        const char *op = va_arg(ap, const char *);
        _represent_type_of_obj(z, va_arg(ap, struct zis_object *), buffer[0], sizeof buffer[0]);
        var.result = zis_exception_obj_format(
            z, "type", NULL, "unsupported operation: %s %s",
            op, buffer[0]
        );
        break;
    }

    case ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN: {
        char buffer[2][80];
        const char *op = va_arg(ap, const char *);
        var.args[0] = va_arg(ap, struct zis_object *);
        var.args[1] = va_arg(ap, struct zis_object *);
        _represent_type_of_obj(z, var.args[0], buffer[0], sizeof buffer[0]);
        _represent_type_of_obj(z, var.args[1], buffer[1], sizeof buffer[1]);
        var.result = zis_exception_obj_format(
            z, "type", NULL, "unsupported operation: %s %s %s",
            buffer[0], op, buffer[1]
        );
        break;
    }

    case ZIS_EXC_FMT_UNSUPPORTED_OPERATION_SUBS: {
        char buffer[2][80];
        const char *op = va_arg(ap, const char *);
        var.args[0] = va_arg(ap, struct zis_object *);
        var.args[1] = va_arg(ap, struct zis_object *);
        _represent_type_of_obj(z, var.args[0], buffer[0], sizeof buffer[0]);
        _represent_type_of_obj(z, var.args[1], buffer[1], sizeof buffer[1]);
        var.result = zis_exception_obj_format(
            z, "type", NULL, "unsupported operation: %s %c %s %s",
            buffer[0], op[0], buffer[1], op + 1
        );
        break;
    }

    case ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE: {
        char arg_type_buf[80];
        const char *arg_name = va_arg(ap, const char *);
        _represent_type_of_obj(z, va_arg(ap, struct zis_object *), arg_type_buf, sizeof arg_type_buf);
        var.result = zis_exception_obj_format(
            z, "type", NULL, "argument %s cannot be %s",
            arg_name, arg_type_buf
        );
        break;
    }

    case ZIS_EXC_FMT_INDEX_OUT_OF_RANGE:
        var.args[0] = va_arg(ap, struct zis_object *);
        var.result = zis_exception_obj_format(z, "key", var.args[0], "index out of range");
        break;

    case ZIS_EXC_FMT_KEY_NOT_FOUND:
        var.args[0] = va_arg(ap, struct zis_object *);
        var.result = zis_exception_obj_format(z, "key", var.args[0], "key not found");
        break;

    case ZIS_EXC_FMT_NAME_NOT_FOUND: {
        char buffer[80];
        const char *what = va_arg(ap, char *);
        var.args[0] = va_arg(ap, struct zis_object *);
        assert(zis_object_type_is(zis_object_from(var.args[0]), z->globals->type_Symbol));
        struct zis_symbol_obj *name_sym = zis_object_cast(var.args[0], struct zis_symbol_obj);
        if (zis_symbol_obj_data_size(name_sym) <= sizeof buffer) {
            const size_t n = zis_symbol_obj_data_size(name_sym);
            memcpy(buffer, zis_symbol_obj_data(name_sym), n);
            var.result = zis_exception_obj_format(z, "key", var.args[0], "no %s named %.*s", what, (int)n, buffer);
        } else {
            var.result = zis_exception_obj_format(z, "key", var.args[0], "no such a %s", what);
        }
        break;
    }

    default:
        var.result = NULL;
    }

    va_end(ap);
    zis_locals_drop(z, var);
    return var.result;
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
        ip_offset = (unsigned int)((zis_func_obj_bytecode_word_t *)ip - func_p);

    zis_locals_decl(
        z, var,
        struct zis_exception_obj *self;
        struct zis_array_obj *stack_trace;
        struct zis_func_obj *func_obj;
    );
    var.self = self;
    var.stack_trace = zis_object_cast(self->_stack_trace, struct zis_array_obj);
    var.func_obj = func_obj;

    if (!zis_object_type_is(self->_stack_trace, z->globals->type_Array)) {
        struct zis_array_obj *x = zis_array_obj_new(z, NULL, 0);
        var.stack_trace = x;
        var.self->_stack_trace = zis_object_from(x);
        zis_object_write_barrier(var.self, x);
    }

    zis_array_obj_append(z, var.stack_trace, zis_object_from(var.func_obj));
    zis_array_obj_append(z, var.stack_trace, zis_smallint_to_ptr((zis_smallint_t)ip_offset));

    zis_locals_drop(z, var);
}

size_t zis_exception_obj_stack_trace_length(
    struct zis_context *z, const struct zis_exception_obj *self
) {
    if (!zis_object_type_is(self->_stack_trace, z->globals->type_Array))
        return 0;
    const size_t n =
        zis_array_obj_length(zis_object_cast(self->_stack_trace, struct zis_array_obj));
    assert(!(n & 1));
    return n / 2;
}

int zis_exception_obj_walk_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    int (*fn)(struct zis_context *, unsigned int index, struct zis_func_obj *func_obj, unsigned int instr_offset, void *arg),
    void *fn_arg
) {
    const size_t n = zis_exception_obj_stack_trace_length(z, self);
    if (!n)
        return 0;

    assert(zis_object_type_is(self->_stack_trace, z->globals->type_Array));
    zis_locals_decl_1(z, var, struct zis_array_obj *stack_trace);
    var.stack_trace = zis_object_cast(self->_stack_trace, struct zis_array_obj);

    int fn_ret = 0;
    for (unsigned int i = 0; i < n; i++) {
        struct zis_object *const *const v = zis_array_obj_data(var.stack_trace);
        assert(zis_object_type_is(v[i * 2], z->globals->type_Function));
        assert(zis_object_is_smallint(v[i * 2 + 1]));
        fn_ret = fn(
            z,
            i,
            zis_object_cast(v[i * 2], struct zis_func_obj),
            (unsigned int)zis_smallint_from_ptr(v[i * 2 + 1]),
            fn_arg
        );
        if (fn_ret)
            break;
    }

    zis_locals_drop(z, var);
    return fn_ret;
}

static int _print_stack_trace_fn(
    struct zis_context *z, unsigned int index,
    struct zis_func_obj *func_obj, unsigned int instr_offset, void *_arg
) {
    struct zis_stream_obj *out_stream = _arg;
    zis_unused_var(out_stream);
    char buffer[80];
    snprintf(buffer, sizeof buffer, "[%02u] ", index);
    fputs(buffer, stdout);
    do {
        struct zis_string_obj *func_name =
            zis_context_guess_variable_name(z, zis_object_from(func_obj));
        if (func_name) {
            size_t n = zis_string_obj_to_u8str(func_name, buffer, sizeof buffer - 1);
            if (n != (size_t)-1) {
                buffer[n] = 0;
                break;
            }
        }
        strcpy(buffer, "??");
    } while (0);
    fputs(buffer, stdout);
    snprintf(buffer, sizeof buffer, " (+%u)\n", instr_offset);
    fputs(buffer, stdout);
    // TODO: print source location.
    // TODO: print to the `out_stream`.
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
    if (zis_object_type_is(self->what, z->globals->type_String)) {
        char buffer[80];
        buffer[zis_string_obj_to_u8str(zis_object_cast(self->what, struct zis_string_obj), buffer, sizeof buffer)] = 0;
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

static const char *const T_Exception_D_fields[] = {
    "type",
    "what",
    "data",
    NULL, // _stack_trace
};

ZIS_NATIVE_TYPE_DEF_NB(
    Exception, struct zis_exception_obj,
    T_Exception_D_fields, NULL, NULL
);
