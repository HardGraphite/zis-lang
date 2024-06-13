#include "invoke.h"

#include <stdarg.h>
#include <math.h>

#include "assembly.h" // zis_debug_dump_bytecode()
#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "instr.h"
#include "loader.h"
#include "ndefutil.h"
#include "object.h"
#include "stack.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "floatobj.h"
#include "funcobj.h"
#include "mapobj.h"
#include "moduleobj.h"
#include "symbolobj.h"
#include "tupleobj.h"
#include "typeobj.h"

#include "zis_config.h" // ZIS_BUILD_CGOTO

/* ----- invocation tools --------------------------------------------------- */

zis_noinline zis_cold_fn static void
format_error_func_type(struct zis_context *z, struct zis_object *fn) {
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "type", fn, "not callable");
    zis_context_set_reg0(z, zis_object_from(exc));
}

zis_noinline zis_cold_fn static void
format_error_argc(struct zis_context *z, struct zis_func_obj *fn, size_t argc) {
    const struct zis_func_obj_meta func_meta = fn->meta;
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
    zis_context_set_reg0(z, zis_object_from(exc));
}

struct invocation_info {
    struct zis_object      **caller_frame;
    size_t                   arg_shift;
    struct zis_func_obj_meta func_meta;
};

zis_noinline static bool _invocation_enter_callable_obj(
    struct zis_context *z,
    zis_instr_word_t *caller_ip,
    struct zis_object **ret_val_reg,
    struct invocation_info *info
) {
    struct zis_object **caller_frame = z->callstack->frame;
    struct zis_func_obj *func_obj;
    size_t callable_obj_depth = 0;
    struct zis_object *callable_obj_list[8];
    info->caller_frame = caller_frame;

    /// Extract the function object.
    for (struct zis_object *callable = caller_frame[0];;) {
        if (zis_unlikely(zis_object_is_smallint(callable))) {
            format_error_func_type(z, callable);
            return false;
        }
        struct zis_type_obj *const callable_type = zis_object_type(callable);
        if (zis_likely(callable_type == z->globals->type_Function)) {
            caller_frame[0] = callable;
            func_obj = zis_object_cast(callable, struct zis_func_obj);
            break;
        }
        if (zis_unlikely(callable_obj_depth >= sizeof callable_obj_list / sizeof callable_obj_list[0])) {
            format_error_func_type(z, callable); // FIXME: TOO MANY LEVELS!
            return false;
        }
        callable = zis_type_obj_get_method(callable_type, z->globals->sym_operator_call);
        if (zis_unlikely(!callable)) {
            format_error_func_type(z, callable);
            return false;
        }
        callable_obj_list[callable_obj_depth++] = callable;
    }
    info->func_meta = func_obj->meta;
    info->arg_shift = callable_obj_depth + 1;

    /// New frame.
    const size_t callee_frame_size = func_obj->meta.nr;
    zis_callstack_enter(z->callstack, callee_frame_size, caller_ip, ret_val_reg);
    if (func_obj->meta.na >= (unsigned char)callable_obj_depth) {
        struct zis_object **frame = z->callstack->frame;
        for (size_t i = 0; i < callable_obj_depth; i++)
            frame[callable_obj_depth - i] = callable_obj_list[i];
    } else {
        format_error_argc(z, zis_object_cast(caller_frame[0], struct zis_func_obj), callable_obj_depth);
        // FIXME: pass extra arguments as packed.
        return false;
    }

    return true;
}

/// Enter a new frame for a invocation.
/// REG-0 (caller frame) is the object to call and will be replaced with the real function object.
/// On error, make an exception (REG-0) and returns false.
/// This function guarantees that no object is allocated during call.
zis_static_force_inline bool invocation_enter(
    struct zis_context *z,
    zis_instr_word_t *caller_ip,
    struct zis_object **ret_val_reg,
    struct invocation_info *info
) {
    struct zis_object **caller_frame = z->callstack->frame;
    info->caller_frame = caller_frame;

    /// Extract the function object.
    struct zis_object *const callable = caller_frame[0];
    if (zis_unlikely(!zis_object_type_is(callable, z->globals->type_Function)))
        return _invocation_enter_callable_obj(z, caller_ip, ret_val_reg, info);
    struct zis_func_obj *func_obj = zis_object_cast(callable, struct zis_func_obj);
    info->func_meta = func_obj->meta;
    info->arg_shift = 1;

    /// New frame.
    const size_t callee_frame_size = func_obj->meta.nr;
    zis_callstack_enter(z->callstack, callee_frame_size, caller_ip, ret_val_reg);

    return true;
}

/// Pass arguments (a vector).
/// On error, make an exception (REG-0) and returns false.
static bool invocation_pass_args_vec(
    struct zis_context *z,
    struct zis_object *const *argv,
    size_t argc,
    struct invocation_info *info
) {
    const struct zis_func_obj_meta func_meta = info->func_meta;
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
            assert(zis_object_type_is(info->caller_frame[0], z->globals->type_Function));
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
                zis_tuple_obj_new(z, (struct zis_object **)argv + argc_min, argc - argc_min);
            arg_list[argc_min] = zis_object_from(va_list_);
        }
    }

    return true;
}

/// Pass arguments (packed).
static bool invocation_pass_args_pac(
    struct zis_context *z,
    struct zis_object *packed_args, size_t argc,
    struct invocation_info *info
) {
    assert(
        zis_object_type_is(packed_args, z->globals->type_Tuple) ||
        zis_object_type_is(packed_args, z->globals->type_Array_Slots)
    );
    static_assert(
        offsetof(struct zis_tuple_obj, _data) ==
            offsetof(struct zis_array_slots_obj, _data),
        "");
    struct zis_object **argv =
        zis_object_cast(packed_args, struct zis_tuple_obj)->_data;
    assert(zis_tuple_obj_length(zis_object_cast(packed_args, struct zis_tuple_obj)) >= argc);
    assert(zis_array_slots_obj_length(zis_object_cast(packed_args, struct zis_array_slots_obj)) >= argc);

    const struct zis_func_obj_meta func_meta = info->func_meta;
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
    zis_object_write_barrier_n(va_list_, argv + argc_min, rest_n);

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
static bool invocation_pass_args_dis(
    struct zis_context *z,
    const unsigned int args_prev_frame_regs[],
    size_t argc,
    struct invocation_info *info
) {
    // Adapted from `invocation_pass_args_vec()`.

    const struct zis_func_obj_meta func_meta = info->func_meta;
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
            assert(zis_object_type_is(info->caller_frame[0], z->globals->type_Function));
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
            zis_object_assert_no_write_barrier(va_list_); // FIXME: write barrier may be needed here.
        }
    }

    return true;
}

/// Pass arguments like `invocation_pass_args_dis()`.
/// Modified for the CALL instruction. Includes register index checks.
static bool invocation_pass_args_dis_1(
    struct zis_context *z,
    zis_instr_word_t args_prev_frame_regs /* Same with the operands of instruction CALL */,
    unsigned int argc,
    struct invocation_info *info
) {
    // Adapted from `invocation_pass_args_dis()`.

    const struct zis_func_obj_meta func_meta = info->func_meta;
    assert(func_meta.nr >= 1U + func_meta.na + (func_meta.no == (unsigned char)-1 ? 1U : func_meta.no));
    const size_t argc_min = func_meta.na;
    assert(info->arg_shift > 0);
    struct zis_object **const arg_list = z->callstack->frame + info->arg_shift;
    struct zis_object **const caller_frame = info->caller_frame;

    if (zis_likely(argc == argc_min)) {
        for (uint32_t i = 0; i < argc; i++) {
            const unsigned int idx = (args_prev_frame_regs >> (7 + 6 * i)) & 63;
            struct zis_object **arg_p = caller_frame + idx;
            if (zis_unlikely(arg_p >= arg_list))
                goto bound_check_fail;
            arg_list[i] = *arg_p;
        }
        if (zis_unlikely(func_meta.no)) {
            if (func_meta.no != (unsigned char)-1)
                zis_object_vec_fill(arg_list + argc, zis_object_from(z->globals->val_nil), func_meta.no);
            else
                arg_list[argc] = zis_object_from(z->globals->val_empty_tuple);
        }
    } else {
        unsigned int args[4];
        assert(argc <= sizeof args / sizeof args[0]);
        for (uint32_t i = 0; i < argc; i++) {
            const unsigned int idx = (args_prev_frame_regs >> (7 + 6 * i)) & 63;
            if (zis_unlikely(caller_frame + idx >= arg_list))
                goto bound_check_fail;
            args[i] = idx;
        }
        return invocation_pass_args_dis(z, args, argc, info);
    }

    return true;

bound_check_fail:
    // see: BOUND_CHECK_REG()
    zis_debug_log(FATAL, "Interp", "register index out of range");
    zis_context_panic(z, ZIS_CONTEXT_PANIC_ILL);
}

