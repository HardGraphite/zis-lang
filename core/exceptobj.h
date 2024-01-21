/// The Exception type.

#pragma once

#include <stdarg.h>

#include "attributes.h"
#include "compat.h"
#include "object.h"

struct zis_context;
struct zis_func_obj;
struct zis_stream_obj;

/// `Exception` object.
struct zis_exception_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *type; ///< Exception type.
    struct zis_object *what; ///< Message.
    struct zis_object *data; ///< Exception data.
    struct zis_object *_stack_trace; // nil or zis_array_obj{ func1, ip_off1, func2, ip_off2, ... }
};

/// Create an `Exception` object. `type`, `what`, and `data` are all optional.
struct zis_exception_obj *zis_exception_obj_new(
    struct zis_context *z,
    struct zis_object *type, struct zis_object *what, struct zis_object *data
);

/// Create an `Exception` object.
/// R = { [0] = type , [1] = what , [2] = data }.
struct zis_exception_obj *zis_exception_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 3]
);

/// Create an `Exception` with formatted string as field `what`.
/// Parameters `type`, `what_fmt`, and `data` are all optional.
zis_printf_fn_attrs(4, 5)
struct zis_exception_obj *zis_exception_obj_format(
    struct zis_context *z,
    const char *type, struct zis_object *data,
    zis_printf_fn_arg_fmtstr const char *restrict what_fmt, ...
);

/// See `zis_exception_obj_format()`.
struct zis_exception_obj *zis_exception_obj_vformat(
    struct zis_context *z,
    const char *type, struct zis_object *data,
    const char *restrict what_fmt, va_list what_args
);

/// Add a record to the stack trace.
void zis_exception_obj_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    struct zis_func_obj *func_obj, const void *ip
);

/// Get the number of entries in the stack trace.
size_t zis_exception_obj_stack_trace_length(
    struct zis_context *z, const struct zis_exception_obj *self
);

/// Traverse the stack trace.
int zis_exception_obj_walk_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    int (*fn)(unsigned int index, struct zis_func_obj *func_obj, unsigned int instr_offset, void *arg),
    void *fn_arg
);

/// Print the stack trace. The `out_stream` is optional.
int zis_exception_obj_print_stack_trace(
    struct zis_context *z, struct zis_exception_obj *self,
    struct zis_stream_obj *out_stream /* = NULL */
);

/// Print the exception. The `out_stream` is optional.
int zis_exception_obj_print(
    struct zis_context *z, struct zis_exception_obj *self,
    struct zis_stream_obj *out_stream /* = NULL */
);
