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

struct invocation_info {
    struct zis_object  **caller_frame;
    size_t               arg_shift;
    struct zis_func_meta func_meta;
};

/// Enter a new frame for a invocation.
/// REG-0 (caller frame) is the object to call and will be replaced with the real function object.
/// On error, make an exception (REG-0) and returns false.
zis_static_force_inline bool invocation_enter(
    struct zis_context *z,
    void *return_ip,
    struct invocation_info *info
) {
    struct zis_object **caller_frame = z->callstack->frame;
    size_t callee_frame_size;
    info->caller_frame = caller_frame;

    /// Extract the function object.
    struct zis_object *const callable = caller_frame[0];
    if (zis_likely(zis_object_type(callable) == z->globals->type_Function)) {
        struct zis_func_obj *func_obj = zis_object_cast(callable, struct zis_func_obj);
        callee_frame_size = func_obj->meta.nr;
        info->func_meta = func_obj->meta;
        info->arg_shift = 1;
    } else {
        format_error_type(z, callable);
        return false;
        // TODO: other callable objects.
    }

    /// New frame.
    zis_callstack_enter(z->callstack, callee_frame_size, return_ip);

    return true;
}

/// Pass arguments (a vector).
/// On error, make an exception (REG-0) and returns false.
zis_static_force_inline bool invocation_pass_args_vec(
    struct zis_context *z,
    struct zis_object **argv,
    size_t argc,
    struct invocation_info *info
) {
    const struct zis_func_meta func_meta = info->func_meta;
    assert(func_meta.nr >= 1U + func_meta.na + (func_meta.no == (unsigned char)-1 ? 1U : func_meta.no));
    const size_t argc_min = func_meta.na;
    assert(info->arg_shift > 0);
    struct zis_object **const arg_list = z->callstack->frame + info->arg_shift;

    if (zis_likely(argc == argc_min)) {
        zis_object_vec_copy(arg_list, argv, argc);
        if (zis_unlikely(func_meta.no)) {
            if (func_meta.no != (unsigned char)-1)
                zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), func_meta.no);
            else
                arg_list[argc] = zis_object_from(z->globals->val_empty_tuple);
        }
    } else {
        if (zis_unlikely(argc < argc_min)) {
        argc_error:
            assert(zis_object_type(info->caller_frame[0]) == z->globals->type_Function);
            struct zis_func_obj *func_obj = zis_object_cast(info->caller_frame[0], struct zis_func_obj);
            format_error_argc(z, func_obj, argc);
            return false;
        }
        if (func_meta.no != (unsigned char)-1) {
            if (zis_unlikely(argc > argc_min + func_meta.no))
                goto argc_error;
            zis_object_vec_copy(arg_list, argv, argc);
            const size_t fill_n = argc_min + func_meta.no - argc;
            zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), fill_n);
        } else {
            zis_object_vec_copy(arg_list, argv, argc_min);
            struct zis_tuple_obj *const va_list_ =
                zis_tuple_obj_new(z, argv + argc_min, argc - argc_min);
            arg_list[argc_min] = zis_object_from(va_list_);
        }
    }

    return true;
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
zis_static_force_inline bool invocation_pass_args_dis(
    struct zis_context *z,
    const unsigned int args_prev_frame_regs[],
    size_t argc,
    struct invocation_info *info
) {
    // Adapted from `invocation_pass_args_vec()`.

    const struct zis_func_meta func_meta = info->func_meta;
    assert(func_meta.nr >= 1U + func_meta.na + (func_meta.no == (unsigned char)-1 ? 1U : func_meta.no));
    const size_t argc_min = func_meta.na;
    assert(info->arg_shift > 0);
    struct zis_object **const arg_list = z->callstack->frame + info->arg_shift;
    struct zis_object **const caller_frame = info->caller_frame;

    if (zis_likely(argc == argc_min)) {
        _invocation_pass_args_dis_copy(arg_list, caller_frame, args_prev_frame_regs, argc);
        if (zis_unlikely(func_meta.no)) {
            if (func_meta.no != (unsigned char)-1)
                zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), func_meta.no);
            else
                arg_list[argc] = zis_object_from(z->globals->val_empty_tuple);
        }
    } else {
        if (zis_unlikely(argc < argc_min)) {
        argc_error:
            assert(zis_object_type(info->caller_frame[0]) == z->globals->type_Function);
            struct zis_func_obj *func_obj = zis_object_cast(info->caller_frame[0], struct zis_func_obj);
            format_error_argc(z, func_obj, argc);
            return false;
        }
        if (func_meta.no != (unsigned char)-1) {
            if (zis_unlikely(argc > argc_min + func_meta.no))
                goto argc_error;
            _invocation_pass_args_dis_copy(arg_list, caller_frame, args_prev_frame_regs, argc);
            const size_t fill_n = argc_min + func_meta.no - argc;
            zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), fill_n);
        } else {
            _invocation_pass_args_dis_copy(arg_list, caller_frame, args_prev_frame_regs, argc_min);
            const size_t rest_n = argc - argc_min;
            struct zis_tuple_obj *const va_list_ = zis_tuple_obj_new(z, NULL, rest_n);
            arg_list[argc_min] = zis_object_from(va_list_);
            _invocation_pass_args_dis_copy(va_list_->_data, caller_frame, args_prev_frame_regs + argc_min, rest_n);
            zis_object_assert_no_write_barrier(va_list_);
        }
    }

    return true;
}