/// Leave the frame. Returns the caller ip.
zis_static_force_inline zis_instr_word_t *invocation_leave(
    struct zis_context *z,
    struct zis_object *ret_val
) {
    assert(ret_val != NULL);
    struct zis_callstack *const stack = z->callstack;
    zis_instr_word_t *caller_ip;
    {
        const struct zis_callstack_frame_info *const fi =
            zis_callstack_frame_info(stack);
        caller_ip = fi->caller_ip;
        *fi->ret_val_reg = ret_val;
    }
    zis_callstack_leave(stack);
    return caller_ip;
}

/* ----- bytecode execution ------------------------------------------------- */

zis_noinline zis_cold_fn static void
format_error_global_not_found(
    struct zis_context *z, struct zis_func_obj *func, unsigned name_sym_id
) {
    struct zis_symbol_obj *name = zis_func_obj_symbol(func, name_sym_id);
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "key", zis_object_from(name), "variable not defined");
    zis_context_set_reg0(z, zis_object_from(exc));
}

zis_noinline zis_cold_fn static void
format_error_field_not_exists(
    struct zis_context *z, struct zis_func_obj *func, unsigned name_sym_id
) {
    struct zis_symbol_obj *name = zis_func_obj_symbol(func, name_sym_id);
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "key", zis_object_from(name), "field not exists");
    zis_context_set_reg0(z, zis_object_from(exc));
}

zis_noinline zis_cold_fn static void
format_error_method_not_exists(
    struct zis_context *z, struct zis_object *obj, struct zis_symbol_obj *name
) {
    struct zis_exception_obj *exc = zis_exception_obj_format(
        z, "key", obj, "method `%.*s' does not exist",
        (int)zis_symbol_obj_data_size(name), zis_symbol_obj_data(name)
    );
    zis_context_set_reg0(z, zis_object_from(exc));
}

zis_noinline zis_cold_fn static void
format_error_cond_is_not_bool(
    struct zis_context *z, struct zis_object *value
) {
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "type", value, "condition expression is not boolean");
    zis_context_set_reg0(z, zis_object_from(exc));
}

