/// Invocations.

#pragma once

#include <stddef.h>

#include "funcobj.h" // struct zis_func_meta func_meta

struct zis_context;
struct zis_object;

/// Prepare a frame for an invocation and return the function to call.
/// On failure (like object is not invocable or argument number mismatches),
/// makes an exception and stores to REG-0, and returns NULL.
struct zis_func_obj *zis_invoke_prepare(
    struct zis_context *z,
    struct zis_object *callable, size_t argc
);

/// Pass arguments after `zis_invoke_prepare()` is called.
/// The arguments (`argv`) must be a vector of objects on the stack.
void zis_invoke_pass_args_v(
    struct zis_context *z, struct zis_func_meta func_meta,
    struct zis_object **argv, size_t argc
);

/// Pass arguments after `zis_invoke_prepare()` is called.
/// The packed arguments (`packed_args`) must be a `Tuple` or an `Array.Slots`.
void zis_invoke_pass_args_p(
    struct zis_context *z, struct zis_func_meta func_meta,
    struct zis_object *packed_args, size_t argc
);

/// Pass arguments after `zis_invoke_prepare()` is called.
/// `arg_regs` is an array of register indices that refers to the argument objects
/// in the previous callstack frame.
void zis_invoke_pass_args_d(
    struct zis_context *z, struct zis_func_meta func_meta,
    const unsigned int arg_regs[], size_t argc
);

/// Clean up a frame created with `zis_invoke_prepare()` and get the return value.
struct zis_object *zis_invoke_cleanup(struct zis_context *z);

/// Call a function object.
/// `zis_invoke_prepare()` and `zis_invoke_pass_args_*() should be called
/// before calling this function; `zis_invoke_cleanup()` should be called
/// after calling this function.
int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func);