/// Leave the frame. Returns the function return value.
zis_static_force_inline void *invocation_leave(
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

struct zis_func_obj *zis_invoke_prepare_va(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object **argv, size_t argc
) {
    struct invocation_info ii;
    z->callstack->frame[0] = callable;
    if (zis_unlikely(!invocation_enter(z, NULL, &ii))) {
        return NULL;
    }
    assert(argv > ii.caller_frame);
    if (zis_unlikely(!invocation_pass_args_vec(z, argv, argc, &ii))) {
        // TODO: add to traceback.
        ii.caller_frame[0] = invocation_leave(z, NULL);
        return NULL;
    }
    assert(zis_object_type(ii.caller_frame[0]) == z->globals->type_Function);
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

static bool _zis_invoke_prepare_pa_pass_args(
    struct zis_context *z,
    struct zis_object *packed_args, size_t argc,
    struct invocation_info *info
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

    const struct zis_func_meta func_meta = info->func_meta;
    if (zis_likely(argc <= func_meta.na || func_meta.no != (unsigned char)-1)) {
        // No object allocation. No need to worry about `packed_args` been moved.
        return invocation_pass_args_vec(z, argv, argc, info);
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

    return true;
}

struct zis_func_obj *zis_invoke_prepare_pa(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object *packed_args, size_t argc
) {
    struct invocation_info ii;
    z->callstack->frame[0] = callable;
    if (zis_unlikely(!invocation_enter(z, NULL, &ii))) {
        return NULL;
    }
    assert(packed_args != ii.caller_frame[0]);
    if (zis_unlikely(!_zis_invoke_prepare_pa_pass_args(z, packed_args, argc, &ii))) {
        // TODO: add to traceback.
        ii.caller_frame[0] = invocation_leave(z, NULL);
        return NULL;
    }
    assert(zis_object_type(ii.caller_frame[0]) == z->globals->type_Function);
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

struct zis_func_obj *zis_invoke_prepare_da(
    struct zis_context *z, struct zis_object *callable,
    const unsigned int arg_regs[], size_t argc
) {
    struct invocation_info ii;
    z->callstack->frame[0] = callable;
    if (zis_unlikely(!invocation_enter(z, NULL, &ii))) {
        return NULL;
    }
    if (zis_unlikely(!invocation_pass_args_dis(z, arg_regs, argc, &ii))) {
        // TODO: add to traceback.
        ii.caller_frame[0] = invocation_leave(z, NULL);
        return NULL;
    }
    assert(zis_object_type(ii.caller_frame[0]) == z->globals->type_Function);
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

struct zis_object *zis_invoke_cleanup(struct zis_context *z) {
#ifdef NDEBUG
    return invocation_leave(z, NULL);
#else // !NDEBUG
    void *ret_ip;
    struct zis_object *const ret_val = invocation_leave(z, &ret_ip);
    assert(!ret_ip);
    return ret_val;
#endif // NDEBUG
}

int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func) {
    assert(zis_object_from(func) == zis_callstack_frame_info(z->callstack)->prev_frame[0]);
    const zis_native_func_t c_func = func->native;
    if (zis_unlikely(c_func))
        return c_func(z);
    return exec_bytecode(z, func);
}