/// Run the bytecode in the function object.
/// Then pop the current frame and handles the return value.
zis_hot_fn static int invoke_bytecode_func(
    struct zis_context *z, struct zis_func_obj *this_func
) {
#define OP_DISPATCH_USE_COMPUTED_GOTO ZIS_USE_COMPUTED_GOTO

    zis_instr_word_t *ip = this_func->bytecode; // The instruction pointer.
    zis_instr_word_t this_instr = *ip;

#define IP_ADVANCE     (this_instr = *++ip)
#define IP_JUMP_TO(X)  (ip = (X), this_instr = *ip)

    struct zis_callstack *const stack = z->callstack;
    struct zis_object **bp = stack->frame;
    struct zis_object **sp = stack->top;

#define BP_SP_CHANGED  (bp = stack->frame, sp = stack->top)

    /* this_func; */
    assert(zis_callstack_frame_info(stack)->prev_frame[0] == zis_object_from(this_func));
    uint32_t func_sym_count = (uint32_t)zis_func_obj_symbol_count(this_func);
    uint32_t func_const_count = (uint32_t)zis_func_obj_constant_count(this_func);

#define FUNC_ENSURE \
    do {  /* this_func shall not be moved */ \
        assert((void *)this_func == (void *)zis_callstack_frame_info(stack)->prev_frame[0]); \
        assert(zis_object_type_is((struct zis_object *)this_func, g->type_Function));        \
        assert((size_t)func_sym_count == zis_func_obj_symbol_count(this_func));     \
        assert((size_t)func_const_count == zis_func_obj_constant_count(this_func)); \
    } while (0)
#define FUNC_CHANGED \
    do {             \
        struct zis_object *p = zis_callstack_frame_info(stack)->prev_frame[0]; \
        assert(zis_object_type_is(p, g->type_Function));                       \
        this_func = zis_object_cast(p, struct zis_func_obj);                   \
        func_sym_count = (uint32_t)zis_func_obj_symbol_count(this_func);       \
        func_const_count = (uint32_t)zis_func_obj_constant_count(this_func);   \
    } while (0)
#define FUNC_CHANGED_TO(NEW_FUNC) \
    do {                          \
        this_func = (NEW_FUNC);   \
        assert((void *)this_func == (void *)zis_callstack_frame_info(stack)->prev_frame[0]); \
        assert(zis_object_type_is((struct zis_object *)this_func, g->type_Function));        \
        func_sym_count = (uint32_t)zis_func_obj_symbol_count(this_func);       \
        func_const_count = (uint32_t)zis_func_obj_constant_count(this_func);   \
    } while (0)

    struct zis_context_globals *const g = z->globals;

    struct {
        struct zis_symbol_obj *method;
        uint32_t extra_arg_regs; // arg2 and arg3
    } internal_call_param; // See macro CALL_METHOD() and label _do_internal_send.

#if OP_DISPATCH_USE_COMPUTED_GOTO // vvv

    // https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html

#define OP_LABEL(NAME)   _op_##NAME##_label
#define OP_DEFINE(NAME)  OP_LABEL(NAME) :
#define OP_UNDEFINED     OP_LABEL() :
#define OP_DISPATCH      goto *(&& OP_LABEL(NOP) + _op_dispatch_table[zis_instr_extract_opcode(this_instr)])

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // &&label
#pragma GCC diagnostic ignored "-Wpointer-arith" // &&label1 - &&label0

    static const int _op_dispatch_table[] = {
#define E(CODE, NAME, TYPE) [CODE] = && OP_LABEL(NAME) - && OP_LABEL(NOP),
        ZIS_OP_LIST_FULL
#undef E
    };

    OP_DISPATCH;

#else // ^^^ OP_DISPATCH_USE_COMPUTED_GOTO | !OP_DISPATCH_USE_COMPUTED_GOTO vvv

#define OP_DEFINE(NAME)  case (zis_instr_word_t)ZIS_OPC_##NAME :
#define OP_UNDEFINED     default :
#define OP_DISPATCH      goto _interp_loop

_interp_loop:
    switch (zis_instr_extract_opcode(this_instr)) {

#endif // ^^^ OP_DISPATCH_USE_COMPUTED_GOTO

/// Throws the object in `bp[0]` (REG-0).
#define THROW_REG0 \
    do {           \
        this_instr = 0; \
        goto _op_thr;   \
    } while (0)

/// Calls method `NAME` of object `bp[OBJ_REG]` with optional arguments
/// `bp[ARG2_REG]` and `bp[ARG3_REG]` and stores the return value to `bp[RET_REG]`.
#define CALL_METHOD(RET_REG, NAME, ARGC, OBJ_REG, ARG2_REG, ARG3_REG) \
    do {                                                              \
        assert((ARGC) <= 3 && (RET_REG) <= 0x3fff && (OBJ_REG) <= 0xffff && (ARG2_REG) <= 0xffff && (ARG3_REG) <= 0xffff); \
        this_instr = (ARGC) | (RET_REG) << 2 | (OBJ_REG) << 16;       \
        internal_call_param.extra_arg_regs = (ARG2_REG) | (ARG3_REG) << 16; \
        internal_call_param.method = (NAME);                          \
        goto _do_internal_send;                                       \
    } while (0)

#define BOUND_CHECK_REG(PTR) \
    do {                     \
        assert(PTR >= bp);   \
        if (zis_unlikely(PTR > sp)) { \
            zis_debug_log(FATAL, "Interp", "register index out of range"); \
            goto panic_ill;  \
        }                    \
    } while (0)

#define BOUND_CHECK_REG_VEC(BASE_PTR, LEN) \
    do {                                   \
        assert((BASE_PTR) >= bp);          \
        if (zis_unlikely(((BASE_PTR) + (LEN) - 1) > sp)) { \
            zis_debug_log(FATAL, "Interp", "register index out of range"); \
            goto panic_ill;                \
        }                                  \
    } while (0)

#define BOUND_CHECK_SYM(I) \
    do {                   \
        if (zis_unlikely(I >= func_sym_count)) { \
            zis_debug_log(FATAL, "Interp", "symbol index out of range"); \
            goto panic_ill;\
        }                  \
    } while (0)

#define BOUND_CHECK_CON(I) \
    do {                   \
        if (zis_unlikely(I >= func_const_count)) { \
            zis_debug_log(FATAL, "Interp", "constant index out of range"); \
            goto panic_ill;\
        }                  \
    } while (0)

#define BOUND_CHECK_GLB(I) \
    do {                   \
        if (zis_unlikely(I >= zis_module_obj_var_count(this_func->_module))) { \
            zis_debug_log(FATAL, "Interp", "global index out of range");      \
            goto panic_ill;\
        }                  \
    } while (0)

#define BOUND_CHECK_FLD(OBJ, I) \
    do {                   \
        if (zis_unlikely(I >= zis_object_slot_count((OBJ)))) { \
            zis_debug_log(FATAL, "Interp", "field index out of range"); \
            goto panic_ill;\
        }                  \
    } while (0)

    OP_DEFINE(NOP) {
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(ARG) {
        zis_debug_log(FATAL, "Interp", "unexpected opcode %#04x", ZIS_OPC_ARG);
        goto panic_ill;
    }

    OP_DEFINE(BRK) {
        uint32_t breakpoint_id;
        zis_instr_extract_operands_Aw(this_instr, breakpoint_id);
        zis_debug_log(INFO, "Interp", "breakpoint: ip=%p, id=%u", (void *)ip, breakpoint_id);
        zis_unused_var(breakpoint_id);
        goto panic_ill; // TODO: run the debugger.
    }

    OP_DEFINE(LDNIL) {
        uint32_t tgt, count;
        zis_instr_extract_operands_ABw(this_instr, tgt, count);
        struct zis_object **tgt_p = bp + tgt, **tgt_last_p = tgt_p + count - 1;
        BOUND_CHECK_REG(tgt_last_p);
        struct zis_object *const nil = zis_object_from(g->val_nil);
        for (; tgt_p <= tgt_last_p; tgt_p++)
            *tgt_p = nil;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDBLN) {
        uint32_t tgt; bool val;
        zis_instr_extract_operands_ABw(this_instr, tgt, val);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        *tgt_p = zis_object_from(val ? g->val_true : g->val_false);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDCON) {
        uint32_t tgt, id;
        zis_instr_extract_operands_ABw(this_instr, tgt, id);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        FUNC_ENSURE;
        BOUND_CHECK_CON(id);
        *tgt_p = zis_func_obj_constant(this_func, id);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDSYM) {
        uint32_t tgt, id;
        zis_instr_extract_operands_ABw(this_instr, tgt, id);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(id);
        *tgt_p = zis_object_from(zis_func_obj_symbol(this_func, id));
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKINT) {
        uint32_t tgt; zis_smallint_t val;
        zis_instr_extract_operands_ABsw(this_instr, tgt, val);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        *tgt_p = zis_smallint_to_ptr(val);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKFLT) {
        uint32_t tgt; double frac; int exp;
        zis_instr_extract_operands_ABsCs(this_instr, tgt, frac, exp);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        *tgt_p = zis_object_from(zis_float_obj_new(z, ldexp(frac, exp)));
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKTUP) {
        uint32_t tgt, val_start, val_count;
        zis_instr_extract_operands_ABC(this_instr, tgt, val_start, val_count);
        struct zis_object **tgt_p = bp + tgt, **val_p = bp + val_start;
        BOUND_CHECK_REG(tgt_p);
        BOUND_CHECK_REG_VEC(val_p, val_count);
        *tgt_p = zis_object_from(zis_tuple_obj_new(z, val_p, val_count));
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKARR) {
        uint32_t tgt, val_start, val_count;
        zis_instr_extract_operands_ABC(this_instr, tgt, val_start, val_count);
        struct zis_object **tgt_p = bp + tgt, **val_p = bp + val_start;
        BOUND_CHECK_REG(tgt_p);
        BOUND_CHECK_REG_VEC(val_p, val_count);
        *tgt_p = zis_object_from(zis_array_obj_new(z, val_p, val_count));
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKMAP) {
        uint32_t tgt, val_start, val_count;
        zis_instr_extract_operands_ABC(this_instr, tgt, val_start, val_count);
        struct zis_object **tgt_p = bp + tgt;
        struct zis_object **val_p = bp + val_start, **val_end_p = val_p + val_count * 2;
        BOUND_CHECK_REG(tgt_p);
        if (val_count) {
            BOUND_CHECK_REG(val_end_p - 1);
            struct zis_object **const map_reg = zis_callstack_frame_alloc_temp(z, 1);
            *map_reg = zis_object_from(zis_map_obj_new(z, 0.0f, val_count));
            for (; val_p < val_end_p; val_p += 2) {
                assert(zis_object_type_is(*map_reg, g->type_Map));
                struct zis_map_obj *map = zis_object_cast(*map_reg, struct zis_map_obj);
                if (zis_map_obj_set(z, map, val_p[0], val_p[1]) != ZIS_OK) {
                    if (map_reg != tgt_p) {
                        assert(tgt == 0);
                        zis_callstack_frame_free_temp(z, 1);
                    }
                    THROW_REG0;
                }
            }
            *tgt_p = *map_reg;
            zis_callstack_frame_free_temp(z, 1);
            assert(stack->top == sp);
        } else {
            // val_count == 0
            *tgt_p = zis_object_from(zis_map_obj_new(z, 0.0f, 0));
        }
        IP_ADVANCE;
        OP_DISPATCH;
    }

    _op_thr:
    OP_DEFINE(THR) {
        uint32_t val;
        zis_instr_extract_operands_Aw(this_instr, val);
        struct zis_object **val_p = bp + val;
        BOUND_CHECK_REG(val_p);
        const bool val_is_exc = zis_object_type_is(*val_p, g->type_Exception);
        FUNC_ENSURE;
        while (true) {
            if (val_is_exc) {
                assert(zis_object_type_is(*val_p, g->type_Exception));
                struct zis_exception_obj *exc = zis_object_cast(*val_p, struct zis_exception_obj);
                zis_exception_obj_stack_trace(z, exc, this_func, ip);
            }
            // TODO: check if caught.
            struct zis_object **new_val_p = zis_callstack_frame_info(stack)->ret_val_reg;
            ip = invocation_leave(z, *val_p);
            if (zis_unlikely(!ip))
                return ZIS_THR;
            FUNC_CHANGED;
            assert(*new_val_p == *val_p);
            val_p = new_val_p;
        }
    }

    OP_DEFINE(RETNIL) {
        ip = invocation_leave(z, zis_object_from(g->val_nil));
        if (zis_unlikely(!ip))
            return ZIS_OK;
        BP_SP_CHANGED;
        FUNC_CHANGED;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(RET) {
        uint32_t ret;
        zis_instr_extract_operands_Aw(this_instr, ret);
        struct zis_object **ret_p = bp + ret;
        BOUND_CHECK_REG(ret_p);
        ip = invocation_leave(z, *ret_p);
        if (zis_unlikely(!ip))
            return ZIS_OK;
        BP_SP_CHANGED;
        FUNC_CHANGED;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(CALL) {
        uint32_t ret, argc;
        ret = this_instr >> 27;
        argc = (this_instr >> 25) & 3;
        struct zis_object **ret_p = bp + ret;
        BOUND_CHECK_REG(ret_p);
        struct invocation_info ii;
        if (zis_unlikely(!invocation_enter(z, ip, ret_p, &ii)))
            THROW_REG0;
        BP_SP_CHANGED;
        FUNC_CHANGED_TO(zis_object_cast(ii.caller_frame[0], struct zis_func_obj));
        if (zis_unlikely(!invocation_pass_args_dis_1(z, this_instr, argc, &ii)))
            THROW_REG0;
    } {
    _do_call_func_obj:
        if (this_func->native) {
            const int status = this_func->native(z);
            if (zis_unlikely(status == ZIS_THR))
                THROW_REG0;
            assert(status == ZIS_OK);
            zis_instr_word_t *ip0 = invocation_leave(z, bp[0]);
            assert(ip0 == ip), zis_unused_var(ip0);
            BP_SP_CHANGED;
            FUNC_CHANGED;
            IP_ADVANCE;
        } else {
            assert(zis_func_obj_bytecode_length(this_func));
            IP_JUMP_TO(this_func->bytecode);
        }
        OP_DISPATCH;
    }

    OP_DEFINE(CALLV) {
        uint32_t ret, arg_start, arg_count;
        zis_instr_extract_operands_ABC(this_instr, ret, arg_start, arg_count);
        struct zis_object **ret_p = bp + ret, **arg_p = bp + arg_start;
        BOUND_CHECK_REG(ret_p);
        BOUND_CHECK_REG(arg_p + arg_count - 1);
        struct invocation_info ii;
        if (zis_unlikely(!invocation_enter(z, ip, ret_p, &ii)))
            THROW_REG0;
        BP_SP_CHANGED;
        FUNC_CHANGED_TO(zis_object_cast(ii.caller_frame[0], struct zis_func_obj));
        if (zis_unlikely(!invocation_pass_args_vec(z, arg_p, arg_count, &ii)))
            THROW_REG0;
        goto _do_call_func_obj;
    }

    OP_DEFINE(CALLP) {
        uint32_t ret, args;
        zis_instr_extract_operands_ABw(this_instr, ret, args);
        struct zis_object **ret_p = bp + ret, **arg_p = bp + args;
        BOUND_CHECK_REG(ret_p);
        BOUND_CHECK_REG(arg_p);
        struct zis_object *a = *arg_p;
        struct zis_type_obj *t = zis_object_type_1(a);
        size_t argc;
        if (t == g->type_Tuple) {
            argc = zis_tuple_obj_length(zis_object_cast(a, struct zis_tuple_obj));
        } else if (t == g->type_Array) {
            struct zis_array_slots_obj *v = zis_object_cast(a, struct zis_array_obj)->_data;
            *arg_p = zis_object_from(v);
            argc = zis_array_slots_obj_length(v);
        } else {
            zis_debug_log(FATAL, "Interp", "CALLP: wrong arg pack type");
            goto panic_ill;
        }
        struct invocation_info ii;
        if (zis_unlikely(!invocation_enter(z, ip, ret_p, &ii)))
            THROW_REG0;
        BP_SP_CHANGED;
        FUNC_CHANGED_TO(zis_object_cast(ii.caller_frame[0], struct zis_func_obj));
        if (zis_unlikely(!invocation_pass_args_pac(z, *arg_p, argc, &ii)))
            THROW_REG0;
        goto _do_call_func_obj;
    }

    _do_internal_send: {
        /*
         * `this_instr[1:0]` = argc; `this_instr[15:2]` = ret_reg; `this_instr[31:16]` = arg1_reg;
         * `internal_call_param.extra_arg_regs[15:0]` = arg2_reg; `internal_call_param.extra_arg_regs[31:16]` = arg3_reg;
         * `internal_call_param.method` = method_name.
         * See macro `CALL_METHOD()`.
         */

        static_assert(sizeof this_instr == 32 / 8, "");
        static_assert(sizeof internal_call_param.extra_arg_regs == 32 / 8, "");
        const unsigned int argc = this_instr & 3;
        const unsigned int ret_reg = (this_instr & 0xffff) >> 2;
        const unsigned int arg_regs[3] = {
            this_instr >> 16,
            internal_call_param.extra_arg_regs & 0xffff,
            internal_call_param.extra_arg_regs >> 16,
        };
        struct zis_symbol_obj *const method_name = internal_call_param.method;
        assert(argc >= 1 && argc <= 3);
        assert(bp + ret_reg <= sp);
        assert(bp + arg_regs[0] <= sp && bp + arg_regs[1] <= sp && bp + arg_regs[2] <= sp);
        assert(zis_object_type_is(zis_object_from(method_name), g->type_Symbol));

        struct zis_object *const args[3] = {
            bp[arg_regs[0]], bp[arg_regs[1]], bp[arg_regs[2]]
        };
        struct zis_object *method_obj = zis_type_obj_get_method(
            zis_unlikely(zis_object_is_smallint(args[0])) ?
                g->type_Int : zis_object_type(args[0]),
            method_name
        );
        if (zis_unlikely(!method_obj)) {
            format_error_method_not_exists(z, args[0], method_name);
            THROW_REG0;
        }

        struct invocation_info ii;
        bp[0] = method_obj;
        if (zis_unlikely(!invocation_enter(z, ip, bp + ret_reg, &ii)))
            THROW_REG0;
        BP_SP_CHANGED;
        FUNC_CHANGED_TO(zis_object_cast(ii.caller_frame[0], struct zis_func_obj));

        if (zis_likely(argc <= ii.func_meta.na || ii.func_meta.no != (unsigned char)-1)) {
            // No object allocation. No need to worry about `args` been moved.
            // See similar usage in function `invocation_pass_args_pac()`.
            if (zis_unlikely(!invocation_pass_args_vec(z, args, argc, &ii)))
                THROW_REG0;
        } else if (arg_regs[0] && (arg_regs[1] || argc <= 1) && (arg_regs[2] || argc <= 2)) {
            // REG-0 is not used for argument passing.
            if (zis_unlikely(!invocation_pass_args_dis(z, arg_regs, argc, &ii)))
                THROW_REG0;
        } else {
            // REG-0 is used for argument passing, which has been assign with
            // the function object to call. Temporarily recover it.
            zis_locals_decl_1(z, var, struct zis_object *func);
            var.func = ii.caller_frame[0];
            ii.caller_frame[0] = args[0], ii.caller_frame[1] = args[1], ii.caller_frame[2] = args[2];
            const bool ok = invocation_pass_args_dis(z, arg_regs, argc, &ii);
            ii.caller_frame[0] = var.func;
            zis_locals_drop(z, var);
            if (zis_unlikely(!ok))
                THROW_REG0;
        }

        goto _do_call_func_obj;
    }

    OP_DEFINE(LDMTH) {
        uint32_t name, obj_;
        zis_instr_extract_operands_ABw(this_instr, obj_, name);
        struct zis_object **obj_p = bp + obj_;
        BOUND_CHECK_REG(obj_p);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(name);
        struct zis_symbol_obj *name_sym = zis_func_obj_symbol(this_func, name);
        struct zis_object *obj = *obj_p;
        struct zis_type_obj *const obj_type =
            zis_object_is_smallint(obj) ? g->type_Int : zis_object_type(obj);
        struct zis_object *meth_obj = zis_type_obj_get_method(obj_type, name_sym);
        if (zis_unlikely(!meth_obj)) {
            format_error_method_not_exists(z, obj, name_sym);
            THROW_REG0;
        }
        *bp = meth_obj;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(IMP) {
        uint32_t tgt, name;
        zis_instr_extract_operands_ABw(this_instr, tgt, name);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(name);
        const int flags = ZIS_MOD_LDR_SEARCH_LOADED | ZIS_MOD_LDR_UPDATE_LOADED;
        struct zis_module_obj *module =
            zis_module_loader_import(z, NULL, zis_func_obj_symbol(this_func, name), NULL, flags);
        if (zis_unlikely(!module))
            THROW_REG0;
        *tgt_p = zis_object_from(module);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(IMPSUB) {
        goto panic_ill; // Not implemented.
    }

    OP_DEFINE(LDLOC) {
        uint32_t val, loc;
        zis_instr_extract_operands_ABw(this_instr, val, loc);
        struct zis_object **val_p = bp + val, **loc_p = bp + loc;
        BOUND_CHECK_REG(val_p);
        BOUND_CHECK_REG(loc_p);
        *val_p = *loc_p;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(STLOC) {
        uint32_t val, loc;
        zis_instr_extract_operands_ABw(this_instr, val, loc);
        struct zis_object **val_p = bp + val, **loc_p = bp + loc;
        BOUND_CHECK_REG(val_p);
        BOUND_CHECK_REG(loc_p);
        *loc_p = *val_p;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDGLB) {
        uint32_t val, name;
        zis_instr_extract_operands_ABw(this_instr, val, name);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(name);
        size_t id =
            zis_module_obj_find(this_func->_module, zis_func_obj_symbol(this_func, name));
        if (zis_unlikely(id == (size_t)-1)) {
            struct zis_object *v =
                zis_module_obj_parent_get(z, this_func->_module, zis_func_obj_symbol(this_func, name));
            if (!v) {
                format_error_global_not_found(z, this_func, name);
                THROW_REG0;
            }
            id = zis_module_obj_set(z, this_func->_module, zis_func_obj_symbol(this_func, name), v);
        }
        if (zis_likely(id <= ZIS_INSTR_U16_MAX)) {
            assert(*ip == this_instr);
            assert((enum zis_opcode)zis_instr_extract_opcode(this_instr) == ZIS_OPC_LDGLB);
            this_instr = zis_instr_make_ABw(ZIS_OPC_LDGLBX, val, id);
            *ip = this_instr;
        }
        struct zis_object **val_p = bp + val;
        BOUND_CHECK_REG(val_p);
        *val_p = zis_module_obj_get_i(this_func->_module, id);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(STGLB) {
        uint32_t val, name;
        zis_instr_extract_operands_ABw(this_instr, val, name);
        struct zis_object **val_p = bp + val;
        BOUND_CHECK_REG(val_p);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(name);
        const size_t id =
            zis_module_obj_set(z, this_func->_module, zis_func_obj_symbol(this_func, name), *val_p);
        if (zis_likely(id <= ZIS_INSTR_U16_MAX)) {
            assert(*ip == this_instr);
            assert((enum zis_opcode)zis_instr_extract_opcode(this_instr) == ZIS_OPC_STGLB);
            this_instr = zis_instr_make_ABw(ZIS_OPC_STGLBX, val, id);
            *ip = this_instr;
        }
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDGLBX) {
        uint32_t val, id;
        zis_instr_extract_operands_ABw(this_instr, val, id);
        struct zis_object **val_p = bp + val;
        BOUND_CHECK_REG(val_p);
        FUNC_ENSURE;
        BOUND_CHECK_GLB(id);
        *val_p = zis_module_obj_get_i(this_func->_module, id);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(STGLBX) {
        uint32_t val, id;
        zis_instr_extract_operands_ABw(this_instr, val, id);
        struct zis_object **val_p = bp + val;
        BOUND_CHECK_REG(val_p);
        FUNC_ENSURE;
        BOUND_CHECK_GLB(id);
        zis_module_obj_set_i(this_func->_module, id, *val_p);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDFLDY) {
        uint32_t name, fld, obj_;
        zis_instr_extract_operands_ABC(this_instr, name, fld, obj_);
        struct zis_object **fld_p = bp + fld, **obj_p = bp + obj_;
        BOUND_CHECK_REG(fld_p);
        BOUND_CHECK_REG(obj_p);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(name);
        struct zis_symbol_obj *name_sym = zis_func_obj_symbol(this_func, name);
        struct zis_object *obj = *obj_p;
        struct zis_type_obj *const obj_type = zis_object_type_1(obj);
        if (obj_type == g->type_Module) {
            struct zis_module_obj *const mod = zis_object_cast(obj, struct zis_module_obj);
            struct zis_object *const val = zis_module_obj_get(mod, name_sym);
            if (zis_unlikely(!val)){
                format_error_global_not_found(z, this_func, name);
                THROW_REG0;
            }
            *fld_p = val;
        } else if (obj_type == g->type_Type) {
            struct zis_type_obj *const tp = zis_object_cast(obj, struct zis_type_obj);
            struct zis_object *const val = zis_type_obj_get_static(tp, name_sym);
            if (zis_unlikely(!val)){
                format_error_field_not_exists(z, this_func, name);
                THROW_REG0;
            }
            *fld_p = val;
        } else {
            const size_t index = zis_type_obj_find_field(obj_type, name_sym);
            if (zis_unlikely(index == (size_t)-1)) {
                format_error_field_not_exists(z, this_func, name);
                THROW_REG0;
            }
            assert(index < zis_object_slot_count(obj));
            *fld_p = zis_object_get_slot(obj, index);
        }
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(STFLDY) {
        uint32_t name, fld, obj_;
        zis_instr_extract_operands_ABC(this_instr, name, fld, obj_);
        struct zis_object **fld_p = bp + fld, **obj_p = bp + obj_;
        BOUND_CHECK_REG(fld_p);
        BOUND_CHECK_REG(obj_p);
        FUNC_ENSURE;
        BOUND_CHECK_SYM(name);
        struct zis_symbol_obj *name_sym = zis_func_obj_symbol(this_func, name);
        struct zis_object *obj = *obj_p;
        struct zis_type_obj *const obj_type = zis_object_type_1(obj);
        if (obj_type == g->type_Module) {
            struct zis_module_obj *const mod = zis_object_cast(obj, struct zis_module_obj);
            zis_module_obj_set(z, mod, name_sym, *fld_p);
        } else {
            const size_t index = zis_type_obj_find_field(obj_type, name_sym);
            if (zis_unlikely(index == (size_t)-1)) {
                format_error_field_not_exists(z, this_func, name);
                THROW_REG0;
            }
            assert(index < zis_object_slot_count(obj));
            zis_object_set_slot(obj, index, *fld_p);
        }
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDFLDX) {
        uint32_t index, fld, obj_;
        zis_instr_extract_operands_ABC(this_instr, index, fld, obj_);
        struct zis_object **fld_p = bp + fld, **obj_p = bp + obj_;
        BOUND_CHECK_REG(fld_p);
        BOUND_CHECK_REG(obj_p);
        struct zis_object *obj = *obj_p;
        BOUND_CHECK_FLD(obj, index);
        *fld_p = zis_object_get_slot(obj, index);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(STFLDX) {
        uint32_t index, fld, obj_;
        zis_instr_extract_operands_ABC(this_instr, index, fld, obj_);
        struct zis_object **fld_p = bp + fld, **obj_p = bp + obj_;
        BOUND_CHECK_REG(fld_p);
        BOUND_CHECK_REG(obj_p);
        struct zis_object *obj = *obj_p;
        BOUND_CHECK_FLD(obj, index);
        zis_object_set_slot(obj, index, *fld_p);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(LDELM) {
        uint32_t key, elm, obj;
        zis_instr_extract_operands_ABC(this_instr, key, elm, obj);
        struct zis_object **key_p = bp + key, **elm_p = bp + elm, **obj_p = bp + obj;
        BOUND_CHECK_REG(key_p);
        BOUND_CHECK_REG(elm_p);
        BOUND_CHECK_REG(obj_p);
        CALL_METHOD(elm, g->sym_operator_get_element, 2, obj, key, 0);
    }

    OP_DEFINE(STELM) {
        uint32_t key, elm, obj;
        zis_instr_extract_operands_ABC(this_instr, key, elm, obj);
        struct zis_object **key_p = bp + key, **elm_p = bp + elm, **obj_p = bp + obj;
        BOUND_CHECK_REG(key_p);
        BOUND_CHECK_REG(elm_p);
        BOUND_CHECK_REG(obj_p);
        CALL_METHOD(0, g->sym_operator_set_element, 3, obj, key, elm);
    }

    OP_DEFINE(LDELMI) {
        int32_t key; uint32_t elm, obj;
        zis_instr_extract_operands_AsBC(this_instr, key, elm, obj);
        struct zis_object **elm_p = bp + elm, **obj_p = bp + obj;
        BOUND_CHECK_REG(elm_p);
        BOUND_CHECK_REG(obj_p);
        zis_debug_log_when(
            elm == 0 || obj == 0,
            ERROR, "Interp", "\"LDELMI %i, %u, %u\": REG-0 is occupied by operands",
            key, elm, obj
        );
        bp[0] = zis_smallint_to_ptr(key);
        CALL_METHOD(elm, g->sym_operator_get_element, 2, obj, 0/*key*/, 0);
    }

    OP_DEFINE(STELMI) {
        int32_t key; uint32_t elm, obj;
        zis_instr_extract_operands_AsBC(this_instr, key, elm, obj);
        struct zis_object **elm_p = bp + elm, **obj_p = bp + obj;
        BOUND_CHECK_REG(elm_p);
        BOUND_CHECK_REG(obj_p);
        zis_debug_log_when(
            elm == 0 || obj == 0,
            ERROR, "Interp", "\"STELMI %i, %u, %u\": REG-0 is occupied by operands",
            key, elm, obj
        );
        bp[0] = zis_smallint_to_ptr(key);
        CALL_METHOD(0, g->sym_operator_set_element, 3, obj, 0/*key*/, elm);
    }

    OP_DEFINE(JMP) {
        int32_t offset;
        zis_instr_extract_operands_Asw(this_instr, offset);
        IP_JUMP_TO(ip + offset);
        OP_DISPATCH;
    }

    OP_DEFINE(JMPT) {
        int32_t offset; uint32_t cond;
        zis_instr_extract_operands_AsBw(this_instr, offset, cond);
        struct zis_object **cond_p = bp + cond;
        BOUND_CHECK_REG(cond_p);
        struct zis_object *cond_v = *cond_p;
        if (cond_v == zis_object_from(g->val_true)) {
            IP_JUMP_TO(ip + offset);
        } else {
            if (zis_unlikely(cond_v != zis_object_from(g->val_false))) {
                format_error_cond_is_not_bool(z, cond_v);
                THROW_REG0;
            }
            IP_ADVANCE;
        }
        OP_DISPATCH;
    }

    OP_DEFINE(JMPF) {
        int32_t offset; uint32_t cond;
        zis_instr_extract_operands_AsBw(this_instr, offset, cond);
        struct zis_object **cond_p = bp + cond;
        BOUND_CHECK_REG(cond_p);
        struct zis_object *cond_v = *cond_p;
        if (cond_v == zis_object_from(g->val_false)) {
            IP_JUMP_TO(ip + offset);
        } else {
            if (zis_unlikely(cond_v != zis_object_from(g->val_true))) {
                format_error_cond_is_not_bool(z, cond_v);
                THROW_REG0;
            }
            IP_ADVANCE;
        }
        OP_DISPATCH;
    }

    OP_DEFINE(JMPLE) {
        int32_t offset; uint32_t lhs, rhs;
        zis_instr_extract_operands_AsBC(this_instr, offset, lhs, rhs);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        if (cmp_res != ZIS_OBJECT_GT)
            IP_JUMP_TO(ip + offset);
        else
            IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(JMPLT) {
        int32_t offset; uint32_t lhs, rhs;
        zis_instr_extract_operands_AsBC(this_instr, offset, lhs, rhs);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        if (cmp_res == ZIS_OBJECT_LT)
            IP_JUMP_TO(ip + offset);
        else
            IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(JMPEQ) {
        int32_t offset; uint32_t lhs, rhs;
        zis_instr_extract_operands_AsBC(this_instr, offset, lhs, rhs);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const bool eq = zis_object_equals(z, lhs_v, rhs_v);
        if (eq)
            IP_JUMP_TO(ip + offset);
        else
            IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(JMPGT) {
        int32_t offset; uint32_t lhs, rhs;
        zis_instr_extract_operands_AsBC(this_instr, offset, lhs, rhs);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        if (cmp_res == ZIS_OBJECT_GT)
            IP_JUMP_TO(ip + offset);
        else
            IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(JMPGE) {
        int32_t offset; uint32_t lhs, rhs;
        zis_instr_extract_operands_AsBC(this_instr, offset, lhs, rhs);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        if (cmp_res != ZIS_OBJECT_LT)
            IP_JUMP_TO(ip + offset);
        else
            IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(JMPNE) {
        int32_t offset; uint32_t lhs, rhs;
        zis_instr_extract_operands_AsBC(this_instr, offset, lhs, rhs);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const bool eq = zis_object_equals(z, lhs_v, rhs_v);
        if (!eq)
            IP_JUMP_TO(ip + offset);
        else
            IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(CMP) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        do {
            zis_smallint_t result;
            if (lhs_v == rhs_v)
                result = 0;
            else if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v))
                result = zis_smallint_from_ptr(lhs_v) < zis_smallint_from_ptr(rhs_v) ? -1 : 1;
            else
                break;
            *tgt_p = zis_smallint_to_ptr(result);
            IP_ADVANCE;
            OP_DISPATCH;
        } while (0);
        CALL_METHOD(tgt, g->sym_operator_cmp, 2, lhs, rhs, 0);
    }

    OP_DEFINE(CMPLE) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        *tgt_p = zis_object_from(cmp_res != ZIS_OBJECT_GT ? g->val_true : g->val_false);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(CMPLT) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        *tgt_p = zis_object_from(cmp_res == ZIS_OBJECT_LT ? g->val_true : g->val_false);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(CMPEQ) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        do {
            struct zis_bool_obj *result;
            if (lhs_v == rhs_v)
                result = g->val_true;
            else if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v))
                result = g->val_false;
            else
                break;
            *tgt_p = zis_object_from(result);
            IP_ADVANCE;
            OP_DISPATCH;
        } while (0);
        CALL_METHOD(tgt, g->sym_operator_equ, 2, lhs, rhs, 0);
    }

    OP_DEFINE(CMPGT) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        *tgt_p = zis_object_from(cmp_res == ZIS_OBJECT_GT ? g->val_true : g->val_false);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(CMPGE) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const enum zis_object_ordering cmp_res = zis_object_compare(z, lhs_v, rhs_v);
        if (zis_unlikely(cmp_res == ZIS_OBJECT_IC))
            THROW_REG0;
        *tgt_p = zis_object_from(cmp_res != ZIS_OBJECT_LT ? g->val_true : g->val_false);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(CMPNE) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        const bool eq = zis_object_equals(z, lhs_v, rhs_v);
        *tgt_p = zis_object_from(!eq ? g->val_true : g->val_false);
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(ADD) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            *tgt_p = zis_smallint_to_ptr(lhs_smi + rhs_smi);
            // FIXME: overflow check
            IP_ADVANCE;
            OP_DISPATCH;
        }
        CALL_METHOD(tgt, g->sym_operator_add, 2, lhs, rhs, 0);
    }

    OP_DEFINE(SUB) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            *tgt_p = zis_smallint_to_ptr(lhs_smi - rhs_smi);
            // FIXME: overflow check
            IP_ADVANCE;
            OP_DISPATCH;
        }
        CALL_METHOD(tgt, g->sym_operator_sub, 2, lhs, rhs, 0);
    }

    OP_DEFINE(MUL) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            *tgt_p = zis_smallint_to_ptr(lhs_smi * rhs_smi);
            // FIXME: overflow check
            IP_ADVANCE;
            OP_DISPATCH;
        }
        CALL_METHOD(tgt, g->sym_operator_mul, 2, lhs, rhs, 0);
    }

    OP_DEFINE(DIV) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            *tgt_p = zis_object_from(zis_float_obj_new(z, (double)lhs_smi / (double)rhs_smi));
            IP_ADVANCE;
            OP_DISPATCH;
        }
        CALL_METHOD(tgt, g->sym_operator_div, 2, lhs, rhs, 0);;
    }

    OP_DEFINE(REM) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            *tgt_p = zis_smallint_to_ptr(lhs_smi % rhs_smi);
            IP_ADVANCE;
            OP_DISPATCH;
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "%", 1), 2, lhs, rhs, 0);
    }

    OP_DEFINE(POW) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "**", 2), 2, lhs, rhs, 0);
    }

    OP_DEFINE(SHL) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "<<", 2), 2, lhs, rhs, 0);
    }

    OP_DEFINE(SHR) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, ">>", 2), 2, lhs, rhs, 0);
    }

    OP_DEFINE(BITAND) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            if (lhs_smi >= 0 && rhs_smi >= 0) {
                *tgt_p = zis_smallint_to_ptr(lhs_smi & rhs_smi);
                IP_ADVANCE;
                OP_DISPATCH;
            }
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "&", 1), 2, lhs, rhs, 0);
    }

    OP_DEFINE(BITOR) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            if (lhs_smi >= 0 && rhs_smi >= 0) {
                *tgt_p = zis_smallint_to_ptr(lhs_smi | rhs_smi);
                IP_ADVANCE;
                OP_DISPATCH;
            }
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "|", 1), 2, lhs, rhs, 0);
    }

    OP_DEFINE(BITXOR) {
        uint32_t tgt, lhs, rhs;
        zis_instr_extract_operands_ABC(this_instr, tgt, lhs, rhs);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *lhs_v, *rhs_v;
        {
            struct zis_object **lhs_p = bp + lhs;
            BOUND_CHECK_REG(lhs_p);
            lhs_v = *lhs_p;
            struct zis_object **rhs_p = bp + rhs;
            BOUND_CHECK_REG(rhs_p);
            rhs_v = *rhs_p;
        }
        if (zis_object_is_smallint(lhs_v) && zis_object_is_smallint(rhs_v)) {
            const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs_v);
            const zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs_v);
            if (lhs_smi >= 0 && rhs_smi >= 0) {
                *tgt_p = zis_smallint_to_ptr(lhs_smi ^ rhs_smi);
                IP_ADVANCE;
                OP_DISPATCH;
            }
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "^", 1), 2, lhs, rhs, 0);
    }

    OP_DEFINE(NOT) {
        uint32_t tgt, val;
        zis_instr_extract_operands_ABw(this_instr, tgt, val);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *val_v;
        {
            struct zis_object **val_p = bp + val;
            BOUND_CHECK_REG(val_p);
            val_v = *val_p;
        }
        if (val_v == zis_object_from(g->val_true))
            val_v = zis_object_from(g->val_false);
        else if (val_v == zis_object_from(g->val_false))
            val_v = zis_object_from(g->val_true);
        else
            format_error_cond_is_not_bool(z, val_v);
        *tgt_p = val_v;
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(NEG) {
        uint32_t tgt, val;
        zis_instr_extract_operands_ABw(this_instr, tgt, val);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *val_v;
        {
            struct zis_object **val_p = bp + val;
            BOUND_CHECK_REG(val_p);
            val_v = *val_p;
        }
        if (zis_object_is_smallint(val_v)) {
            const zis_smallint_t val_smi = zis_smallint_from_ptr(val_v);
            if (zis_likely(val_smi != ZIS_SMALLINT_MIN)) {
                *tgt_p = zis_smallint_to_ptr(-val_smi);
                IP_ADVANCE;
                OP_DISPATCH;
            }
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "-#", 2), 1, val, 0, 0);
    }

    OP_DEFINE(BITNOT) {
        uint32_t tgt, val;
        zis_instr_extract_operands_ABw(this_instr, tgt, val);
        struct zis_object **tgt_p = bp + tgt;
        BOUND_CHECK_REG(tgt_p);
        struct zis_object *val_v;
        {
            struct zis_object **val_p = bp + val;
            BOUND_CHECK_REG(val_p);
            val_v = *val_p;
        }
        if (zis_object_is_smallint(val_v)) {
            const zis_smallint_t val_smi = zis_smallint_from_ptr(val_v);
            if (val_smi >= 0) {
                *tgt_p = zis_smallint_to_ptr(~val_smi);
                IP_ADVANCE;
                OP_DISPATCH;
            }
        }
        CALL_METHOD(tgt, zis_symbol_registry_get(z, "~", 1), 1, val, 0, 0);
    }

    OP_UNDEFINED {
        zis_debug_log(FATAL, "Interp", "unknown opcode %#04x", zis_instr_extract_opcode(this_instr));
        goto panic_ill;
    }

