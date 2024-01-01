#include "invoke.h"

#include "attributes.h"
#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "object.h"
#include "stack.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "funcobj.h"
#include "tupleobj.h"
#include "typeobj.h"

/* ----- common utilities --------------------------------------------------- */

zis_noinline zis_cold_fn static void
format_error_type(struct zis_context *z, struct zis_object *fn) {
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "type", fn, "not callable");
    z->callstack->frame[0] = zis_object_from(exc);
}

zis_noinline zis_cold_fn static void
format_error_argc(struct zis_context *z, struct zis_func_obj *fn, size_t argc) {
    const struct zis_func_meta func_meta = fn->meta;
    size_t expected_argc = func_meta.na;
    const char *expected_prefix = "";
    if (func_meta.no) {
        if (func_meta.no == (unsigned char)-1 || argc < func_meta.na)
            expected_prefix = "at least ";
        else
            expected_argc += func_meta.no, expected_prefix = "at most ";
    }
    assert(argc != expected_argc);
    struct zis_exception_obj *exc = zis_exception_obj_format(
        z, "type", zis_object_from(fn),
        "wrong number of arguments (given %zu, expected %s%zu)",
        argc, expected_prefix, expected_argc
    );
    z->callstack->frame[0] = zis_object_from(exc);
}

/* ----- invocation tools --------------------------------------------------- */

/// Enter new frame. Returns the function to call.
zis_static_force_inline struct zis_func_obj *invocation_prepare(
    struct zis_context *z,
    void *return_ip,
    struct zis_object *callable,
    size_t argc
) {
    /// Extract the function object.
    struct zis_type_obj *const func_type = z->globals->type_Function;
    if (zis_unlikely(zis_object_type(callable) != func_type)) {
        format_error_type(z, callable);
        return NULL;
        // TODO: other callable objects.
    }
    struct zis_func_obj *func_obj = zis_object_cast(callable, struct zis_func_obj);
    const struct zis_func_meta func_meta = func_obj->meta;
    assert(func_meta.nr >= 1U + func_meta.na + (func_meta.no == (unsigned char)-1 ? 1U : func_meta.no));

    /// Check argc.
    const size_t argc_min = func_meta.na;
    if (zis_unlikely(argc != argc_min)) {
        if (zis_unlikely(
            argc < argc_min ||
            (func_meta.no != (unsigned char)-1 && argc > argc_min + func_meta.no)
        )) {
            // TODO: eliminate the complex conditional branches.
            format_error_argc(z, func_obj, argc);
            return NULL;
        }
    }

    /// New frame.
    zis_callstack_enter(z->callstack, func_meta.nr, return_ip);

    return func_obj;
}

/// Pass arguments (a vector).
zis_static_force_inline void invocation_pass_args_vec(
    struct zis_context *z,
    struct zis_func_meta func_meta,
    struct zis_object **argv,
    size_t argc
) {
    const size_t argc_min = func_meta.na;
    struct zis_object **const arg_list = z->callstack->frame + 1;

    if (zis_likely(argc == argc_min)) {
        zis_object_vec_copy(arg_list, argv, argc);
        if (zis_unlikely(func_meta.no)) {
            if (func_meta.no != (unsigned char)-1)
                zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), func_meta.no);
            else
                arg_list[argc] = zis_object_from(z->globals->val_empty_tuple);
        }
    } else {
        assert(!(argc < argc_min || !func_meta.no));
        if (func_meta.no != (unsigned char)-1) {
            zis_object_vec_copy(arg_list, argv, argc);
            assert(argc_min + func_meta.no >= argc);
            const size_t fill_n = argc_min + func_meta.no - argc;
            zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), fill_n);
        } else {
            zis_object_vec_copy(arg_list, argv, argc_min);
            struct zis_tuple_obj *const va_list_ =
                zis_tuple_obj_new(z, argv + argc_min, argc - argc_min);
            arg_list[argc_min] = zis_object_from(va_list_);
        }
    }
}

zis_static_force_inline void _invocation_pass_args_dis_copy(
    struct zis_object **dst,
    struct zis_object **src_frame, const unsigned int src_indices[],
    size_t n
) {
    for (size_t i = 0; i < n; i++)
        dst[i] = src_frame[src_indices[i]];
}

