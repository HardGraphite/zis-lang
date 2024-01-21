/// Invocations.

#pragma once

#include <stddef.h>

#include "funcobj.h"

struct zis_context;
struct zis_object;

/// Prepare the environment for an invocation and return the function to call.
/// On failure, makes an exception and stores to REG-0, and returns NULL.
/// The arguments (`argv`) must be a vector of objects on the stack.
/// `REG-0` must not be used for argument passing.
struct zis_func_obj *zis_invoke_prepare_va(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object **argv, size_t argc
);

/// Prepare the environment for an invocation and return the function to call.
/// On failure, makes an exception and stores to REG-0, and returns NULL.
/// The packed arguments (`packed_args`) must be a `Tuple` or an `Array.Slots`..
/// `REG-0` must not be used for argument passing.
struct zis_func_obj *zis_invoke_prepare_pa(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object *packed_args, size_t argc
);

/// Prepare the environment for an invocation and return the function to call.
/// On failure, makes an exception and stores to REG-0, and returns NULL.
/// `arg_regs` is an array of register indices that refers to the argument objects
/// in the previous callstack frame..
/// `REG-0` must not be used for argument passing.
struct zis_func_obj *zis_invoke_prepare_da(
    struct zis_context *z, struct zis_object *callable,
    const unsigned int arg_regs[], size_t argc
);

/// Clean up a frame created with `zis_invoke_prepare()`
/// and get the return value (also in REG-0).
struct zis_object *zis_invoke_cleanup(struct zis_context *z);

/// Call a function object.
/// `zis_invoke_prepare_*() should be called before calling this function;
/// `zis_invoke_cleanup()` should be called after calling this function.
/// The function `func` shall have been stored in the REG-0 in caller's frame.
/// If an exception is thrown, the stack trace will be updated.
int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func);