#undef THROW_REG0
#undef CALL_METHOD
#undef BOUND_CHECK_REG
#undef BOUND_CHECK_SYM
#undef BOUND_CHECK_CON
#undef BOUND_CHECK_GLB
#undef BOUND_CHECK_FLD

panic_ill:
    zis_debug_log_1(DUMP, "Interp", "zis_debug_dump_bytecode()", fp, {
        FUNC_ENSURE;
        zis_debug_dump_bytecode(z, this_func, (uint32_t)(ip - this_func->bytecode), fp);
    });
    zis_context_panic(z, ZIS_CONTEXT_PANIC_ILL);

#if OP_DISPATCH_USE_COMPUTED_GOTO // vvv

#undef OP_LABEL
#undef OP_DEFINE
#undef OP_UNDEFINED
#undef OP_DISPATCH

#pragma GCC diagnostic pop

#else // ^^^ OP_DISPATCH_USE_COMPUTED_GOTO | !OP_DISPATCH_USE_COMPUTED_GOTO vvv

    }

#undef OP_DEFINE
#undef OP_UNDEFINED
#undef OP_DISPATCH

#endif // ^^^ OP_DISPATCH_USE_COMPUTED_GOTO

#undef IP_ADVANCE
#undef IP_JUMP_TO

#undef BP_SP_CHANGED

