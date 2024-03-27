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
    struct zis_object **ret_to /* = NULL */, struct zis_object **argv, size_t argc
);

/// Prepare the environment for an invocation and return the function to call.
/// On failure, makes an exception and stores to REG-0, and returns NULL.
/// The packed arguments (`packed_args`) must be a `Tuple` or an `Array.Slots`..
/// `REG-0` must not be used for argument passing.
struct zis_func_obj *zis_invoke_prepare_pa(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object **ret_to /* = NULL */, struct zis_object *packed_args, size_t argc
);

/// Prepare the environment for an invocation and return the function to call.
/// On failure, makes an exception and stores to REG-0, and returns NULL.
/// `arg_regs` is an array of register indices that refers to the argument objects
/// in the previous callstack frame..
/// `REG-0` must not be used for argument passing.
struct zis_func_obj *zis_invoke_prepare_da(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object **ret_to /* = NULL */, const unsigned int arg_regs[], size_t argc
);

/// Call a function object and pop the frame.
/// `zis_invoke_prepare_*() should be called before calling this function;
/// otherwise, the new frame (and the arguments) shall have been prepared
/// and the function `func` shall have been stored in the REG-0 in the caller's frame.
/// If an exception is thrown, the stack trace will be updated.
int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func);
