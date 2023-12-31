#include "invoke.h"

#include "attributes.h"
#include "context.h"
#include "globals.h"
#include "object.h"
#include "stack.h"

#include "exceptobj.h"
#include "funcobj.h"
#include "typeobj.h"

/* ----- common utilities --------------------------------------------------- */

zis_noinline zis_cold_fn static struct zis_exception_obj *
format_error_type(struct zis_context *z, struct zis_object *fn) {
    return zis_exception_obj_format(z, "type", fn, "not callable");
}

zis_noinline zis_cold_fn static struct zis_exception_obj *
format_error_argc(struct zis_context *z, struct zis_func_obj *fn, size_t argc) {
    const zis_func_obj_func_meta_t func_meta = fn->meta;
    return zis_exception_obj_format(
        z, "type", zis_object_from(fn),
        "wrong number of arguments (given %zu, expected %s%u)",
        argc, func_meta.va ? "at least " : "", func_meta.na
    );
}

/* ----- bytecode execution ------------------------------------------------- */

zis_hot_fn static int exec_bytecode(
    struct zis_context *z, struct zis_func_obj *func_obj
) {
    zis_unused_var(z), zis_unused_var(func_obj);
    zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
}

/* ----- public functions --------------------------------------------------- */

struct zis_func_obj *zis_invoke_prepare(
    struct zis_context *z,
    struct zis_object *callable, size_t arg_count
) {
    struct zis_type_obj *const func_type = z->globals->type_Function;

    if (zis_unlikely(zis_object_type(callable) != func_type)) {
        z->callstack->frame[0] = zis_object_from(format_error_type(z, callable));
        return NULL;
    }

    struct zis_func_obj *func_obj = zis_object_cast(callable, struct zis_func_obj);
    const zis_func_obj_func_meta_t func_meta = func_obj->meta;
    if (zis_unlikely(arg_count != func_meta.na)) {
        if (!(func_meta.va && arg_count > func_meta.na)) {
            z->callstack->frame[0] =
                zis_object_from(format_error_argc(z, func_obj, arg_count));
            return NULL;
        }
    }

    assert(func_meta.nr >= 1U + func_meta.na + func_meta.va ? 1U : 0U);
    zis_callstack_enter(z->callstack, func_meta.nr, NULL);

    return func_obj;
}

struct zis_object *zis_invoke_cleanup(struct zis_context *z) {
    struct zis_callstack *const stack = z->callstack;
    struct zis_object *const ret_val = stack->frame[0];
    assert(!zis_callstack_frame_info(stack)->return_ip);
    zis_callstack_leave(stack);
    return ret_val;
}

int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func) {
    const zis_native_func_t c_func = func->native;
    if (zis_unlikely(c_func))
        return c_func(z);
    return exec_bytecode(z, func);
}