#undef FUNC_ENSURE
#undef FUNC_CHANGED
#undef FUNC_CHANGED_TO

#undef OP_DISPATCH_USE_COMPUTED_GOTO
}

/* ----- public functions --------------------------------------------------- */

zis_noinline zis_cold_fn static void
invoke_prepare_xx_format_error_method_api_bad_usage(struct zis_context *z) {
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "type", NULL, "illegal method invocation");
    zis_context_set_reg0(z, zis_object_from(exc));
}

/// Extract method function and store to REG-0.
/// On error, make an exception (REG-0) and returns false.
static bool invoke_prepare_xx_extract_method(
    struct zis_context *z,
    struct zis_object *object,
    struct zis_object *method_name_v
) {
    if (zis_unlikely(!zis_object_type_is(method_name_v, z->globals->type_Symbol))) {
        invoke_prepare_xx_format_error_method_api_bad_usage(z);
        return false;
    }

    struct zis_symbol_obj *const method_name =
        zis_object_cast(method_name_v, struct zis_symbol_obj);
    struct zis_object *const method_func = zis_type_obj_get_method(
        zis_likely(!zis_object_is_smallint(object)) ?
            zis_object_type(object) : z->globals->type_Int,
        zis_object_cast(method_name, struct zis_symbol_obj)
    );

    if (zis_unlikely(!method_func)) {
        struct zis_exception_obj *exc = zis_exception_obj_format(
            z, "type", NULL, "no method %.*s",
            (int)zis_symbol_obj_data_size(method_name),
            zis_symbol_obj_data(method_name)
        );
        zis_context_set_reg0(z, zis_object_from(exc));
        return false;
    }

    z->callstack->frame[0] = method_func;
    return true;
}