/// Pass arguments (discrete).
zis_static_force_inline void invocation_pass_args_dis(
    struct zis_context *z,
    struct zis_func_meta func_meta,
    const unsigned int args_prev_frame_regs[],
    size_t argc
) {
    // Adapted from `invocation_pass_args_vec()`.

    const size_t argc_min = func_meta.na;
    struct zis_object **const arg_list = z->callstack->frame + 1;
    struct zis_object **const prev_frame = zis_callstack_frame_info(z->callstack)->prev_frame;

    if (zis_likely(argc == argc_min)) {
        _invocation_pass_args_dis_copy(arg_list, prev_frame, args_prev_frame_regs, argc);
        if (zis_unlikely(func_meta.no)) {
            if (func_meta.no != (unsigned char)-1)
                zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), func_meta.no);
            else
                arg_list[argc] = zis_object_from(z->globals->val_empty_tuple);
        }
    } else {
        assert(!(argc < argc_min || !func_meta.no));
        if (func_meta.no != (unsigned char)-1) {
            _invocation_pass_args_dis_copy(arg_list, prev_frame, args_prev_frame_regs, argc);
            assert(argc_min + func_meta.no >= argc);
            const size_t fill_n = argc_min + func_meta.no - argc;
            zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), fill_n);
        } else {
            _invocation_pass_args_dis_copy(arg_list, prev_frame, args_prev_frame_regs, argc_min);
            const size_t rest_n = argc - argc_min;
            struct zis_tuple_obj *const va_list_ = zis_tuple_obj_new(z, NULL, rest_n);
            arg_list[argc_min] = zis_object_from(va_list_);
            _invocation_pass_args_dis_copy(va_list_->_data, prev_frame, args_prev_frame_regs + argc_min, rest_n);
            zis_object_assert_no_write_barrier(va_list_);
        }
    }
}

/// Leave the frame. Returns the function return value.
zis_static_force_inline void *invocation_cleanup(
    struct zis_context *z,
    void **return_ip
) {
    struct zis_callstack *const stack = z->callstack;
    struct zis_object *const ret_val = stack->frame[0];
    if (return_ip)
        *return_ip = zis_callstack_frame_info(stack)->return_ip;
    zis_callstack_leave(stack);
    return ret_val;
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
    struct zis_object *callable, size_t argc
) {
    return invocation_prepare(z, NULL, callable, argc);
}

void zis_invoke_pass_args_v(
    struct zis_context *z, struct zis_func_meta func_meta,
    struct zis_object **argv, size_t argc
) {
    invocation_pass_args_vec(z, func_meta, argv, argc);
}

void zis_invoke_pass_args_p(
    struct zis_context *z, struct zis_func_meta func_meta,
    struct zis_object *packed_args, size_t argc
) {
    assert(
        zis_object_type(packed_args) == z->globals->type_Tuple ||
        zis_object_type(packed_args) == z->globals->type_Array_Slots
    );
    static_assert(
        offsetof(struct zis_tuple_obj, _data) ==
            offsetof(struct zis_array_slots_obj, _data),
    "");
    struct zis_object **argv =
        zis_object_cast(packed_args, struct zis_tuple_obj)->_data;
    assert(zis_tuple_obj_length(zis_object_cast(packed_args, struct zis_tuple_obj)) >= argc);
    assert(zis_array_slots_obj_length(zis_object_cast(packed_args, struct zis_array_slots_obj)) >= argc);

    if (zis_likely(argc == func_meta.na || func_meta.no != (unsigned char)-1)) {
        // No object allocation. No need to worry about `packed_args` been moved.
        zis_invoke_pass_args_v(z, func_meta, argv, argc);
        return;
    }

    const size_t argc_min = func_meta.na;
    struct zis_object **const arg_list = z->callstack->frame + 1;
    assert(argc > argc_min || func_meta.no != (unsigned char)-1);
    zis_object_vec_copy(arg_list, argv, argc_min);
    const size_t rest_n = argc - argc_min;
    arg_list[argc_min] = zis_object_from(packed_args); // Protect it!
    struct zis_tuple_obj *const va_list_ = zis_tuple_obj_new(z, NULL, rest_n);
    argv = zis_object_cast(arg_list[argc_min], struct zis_tuple_obj)->_data;
    arg_list[argc_min] = zis_object_from(va_list_);
    zis_object_vec_copy(va_list_->_data, argv + argc_min, rest_n);
    zis_object_assert_no_write_barrier(va_list_);
}

void zis_invoke_pass_args_d(
    struct zis_context *z, struct zis_func_meta func_meta,
    const unsigned int arg_regs[], size_t argc
) {
    invocation_pass_args_dis(z, func_meta, arg_regs, argc);
}

struct zis_object *zis_invoke_cleanup(struct zis_context *z) {
#ifdef NDEBUG
    return invocation_cleanup(z, NULL);
#else // !NDEBUG
    void *ret_ip;
    struct zis_object *const ret_val = invocation_cleanup(z, &ret_ip);
    assert(!ret_ip);
    return ret_val;
#endif // NDEBUG
}

int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func) {
    const zis_native_func_t c_func = func->native;
    if (zis_unlikely(c_func))
        return c_func(z);
    return exec_bytecode(z, func);
}
