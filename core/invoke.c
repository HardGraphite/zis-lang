#include "invoke.h"

#include <math.h>

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "instr.h"
#include "ndefutil.h"
#include "object.h"
#include "stack.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "floatobj.h"
#include "funcobj.h"
#include "mapobj.h"
#include "tupleobj.h"
#include "typeobj.h"

#include "zis_config.h" // ZIS_BUILD_CGOTO

/* ----- invocation tools --------------------------------------------------- */

zis_noinline zis_cold_fn static void
format_error_type(struct zis_context *z, struct zis_object *fn) {
    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, "type", fn, "not callable");
    z->callstack->frame[0] = zis_object_from(exc);
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
    z->callstack->frame[0] = zis_object_from(exc);
}

struct invocation_info {
    struct zis_object      **caller_frame;
    size_t                   arg_shift;
    struct zis_func_obj_meta func_meta;
};

/// Enter a new frame for a invocation.
/// REG-0 (caller frame) is the object to call and will be replaced with the real function object.
/// On error, make an exception (REG-0) and returns false.
zis_static_force_inline bool invocation_enter(
    struct zis_context *z,
    zis_instr_word_t *caller_ip,
    struct zis_object **ret_val_reg,
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
    zis_callstack_enter(z->callstack, callee_frame_size, caller_ip, ret_val_reg);

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

/// Pass arguments (packed).
zis_static_force_inline bool invocation_pass_args_pac(
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
    zis_object_assert_no_write_barrier(va_list_);

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

zis_hot_fn static int exec_bytecode(
    struct zis_context *z, struct zis_func_obj *func_obj
) {
#define OP_DISPATCH_USE_COMPUTED_GOTO ZIS_USE_COMPUTED_GOTO

    zis_instr_word_t *ip = func_obj->bytecode; // The instruction pointer.
    zis_instr_word_t this_instr = *ip;

#define IP_ADVANCE     (this_instr = *++ip)
#define IP_JUMP_TO(X)  (ip = (X), this_instr = *ip)

    struct zis_callstack *const stack = z->callstack;
    struct zis_object **bp = stack->frame;
    struct zis_object **sp = stack->top;

#define BP_SP_CHANGED  (bp = stack->frame, sp = stack->top)

    /* func_obj; */
    assert(zis_callstack_frame_info(stack)->prev_frame[0] == zis_object_from(func_obj));
    uint32_t func_sym_count = (uint32_t)zis_func_obj_symbol_count(func_obj);
    uint32_t func_const_count = (uint32_t)zis_func_obj_constant_count(func_obj);

#define FUNC_ENSURE \
    do {  /* func_obj shall not be moved */ \
        assert((void *)func_obj == (void *)zis_callstack_frame_info(stack)->prev_frame[0]); \
        assert(zis_object_type((struct zis_object *)func_obj) == g->type_Function);         \
        assert((size_t)func_sym_count == zis_func_obj_symbol_count(func_obj));     \
        assert((size_t)func_const_count == zis_func_obj_constant_count(func_obj)); \
    } while (0)
#define FUNC_CHANGED \
    do {             \
        struct zis_object *p = zis_callstack_frame_info(stack)->prev_frame[0]; \
        assert(zis_object_type(p) == g->type_Function);                        \
        func_obj = zis_object_cast(p, struct zis_func_obj);                    \
        func_sym_count = (uint32_t)zis_func_obj_symbol_count(func_obj);        \
        func_const_count = (uint32_t)zis_func_obj_constant_count(func_obj);    \
    } while (0)
#define FUNC_CHANGED_TO(NEW_FUNC) \
    do {                          \
        func_obj = (NEW_FUNC);    \
        assert((void *)func_obj == (void *)zis_callstack_frame_info(stack)->prev_frame[0]); \
        assert(zis_object_type((struct zis_object *)func_obj) == g->type_Function);         \
        func_sym_count = (uint32_t)zis_func_obj_symbol_count(func_obj);        \
        func_const_count = (uint32_t)zis_func_obj_constant_count(func_obj);    \
    } while (0)

    struct zis_context_globals *const g = z->globals;

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

#define THROW_REG0 \
    do {           \
        this_instr = 0; \
        goto _do_throw; \
    } while (0)
#define BOUND_CHECK_REG(PTR) \
    do {                     \
        assert(PTR >= bp);   \
        if (zis_unlikely(PTR > sp)) { \
            zis_debug_log(FATAL, "Interp", "register index out of range"); \
            goto panic_ill;  \
        }                    \
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

    OP_DEFINE(NOP) {
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(ARG) {
        zis_debug_log(FATAL, "Interp", "unexpected opcode %#04x", ZIS_OPC_ARG);
        goto panic_ill;
    }

    OP_DEFINE(LDNIL) {
        uint32_t tgt, count;
        zis_instr_extract_operands_ABw(this_instr, tgt, count);
        struct zis_object **tgt_p = bp + tgt, **tgt_last_p = tgt_p + count;
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
        *tgt_p = zis_func_obj_constant(func_obj, id);
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
        *tgt_p = zis_func_obj_symbol(func_obj, id);
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
        *tgt_p = zis_object_from(zis_float_obj_new(z, ldexp(frac, exp - 7)));
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKTUP) {
        uint32_t tgt, val_start, val_count;
        zis_instr_extract_operands_ABC(this_instr, tgt, val_start, val_count);
        struct zis_object **tgt_p = bp + tgt, **val_p = bp + val_start;
        BOUND_CHECK_REG(tgt_p);
        BOUND_CHECK_REG(val_p + val_count - 1);
        *tgt_p = zis_object_from(zis_tuple_obj_new(z, val_p, val_count));
        IP_ADVANCE;
        OP_DISPATCH;
    }

    OP_DEFINE(MKARR) {
        uint32_t tgt, val_start, val_count;
        zis_instr_extract_operands_ABC(this_instr, tgt, val_start, val_count);
        struct zis_object **tgt_p = bp + tgt, **val_p = bp + val_start;
        BOUND_CHECK_REG(tgt_p);
        BOUND_CHECK_REG(val_p + val_count - 1);
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
        zis_map_obj_new_r(z, tgt_p, 0.0f, val_count);
        if (val_count) {
            BOUND_CHECK_REG(val_end_p - 1);
            struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 4);
            for (; val_p < val_end_p; val_p += 2) {
                tmp_regs[0] = *tgt_p, tmp_regs[1] = val_p[0], tmp_regs[2] = val_p[1];
                if (zis_map_obj_set_r(z, tmp_regs) != ZIS_OK)
                    THROW_REG0;
            }
            zis_callstack_frame_free_temp(z, 4);
            assert(stack->top == sp);
        }
        IP_ADVANCE;
        OP_DISPATCH;
    }

    _do_throw:
    OP_DEFINE(THR) {
        uint32_t val;
        zis_instr_extract_operands_Aw(this_instr, val);
        struct zis_object **val_p = bp + val;
        BOUND_CHECK_REG(val_p);
        const bool val_is_exc =
            !zis_object_is_smallint(*val_p) && zis_object_type(*val_p) == g->type_Exception;
        FUNC_ENSURE;
        while (true) {
            if (val_is_exc) {
                assert(zis_object_type(*val_p) == g->type_Exception);
                struct zis_exception_obj *exc = zis_object_cast(*val_p, struct zis_exception_obj);
                zis_exception_obj_stack_trace(z, exc, func_obj, ip);
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
        FUNC_CHANGED_TO(zis_object_cast(ii.caller_frame[0], struct zis_func_obj));
        unsigned int indices[3];
        for (uint32_t i = 0; i < argc; i++) {
            const unsigned int idx = (this_instr >> (7 + 6 * i)) & 63;
            BOUND_CHECK_REG(ii.caller_frame + idx);
            indices[i] = idx;
        }
        if (zis_unlikely(!invocation_pass_args_dis(z, indices, argc, &ii)))
            THROW_REG0;
    } {
    _do_call_func_obj:
        if (func_obj->native) {
            const int status = func_obj->native(z);
            if (zis_unlikely(status == ZIS_THR))
                THROW_REG0;
            assert(status == ZIS_OK);
            zis_instr_word_t *ip0 = invocation_leave(z, bp[0]);
            assert(ip0 == ip), zis_unused_var(ip0);
            FUNC_CHANGED;
            IP_ADVANCE;
        } else {
            assert(zis_func_obj_bytecode_length(func_obj));
            IP_JUMP_TO(func_obj->bytecode);
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
        struct zis_type_obj *t = zis_object_type(a);
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
        FUNC_CHANGED_TO(zis_object_cast(ii.caller_frame[0], struct zis_func_obj));
        if (zis_unlikely(!invocation_pass_args_pac(z, *arg_p, argc, &ii)))
            THROW_REG0;
        goto _do_call_func_obj;
    }

    OP_UNDEFINED {
        zis_debug_log(FATAL, "Interp", "unknown opcode %#04x", zis_instr_extract_opcode(this_instr));
        goto panic_ill;
    }

#undef THROW_REG0
#undef BOUND_CHECK_REG
#undef BOUND_CHECK_SYM
#undef BOUND_CHECK_CON

panic_ill:
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

static void invoke_prepare_xx_pass_args_fail_cleanup(
    struct zis_context *z, const struct invocation_info *restrict ii
) {
    assert(zis_object_type(z->callstack->frame[0]) == z->globals->type_Exception);
    assert(zis_object_type(ii->caller_frame[0]) == z->globals->type_Function);
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
    struct zis_context *z, struct zis_object *callable,
    struct zis_object **argv, size_t argc
) {
    struct invocation_info ii;
    z->callstack->frame[0] = callable;
    if (zis_unlikely(!invocation_enter(z, NULL, z->callstack->frame, &ii))) {
        return NULL;
    }
    assert(!argv || argv > ii.caller_frame);
    if (zis_unlikely(!invocation_pass_args_vec(z, argv, argc, &ii))) {
        invoke_prepare_xx_pass_args_fail_cleanup(z, &ii);
        return NULL;
    }
    assert(zis_object_type(ii.caller_frame[0]) == z->globals->type_Function);
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

struct zis_func_obj *zis_invoke_prepare_pa(
    struct zis_context *z, struct zis_object *callable,
    struct zis_object *packed_args, size_t argc
) {
    struct invocation_info ii;
    z->callstack->frame[0] = callable;
    if (zis_unlikely(!invocation_enter(z, NULL, z->callstack->frame, &ii))) {
        return NULL;
    }
    assert(packed_args != ii.caller_frame[0]);
    if (zis_unlikely(!invocation_pass_args_pac(z, packed_args, argc, &ii))) {
        invoke_prepare_xx_pass_args_fail_cleanup(z, &ii);
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
    if (zis_unlikely(!invocation_enter(z, NULL, z->callstack->frame, &ii))) {
        return NULL;
    }
    if (zis_unlikely(!invocation_pass_args_dis(z, arg_regs, argc, &ii))) {
        invoke_prepare_xx_pass_args_fail_cleanup(z, &ii);
        return NULL;
    }
    assert(zis_object_type(ii.caller_frame[0]) == z->globals->type_Function);
    return zis_object_cast(ii.caller_frame[0], struct zis_func_obj);
}

struct zis_object *zis_invoke_cleanup(struct zis_context *z) {
    struct zis_object *ret_val = z->callstack->frame[0];
    void *ret_ip = invocation_leave(z, ret_val);
    assert(!ret_ip), zis_unused_var(ret_ip);
    return ret_val;
}

int zis_invoke_func(struct zis_context *z, struct zis_func_obj *func) {
    assert(zis_object_from(func) == zis_callstack_frame_info(z->callstack)->prev_frame[0]);
    const zis_native_func_t c_func = func->native;
    if (zis_unlikely(c_func)) {
        const int status = c_func(z);
        if (
            zis_unlikely(status == ZIS_THR) &&
            !zis_object_is_smallint(z->callstack->frame[0]) &&
            zis_object_type(z->callstack->frame[0]) == z->globals->type_Exception
        ) {
            struct zis_exception_obj *exc_obj =
                zis_object_cast(z->callstack->frame[0], struct zis_exception_obj);
            assert(zis_object_type(zis_callstack_frame_info(z->callstack)->prev_frame[0]) == z->globals->type_Function);
            struct zis_func_obj *func_obj =
                zis_object_cast(zis_callstack_frame_info(z->callstack)->prev_frame[0], struct zis_func_obj);
            assert(func_obj->native == c_func);
            zis_exception_obj_stack_trace(z, exc_obj, func_obj, NULL);
        }
        return status;
    }
    return exec_bytecode(z, func);
}