static void invoke_prepare_xx_pass_args_fail_cleanup(
    struct zis_context *z, const struct invocation_info *restrict ii
) {
    assert(zis_object_type_is(z->callstack->frame[0], z->globals->type_Exception));
    assert(zis_object_type_is(ii->caller_frame[0], z->globals->type_Function));
    zis_exception_obj_stack_trace(
        z,
        zis_object_cast(z->callstack->frame[0], struct zis_exception_obj),
        zis_object_cast(ii->caller_frame[0], struct zis_func_obj),
        NULL
    );
    void *ret_ip = invocation_leave(z, z->callstack->frame[0]);
    assert(!ret_ip), zis_unused_var(ret_ip);
}

struct zis_func_obj *zis_invoke_prepare_va(
    struct zis_context *z, struct zis_object *callable /* = NULL */,
    struct zis_object **ret_to /* = NULL */, struct zis_object **argv, size_t argc
) {
    struct invocation_info ii;
    if (callable) {
        z->callstack->frame[0] = callable;
    } else {
        if (zis_unlikely(!argc)) {
            invoke_prepare_xx_format_error_method_api_bad_usage(z);
            return NULL;
        }
        if (!invoke_prepare_xx_extract_method(z, argv[0], z->callstack->frame[0]))
            return NULL;
    }
    if (zis_unlikely(!invocation_enter(z, NULL, ret_to ? ret_to : z->callstack->frame, &ii))) {
        return NULL;
    }
    assert(!argv || argv > ii.caller_frame);
    if (zis_unlikely(!invocation_pass_args_vec(z, argv, argc, &ii))) {
        invoke_prepare_xx_pass_args_fail_cleanup(z, &ii);
        return NULL;
    }
    assert(zis_object_type_is(ii.caller_frame[0], z->globals->type_Function));
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

struct zis_func_obj *zis_invoke_prepare_pa(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object **ret_to /* = NULL */, struct zis_object *packed_args, size_t argc
) {
    struct invocation_info ii;
    assert(callable); // not nullable
    z->callstack->frame[0] = callable;
    if (zis_unlikely(!invocation_enter(z, NULL, ret_to ? ret_to : z->callstack->frame, &ii))) {
        return NULL;
    }
    assert(packed_args != ii.caller_frame[0]);
    if (zis_unlikely(!invocation_pass_args_pac(z, packed_args, argc, &ii))) {
        invoke_prepare_xx_pass_args_fail_cleanup(z, &ii);
        return NULL;
    }
    assert(zis_object_type_is(ii.caller_frame[0], z->globals->type_Function));
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

struct zis_func_obj *zis_invoke_prepare_da(
    struct zis_context *z, struct zis_object *callable /* = NULL */,
    struct zis_object **ret_to /* = NULL */, const unsigned int arg_regs[], size_t argc
) {
    struct invocation_info ii;
    if (callable) {
        z->callstack->frame[0] = callable;
    } else {
        struct zis_object **const frame = z->callstack->frame;
        if (zis_unlikely(!argc)) {
            invoke_prepare_xx_format_error_method_api_bad_usage(z);
            return NULL;
        }
        assert(frame + arg_regs[0] <= z->callstack->top);
        if (!invoke_prepare_xx_extract_method(z, frame[arg_regs[0]], frame[0]))
            return NULL;
    }
    if (zis_unlikely(!invocation_enter(z, NULL, ret_to ? ret_to : z->callstack->frame, &ii))) {
        return NULL;
    }
    if (zis_unlikely(!invocation_pass_args_dis(z, arg_regs, argc, &ii))) {
        invoke_prepare_xx_pass_args_fail_cleanup(z, &ii);
        return NULL;
    }
    assert(zis_object_type_is(ii.caller_frame[0], z->globals->type_Function));
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func) {
    assert(zis_object_from(func) == zis_callstack_frame_info(z->callstack)->prev_frame[0]);
    const zis_native_func_t c_func = func->native;
    if (zis_unlikely(c_func)) {
        const int status = c_func(z);
        if (zis_unlikely(status == ZIS_THR)) {
            const struct zis_callstack_frame_info *const frame_info = zis_callstack_frame_info(z->callstack);
            struct zis_object **const frame = z->callstack->frame;
            if (zis_object_type_is(frame[0], z->globals->type_Exception)) {
                struct zis_exception_obj *exc_obj = zis_object_cast(frame[0], struct zis_exception_obj);
                assert(zis_object_type_is(frame_info->prev_frame[0], z->globals->type_Function));
                struct zis_func_obj *func_obj = zis_object_cast(frame_info->prev_frame[0], struct zis_func_obj);
                assert(func_obj->native == c_func);
                zis_exception_obj_stack_trace(z, exc_obj, func_obj, NULL);
            }
            frame_info->prev_frame[0] = frame[0];
        }
        void *ret_ip = invocation_leave(z, z->callstack->frame[0]);
        assert(!ret_ip), zis_unused_var(ret_ip);
        return status;
    }
    return invoke_bytecode_func(z, func);
}

int zis_invoke_vn(
    struct zis_context *z, struct zis_object **ret_to /* = NULL */,
    struct zis_object *callable /* = NULL */, struct zis_object *argv[], size_t argc
) {
    struct zis_func_obj *const func_obj =
        zis_invoke_prepare_va(z, callable, ret_to, argv, argc);
    if (zis_unlikely(!func_obj))
        return ZIS_THR;
    return zis_invoke_func(z, func_obj);
}

int zis_invoke_v(
    struct zis_context *z, struct zis_object **ret_to /* = NULL */,
    struct zis_object *callable /* = NULL */, struct zis_object *args[]/* {arg1, arg2, ..., NULL} or NULL */
) {
    size_t argc = 0;
    for (struct zis_object **p = args; *p; p++)
        argc++;
    return zis_invoke_vn(z, ret_to, callable, args, argc);
}

int zis_invoke_l(
    struct zis_context *z, struct zis_object **ret_to /* = NULL */,
    struct zis_object *callable /* = NULL */, ... /* arg1, arg2, ..., NULL */
) {
    va_list ap;
    size_t argc = 0, alloc_n = 4;
    struct zis_object **args = zis_callstack_frame_alloc_temp(z, alloc_n);
    va_start(ap, callable);
    while (true) {
        struct zis_object *arg = va_arg(ap, struct zis_object *);
        if (!arg)
            break;
        assert(argc <= alloc_n);
        if (zis_unlikely(argc == alloc_n)) {
            const size_t n = 4;
            alloc_n += n;
            struct zis_object **p = zis_callstack_frame_alloc_temp(z, n);
            zis_unused_var(p), assert(p == args + argc);
        }
        args[argc++] = arg;
    }
    va_end(ap);

    const int status = zis_invoke_vn(z, ret_to, callable, args, argc);
    zis_callstack_frame_free_temp(z, alloc_n);
    return status;
}
