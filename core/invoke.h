/// Invocations.

#pragma once

#include <stddef.h>

struct zis_context;
struct zis_object;

/// Prepare a frame for an invocation and return the function to call.
/// On failure (like object is not invocable or argument number mismatches),
/// makes an exception and stores to REG-0, and returns NULL.
struct zis_func_obj *zis_invoke_prepare(
    struct zis_context *z,
    struct zis_object *callable, size_t arg_count
);

/// Clean up a frame created with `zis_invoke_prepare()` and get the return value.
struct zis_object *zis_invoke_cleanup(struct zis_context *z);

/// Call a function object.
/// `zis_invoke_prepare()` should be called and parameters should be prepared
/// before calling this function; `zis_invoke_cleanup()` should be called
/// after calling this function.
int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func);
