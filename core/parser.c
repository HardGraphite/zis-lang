#include "parser.h"

#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "lexer.h"
#include "locals.h"
#include "memory.h"
#include "object.h"
#include "objmem.h"
#include "token.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "stringobj.h"
#include "symbolobj.h"

#if ZIS_FEATURE_SRC

/* ----- the parser structure ----------------------------------------------- */

struct zis_parser {
    struct zis_lexer lexer;
    struct zis_token token;
    struct zis_locals_root locals_root;
    struct zis_ast_node_obj *ast_root_node;
    jmp_buf error_jb;
#if ZIS_DEBUG_LOGGING
    unsigned int tree_depth;
#endif // ZIS_DEBUG_LOGGING
};

static void parser_gc_visit(void *_p, enum zis_objmem_obj_visit_op op) {
    struct zis_parser *const p = _p;
    _zis_lexer_gc_visit(&p->lexer, (int)op);
    zis_objmem_visit_object_vec(
        (struct zis_object **)&p->token.value,
        (struct zis_object **)&p->token.value + 1,
        op
    );
    _zis_locals_root_gc_visit(&p->locals_root, (int)op);
}

static struct zis_context *parser_z(struct zis_parser *restrict p) {
    return p->lexer.z;
}

/// Error handling: setjmp().
#define parser_error_setjmp(__parser) \
    (setjmp((__parser)->error_jb))

/// Error handling: longjmp().
#define parser_error_longjmp(__parser) \
    (longjmp((__parser)->error_jb, 1))

zis_noreturn zis_cold_fn static void parser_lexer_error_handler(
    struct zis_lexer *l, const char *restrict msg
) {
    struct zis_parser *const p =
        (struct zis_parser *)((char *)l - offsetof(struct zis_parser, lexer));
    assert(&p->lexer == l);
    struct zis_exception_obj *exc = zis_exception_obj_format(
        l->z, "syntax", NULL,
        "%u:%u: %s", l->line, l->column, msg
    );
    zis_context_set_reg0(l->z, zis_object_from(exc));
    parser_error_longjmp(p);
}

/// Format an error and do longjump.
zis_printf_fn_attrs(4, 5) zis_noreturn zis_noinline zis_cold_fn
static void error(
    struct zis_parser *restrict p,
    unsigned int line, unsigned int column,
    zis_printf_fn_arg_fmtstr const char *restrict fmt, ...
) {
    char msg_buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, sizeof msg_buf, fmt, ap);
    va_end(ap);
    zis_debug_log(WARN, "Parser", "error@(%u,%u): %s", line, column, msg_buf);
    struct zis_context *z = parser_z(p);
    struct zis_exception_obj *exc = zis_exception_obj_format(
        z, "syntax", NULL,
        "%u:%u: %s", line, column, msg_buf
    );
    zis_context_set_reg0(z, zis_object_from(exc));
    parser_error_longjmp(p);
}

/// Get reference to the current token.
static const struct zis_token *this_token(const struct zis_parser *restrict p) {
    return &p->token;
}

/// Scan and generate the next token.
static void next_token(struct zis_parser *restrict p) {
    zis_lexer_next(&p->lexer, &p->token);
}

#define parser_debug_log_node(__p, __type) \
    (zis_debug_log(                        \
        TRACE, "Parser", "%-*c<%s loc=\"%u:%u\" />", \
        (int)(__p)->tree_depth + 1, ' ',       \
        (__type), (__p)->token.line0, (__p)->token.column0 \
    ))

#define parser_debug_log_node_begin(__p, __type) \
    (zis_debug_log(                              \
        TRACE, "Parser", "%-*c<%s loc=\"%u:%u\">", \
        (int)++(__p)->tree_depth, ' ',             \
        (__type), (__p)->token.line0, (__p)->token.column0 \
    ))

#define parser_debug_log_node_end(__p, __type) \
    (zis_debug_log(TRACE, "Parser", "%-*c</%s>", (int)(__p)->tree_depth--, ' ', (__type)))

/* ----- convinient functions ----------------------------------------------- */

/// No-token.
#define NTOK ((enum zis_token_type)-1)

zis_noreturn zis_noinline zis_cold_fn static void
error_unexpected_token(struct zis_parser *restrict p, enum zis_token_type expected_tt /*=NTOK*/) {
    const struct zis_token *tok = this_token(p);
    const char *tok_tt_s = zis_token_type_represent(tok->type);
    if (expected_tt == NTOK)
        error(p, tok->line0, tok->column0, "unexpected %s", tok_tt_s);
    error(
        p, tok->line0, tok->column0, "expected %s before %s",
        zis_token_type_represent(expected_tt), tok_tt_s
    );
}

zis_noreturn zis_noinline zis_cold_fn static void error_unexpected_node(
    struct zis_parser *restrict p,
    struct zis_ast_node_obj *node, enum zis_ast_node_type expected_nt
) {
    const struct zis_ast_node_obj_location *const loc = zis_ast_node_obj_location(node);
    const char *node_type_s = zis_ast_node_type_represent(zis_ast_node_obj_type(node));
    if ((int)expected_nt == -1)
        error(p, loc->line0, loc->column0, "unexpected <%s>", node_type_s);
    error(
        p, loc->line0, loc->column0, "expected <%s> but got <%s>",
        zis_ast_node_type_represent(expected_nt), node_type_s
    );
}

static void check_token_type(struct zis_parser *restrict p, enum zis_token_type tt) {
    if (zis_unlikely(this_token(p)->type != tt))
        error_unexpected_token(p, tt);
}

static void check_token_type_and_ignore(struct zis_parser *restrict p, enum zis_token_type tt) {
    check_token_type(p, tt);
    next_token(p);
}

static void check_node_type(
    struct zis_parser *restrict p,
    struct zis_ast_node_obj *node, enum zis_ast_node_type expected_nt
) {
    if (zis_unlikely(zis_ast_node_obj_type(node) != expected_nt))
        error_unexpected_node(p, node, expected_nt);
}

static void node_copy_token_loc(
    struct zis_ast_node_obj *restrict node, const struct zis_token *restrict tok
) {
    struct zis_ast_node_obj_location *loc = zis_ast_node_obj_location(node);
    loc->line0 = tok->line0, loc->column0 = tok->column0;
    loc->line1 = tok->line1, loc->column1 = tok->column1;
}

static void node_copy_token_loc1(
    struct zis_ast_node_obj *restrict node, const struct zis_token *restrict tok
) {
    struct zis_ast_node_obj_location *loc = zis_ast_node_obj_location(node);
    loc->line1 = tok->line1, loc->column1 = tok->column1;
}

static void node_copy_loc0(
    struct zis_ast_node_obj *restrict node_dst, struct zis_ast_node_obj *restrict node_src
) {
    struct zis_ast_node_obj_location *loc_dst = zis_ast_node_obj_location(node_dst);
    struct zis_ast_node_obj_location *loc_src = zis_ast_node_obj_location(node_src);
    loc_dst->line0 = loc_src->line0, loc_dst->column0 = loc_src->column0;
}

static void node_copy_loc1(
    struct zis_ast_node_obj *restrict node_dst, struct zis_ast_node_obj *restrict node_src
) {
    struct zis_ast_node_obj_location *loc_dst = zis_ast_node_obj_location(node_dst);
    struct zis_ast_node_obj_location *loc_src = zis_ast_node_obj_location(node_src);
    loc_dst->line1 = loc_src->line1, loc_dst->column1 = loc_src->column1;
}

/* ----- expression builder ------------------------------------------------- */

/// The expression builder state. Must be declared with `zis_locals_decl()`.
struct expr_builder_state {
    struct zis_array_obj *operator_stack; // { (type << 8 | prec_abs), ... }
    struct zis_array_obj *operand_stack; // { node, ... }
};

static void expr_builder_init(struct expr_builder_state *eb, struct zis_context *z) {
    eb->operator_stack = zis_array_obj_new(z, NULL, 0);
    eb->operand_stack = zis_array_obj_new(z, NULL, 0);
}

/// Append an operand (an AST node).
static void expr_builder_put_operand(
    struct expr_builder_state *eb, struct zis_context *z,
    struct zis_ast_node_obj *node
) {
    zis_array_obj_append(z, eb->operand_stack, zis_object_from(node));
}

/// Get the last operand (an AST node). Returns NULL if empty.
static struct zis_ast_node_obj *expr_builder_pop_operand(
    struct expr_builder_state *eb
) {
    struct zis_object *node = zis_array_obj_pop(eb->operand_stack);
    if (zis_unlikely(!node))
        return NULL;
    // assert(zis_object_type(node) == z->globals->type_AstNode);
    return zis_object_cast(node, struct zis_ast_node_obj);
}

/// Consume an operator and at least one operand, generate a new expression,
/// and put it onto the operand stack.
static void expr_builder_gen_one_expr(
    struct expr_builder_state *eb, struct zis_parser *p
) {
    struct zis_context *z = parser_z(p);
    struct zis_object *top_op_smi = zis_array_obj_pop(eb->operator_stack);
    assert(top_op_smi);
    assert(zis_object_is_smallint(top_op_smi));
    const enum zis_token_type op_type =
        (enum zis_token_type)(unsigned int)(zis_smallint_from_ptr(top_op_smi) >> 8);
    assert(zis_token_type_is_operator(op_type) || op_type == ZIS_TOK_L_PAREN);

    struct zis_ast_node_obj *result_node;
    if (zis_token_type_is_bin_op(op_type)) {
        if (zis_array_obj_length(eb->operand_stack) < 2)
            goto too_few_operands;
    } else {
        assert(zis_token_type_is_un_op(op_type));
        if (zis_array_obj_length(eb->operand_stack) < 1)
            goto too_few_operands;
    }

    switch (op_type) {
        enum zis_ast_node_type node_type;

#define CASE_UN_OP_(OP_TOK, NODE_TYPE) \
    case ZIS_TOK_OP_##OP_TOK:; \
    static_assert((int)ZIS_TOK_OP_##OP_TOK - (int)ZIS_TOK_OP_POS == (int)ZIS_AST_NODE_##NODE_TYPE - (int)ZIS_AST_NODE_Pos, ""); \
    static_assert(sizeof(struct zis_ast_node_##NODE_TYPE##_data) == sizeof(struct zis_ast_node_Pos_data), ""); \
// ^^^ CASE_UN_OP_ ^^^

    CASE_UN_OP_(POS, Pos)
    CASE_UN_OP_(NEG, Neg)
    CASE_UN_OP_(BIT_NOT, BitNot)
    CASE_UN_OP_(NOT, Not)

    // _un_op:
        assert(zis_token_type_is_un_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 1))
            goto too_few_operands;
        node_type = (enum zis_ast_node_type)((unsigned int)op_type - (unsigned int)ZIS_TOK_OP_POS + (unsigned int)ZIS_AST_NODE_Pos);
        result_node = _zis_ast_node_obj_new(z, node_type, sizeof(struct zis_ast_node_Pos_data) / sizeof(void *), false);
        {
            struct zis_ast_node_obj *val_node = expr_builder_pop_operand(eb);
            assert(val_node);
            struct zis_ast_node_Pos_data *data = (struct zis_ast_node_Pos_data *)(result_node->_data);
            data->value = val_node;
            zis_object_write_barrier(result_node, val_node);
            node_copy_loc0(result_node, val_node);
            node_copy_loc1(result_node, val_node);
        }
        break;

#undef CASE_UN_OP_

#define CASE_BIN_OP_(OP_TOK, NODE_TYPE) \
    case ZIS_TOK_OP_##OP_TOK:; \
    static_assert((int)ZIS_TOK_OP_##OP_TOK - (int)ZIS_TOK_OP_ADD == (int)ZIS_AST_NODE_##NODE_TYPE - (int)ZIS_AST_NODE_Add, ""); \
    static_assert(sizeof(struct zis_ast_node_##NODE_TYPE##_data) == sizeof(struct zis_ast_node_Add_data), ""); \
// ^^^ CASE_BIN_OP_ ^^^

    CASE_BIN_OP_(ADD, Add)
    CASE_BIN_OP_(SUB, Sub)
    CASE_BIN_OP_(MUL, Mul)
    CASE_BIN_OP_(DIV, Div)
    CASE_BIN_OP_(REM, Rem)
    CASE_BIN_OP_(SHL, Shl)
    CASE_BIN_OP_(SHR, Shr)
    CASE_BIN_OP_(BIT_AND, BitAnd)
    CASE_BIN_OP_(BIT_OR, BitOr)
    CASE_BIN_OP_(BIT_XOR, BitXor)
    CASE_BIN_OP_(EQL, Assign)
    CASE_BIN_OP_(EQ, Eq)
    CASE_BIN_OP_(NE, Ne)
    CASE_BIN_OP_(LT, Lt)
    CASE_BIN_OP_(LE, Le)
    CASE_BIN_OP_(GT, Gt)
    CASE_BIN_OP_(GE, Ge)
    CASE_BIN_OP_(AND, And)
    CASE_BIN_OP_(OR, Or)
    CASE_BIN_OP_(SUBSCRIPT, Subscript)

    // _bin_op:
        assert(zis_token_type_is_bin_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 2))
            goto too_few_operands;
        node_type = (enum zis_ast_node_type)((unsigned int)op_type - (unsigned int)ZIS_TOK_OP_ADD + (unsigned int)ZIS_AST_NODE_Add);
        result_node = _zis_ast_node_obj_new(z, node_type, sizeof(struct zis_ast_node_Add_data) / sizeof(void *), false);
        {
            struct zis_ast_node_obj *rhs_node = expr_builder_pop_operand(eb);
            struct zis_ast_node_obj *lhs_node = expr_builder_pop_operand(eb);
            assert(lhs_node && rhs_node);
            struct zis_ast_node_Add_data *data = (struct zis_ast_node_Add_data *)(result_node->_data);
            data->lhs = lhs_node, data->rhs = rhs_node;
            zis_object_write_barrier(result_node, lhs_node);
            zis_object_write_barrier(result_node, rhs_node);
            node_copy_loc0(result_node, lhs_node);
            node_copy_loc1(result_node, rhs_node);
        }
        break;

#undef CASE_BIN_OP_

#define CASE_EQL_OP_(OP_TOK, NODE_TYPE) \
    case ZIS_TOK_OP_##OP_TOK:; \
    static_assert((int)ZIS_TOK_OP_##OP_TOK - (int)ZIS_TOK_OP_ADD_EQL == (int)ZIS_AST_NODE_##NODE_TYPE - (int)ZIS_AST_NODE_Add, ""); \
    static_assert(sizeof(struct zis_ast_node_##NODE_TYPE##_data) == sizeof(struct zis_ast_node_Add_data), ""); \
// ^^^ CASE_EQL_OP_ ^^^

    CASE_EQL_OP_(ADD_EQL, Add)
    CASE_EQL_OP_(SUB_EQL, Sub)
    CASE_EQL_OP_(MUL_EQL, Mul)
    CASE_EQL_OP_(DIV_EQL, Div)
    CASE_EQL_OP_(REM_EQL, Rem)
    CASE_EQL_OP_(SHL_EQL, Shl)
    CASE_EQL_OP_(SHR_EQL, Shr)
    CASE_EQL_OP_(BIT_AND_EQL, BitAnd)
    CASE_EQL_OP_(BIT_OR_EQL, BitOr)
    CASE_EQL_OP_(BIT_XOR_EQL, BitXor)

    // _eql_op:
        assert(zis_token_type_is_bin_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 2))
            goto too_few_operands;
        node_type = (enum zis_ast_node_type)((unsigned int)op_type - (unsigned int)ZIS_TOK_OP_ADD_EQL + (unsigned int)ZIS_AST_NODE_Add);
        result_node = _zis_ast_node_obj_new(z, node_type, sizeof(struct zis_ast_node_Add_data) / sizeof(void *), false);
        {
            struct zis_ast_node_obj *rhs_node = expr_builder_pop_operand(eb);
            struct zis_ast_node_obj *lhs_node = expr_builder_pop_operand(eb);
            assert(lhs_node && rhs_node);
            struct zis_ast_node_Add_data *data = (struct zis_ast_node_Add_data *)(result_node->_data);
            data->lhs = lhs_node, data->rhs = rhs_node;
            zis_object_write_barrier(result_node, lhs_node);
            zis_object_write_barrier(result_node, rhs_node);
            node_copy_loc0(result_node, lhs_node);
            node_copy_loc1(result_node, rhs_node);
        } {
            zis_locals_decl_1(p, tmp_var, struct zis_ast_node_obj *op_node);
            tmp_var.op_node = result_node;
            result_node = zis_ast_node_new(z, Assign, false);
            zis_ast_node_set_field(result_node, Assign, lhs, ((struct zis_ast_node_Add_data *)tmp_var.op_node->_data)->lhs);
            zis_ast_node_set_field(result_node, Assign, rhs, tmp_var.op_node);
            zis_object_write_barrier(result_node, tmp_var.op_node);
            node_copy_loc0(result_node, tmp_var.op_node);
            node_copy_loc1(result_node, tmp_var.op_node);
            zis_locals_drop(p, tmp_var);
        }
        break;

#undef CASE_EQL_OP_

    case ZIS_TOK_OP_PERIOD:
        assert(zis_token_type_is_bin_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 2))
            goto too_few_operands;
        result_node = zis_ast_node_new(z, Field, false);
        {
            struct zis_ast_node_obj *rhs_node = expr_builder_pop_operand(eb);
            struct zis_ast_node_obj *lhs_node = expr_builder_pop_operand(eb);
            assert(lhs_node && rhs_node);
            check_node_type(p, rhs_node, ZIS_AST_NODE_Name);
            struct zis_symbol_obj *name = zis_ast_node_get_field(rhs_node, Name, value);
            zis_ast_node_set_field(result_node, Field, value, lhs_node);
            zis_ast_node_set_field(result_node, Field, name, name);
            node_copy_loc0(result_node, lhs_node);
            node_copy_loc1(result_node, rhs_node);
        }
        break;

    case ZIS_TOK_OP_COLON:
        assert(zis_token_type_is_bin_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 2))
            goto too_few_operands;
        result_node = zis_ast_node_new(z, Send, false);
        {
            struct zis_ast_node_obj *call_node = expr_builder_pop_operand(eb);
            struct zis_ast_node_obj *target_node = expr_builder_pop_operand(eb);
            assert(target_node && call_node);
            check_node_type(p, call_node, ZIS_AST_NODE_Call);
            struct zis_array_obj *args = zis_ast_node_get_field(call_node, Call, args);
            struct zis_ast_node_obj *method_node = zis_ast_node_get_field(call_node, Call, value);
            check_node_type(p, method_node, ZIS_AST_NODE_Name);
            struct zis_symbol_obj *method = zis_ast_node_get_field(method_node, Name, value);
            zis_ast_node_set_field(result_node, Send, target, target_node);
            zis_ast_node_set_field(result_node, Send, method, method);
            zis_ast_node_set_field(result_node, Send, args, args);
            node_copy_loc0(result_node, target_node);
            node_copy_loc1(result_node, call_node);
        }
        break;

    case ZIS_TOK_OP_CALL:
        assert(zis_token_type_is_bin_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 2))
            goto too_few_operands;
        {
            struct zis_ast_node_obj *args_node = expr_builder_pop_operand(eb);
            struct zis_ast_node_obj *val_node = expr_builder_pop_operand(eb);
            assert(val_node && args_node);
            check_node_type(p, args_node, ZIS_AST_NODE_Call);
            result_node = args_node;
            zis_ast_node_set_field(result_node, Call, value, val_node);
            node_copy_loc0(result_node, val_node);
        }
        break;

    default:
        zis_unreachable();
    }

    expr_builder_put_operand(eb, z, result_node);
    return;

too_few_operands:;
    // FIXME: the source location of the operator is unknown.
    unsigned int err_ln, err_col;
    struct zis_ast_node_obj *operand = expr_builder_pop_operand(eb);
    if (operand) {
        struct zis_ast_node_obj_location *loc = zis_ast_node_obj_location(operand);
        err_ln = loc->line0, err_col = loc->column0;
    } else {
        err_ln = this_token(p)->line0, err_col = this_token(p)->column0;
    }
    error(p, err_ln, err_col, "too few operands for %s", zis_token_type_represent(op_type));
}

/// Append an operator.
static void expr_builder_put_operator(
    struct expr_builder_state *eb, struct zis_parser *p,
    enum zis_token_type op_type
) {
    struct zis_context *z = parser_z(p);

    assert(zis_token_type_is_operator(op_type));
    const int8_t op_prec = zis_token_operator_precedence(op_type);
    assert(op_prec != 0);
    uint8_t op_prec_abs, op_prec_cmp;
    if (zis_likely(op_prec > 0))
        op_prec_abs = (uint8_t)op_prec, op_prec_cmp = op_prec_abs;
    else
        op_prec_abs = (uint8_t)-op_prec, op_prec_cmp = op_prec_abs - 1;

    while (true) {
        struct zis_object *top_op_smi = zis_array_obj_back(eb->operator_stack);
        if (!top_op_smi)
            break;
        assert(zis_object_is_smallint(top_op_smi));
        const uint8_t top_op_prec_abs = (uint8_t)zis_smallint_from_ptr(top_op_smi);
        if (top_op_prec_abs > op_prec_cmp)
            break;
        expr_builder_gen_one_expr(eb, p);
    }

    const zis_smallint_t op_info = (unsigned int)op_type << 8 | op_prec_abs;
    zis_array_obj_append(z, eb->operator_stack, zis_smallint_to_ptr(op_info));
}

/// Append a "(".
static void expr_builder_put_l_paren(
    struct expr_builder_state *eb, struct zis_context *z
) {
    const zis_smallint_t op_info = (unsigned int)ZIS_TOK_L_PAREN << 8 | (UINT8_MAX - 2);
    zis_array_obj_append(z, eb->operator_stack, zis_smallint_to_ptr(op_info));
}

/// Append a ")". Consumes operators until "(" and returns true.
/// If there is no "(" in the stack, consumes all operators and returns false.
/// The previous token must not be "(".
zis_nodiscard static bool expr_builder_put_r_paren(
    struct expr_builder_state *eb, struct zis_parser *p
) {
    while (true) {
        struct zis_object *top_op_smi = zis_array_obj_back(eb->operator_stack);
        if (!top_op_smi) {
            return false;
        }
        assert(zis_object_is_smallint(top_op_smi));
        const enum zis_token_type tok_type =
            (enum zis_token_type)(unsigned int)(zis_smallint_from_ptr(top_op_smi) >> 8);
        if (tok_type == ZIS_TOK_L_PAREN) {
            zis_array_obj_pop(eb->operator_stack);
            return true;
        }
        expr_builder_gen_one_expr(eb, p);
    }
}

/// Consume all operators and operands to generate the finally result.
static struct zis_ast_node_obj *expr_builder_generate_expr(
    struct expr_builder_state *eb, struct zis_parser *p
) {
    struct zis_context *z = parser_z(p);

    while (zis_array_obj_length(eb->operator_stack)) {
        expr_builder_gen_one_expr(eb, p);
    }
    const size_t rest_operands_count = zis_array_obj_length(eb->operand_stack);
    if (zis_unlikely(rest_operands_count != 1)) {
        if (!rest_operands_count) {
            const struct zis_token *tok = this_token(p);
            error(
                p, tok->line0, tok->column0, "expected %s before %s",
                "an expression", zis_token_type_represent(tok->type)
            );
        } else {
            struct zis_object *node = zis_array_obj_pop(eb->operand_stack);
            assert(zis_object_type(node) == z->globals->type_AstNode);
            struct zis_ast_node_obj_location *loc =
                zis_ast_node_obj_location(zis_object_cast(node, struct zis_ast_node_obj));
            error(p, loc->line0, loc->column0, "unexpected %s", "expression");
        }
    }

    struct zis_object *node = zis_array_obj_pop(eb->operand_stack);
    assert(node);
    assert(zis_object_type(node) == z->globals->type_AstNode);
    return zis_object_cast(node, struct zis_ast_node_obj);
}

/* ----- parsing implementation --------------------------------------------- */

// "nil"
static struct zis_ast_node_obj *parse_Nil_explicit(struct zis_parser *p) {
    parser_debug_log_node(p, "Nil");
    assert(this_token(p)->type == ZIS_TOK_KW_NIL);
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Nil, true);
    node_copy_token_loc(node, this_token(p));
    next_token(p);
    return node;
}

// "true" | "false"
static struct zis_ast_node_obj *parse_Bool_explicit(struct zis_parser *p) {
    parser_debug_log_node(p, "Bool");
    struct zis_context *z = parser_z(p);
    const enum zis_token_type tok_type = this_token(p)->type;
    assert(tok_type == ZIS_TOK_KW_TRUE || tok_type == ZIS_TOK_KW_FALSE);
    struct zis_ast_node_obj *node = zis_ast_node_new(z, Bool, false);
    struct zis_bool_obj *bool_v =
        tok_type == ZIS_TOK_KW_FALSE ? z->globals->val_false : z->globals->val_true;
    zis_ast_node_set_field(node, Bool, value, bool_v);
    node_copy_token_loc(node, this_token(p));
    next_token(p);
    return node;
}

// literals
static struct zis_ast_node_obj *parse_Constant_explicit(struct zis_parser *p) {
    parser_debug_log_node(p, "Constant");
    assert(zis_token_type_is_literal(this_token(p)->type));
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Constant, false);
    zis_ast_node_set_field(node, Constant, value, this_token(p)->value);
    node_copy_token_loc(node, this_token(p));
    next_token(p);
    return node;
}

// identifier
static struct zis_ast_node_obj *parse_Name(struct zis_parser *p) {
    parser_debug_log_node(p, "Name");
    check_token_type(p, ZIS_TOK_IDENTIFIER);
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Name, false);
    const struct zis_token *tok = this_token(p);
    zis_ast_node_set_field(node, Name, value, tok->value_identifier);
    node_copy_token_loc(node, tok);
    next_token(p);
    return node;
}

static struct zis_ast_node_obj *parse_expression_1(struct zis_parser *p, struct expr_builder_state *restrict eb);
static struct zis_ast_node_obj *parse_expression(struct zis_parser *p);

/// Parse a comma-separated list: "beginning_tok element, element, ... end_tok".
/// `beginning_tok` can be `NTOK` to ignore. `pairs` specifies whether the elements
/// are pairs like "expr -> expr".
static void parse_list(
    struct zis_parser *p,
    enum zis_token_type beginning_tok /*=NTOK*/, enum zis_token_type end_tok, const bool pairs,
    struct zis_array_obj *_elements_out, struct zis_ast_node_obj_location *restrict loc_out
) {
    zis_lexer_ignore_eol_begin(&p->lexer);

    if (beginning_tok != NTOK) {
        const struct zis_token *tok = this_token(p);
        loc_out->line0 = tok->line0, loc_out->column0 = tok->column0;
        check_token_type_and_ignore(p, beginning_tok);
    }

    struct zis_context *const z = parser_z(p);
    zis_locals_decl(
        p, var,
        struct expr_builder_state expr_builder;
        struct zis_array_obj *elements;
    );
    zis_locals_zero(var);
    var.elements = _elements_out;
    expr_builder_init(&var.expr_builder, z);

    while (true) {
        if (this_token(p)->type == end_tok)
            break;
        struct zis_ast_node_obj *node = parse_expression_1(p, &var.expr_builder);
        zis_array_obj_append(z, var.elements, zis_object_from(node));
        if (pairs) {
            check_token_type_and_ignore(p, ZIS_TOK_R_ARROW);
            struct zis_ast_node_obj *node2 = parse_expression_1(p, &var.expr_builder);
            zis_array_obj_append(z, var.elements, zis_object_from(node2));
        }
        if (this_token(p)->type == end_tok)
            break;
        check_token_type_and_ignore(p, ZIS_TOK_COMMA);
    }

    zis_lexer_ignore_eol_end(&p->lexer);

    {
        const struct zis_token *tok = this_token(p);
        assert(tok->type == end_tok);
        loc_out->line1 = tok->line1, loc_out->column1 = tok->column1;
    }
    next_token(p);

    zis_locals_drop(p, var);
}

// expr "," ... ")"; `_first_element` = NULL to make a Tuple node without args
static struct zis_ast_node_obj *parse_Tuple_rest(
    struct zis_parser *p, struct zis_ast_node_obj *_first_element /* = NULL */
) {
    parser_debug_log_node_begin(p, "Tuple");

    struct zis_context *z = parser_z(p);
    zis_locals_decl(
        p, var,
        struct zis_ast_node_obj *first_element;
        struct zis_ast_node_obj *node;
        struct zis_array_obj *args;
    );
    zis_locals_zero(var);

    if (_first_element)
        var.first_element = _first_element;
    var.node = zis_ast_node_new(z, Tuple, true);
    var.args = zis_array_obj_new(z, NULL, 0);
    zis_ast_node_set_field(var.node, Tuple, args, var.args);

    if (_first_element) {
        zis_array_obj_append(z, var.args, zis_object_from(var.first_element));
        struct zis_ast_node_obj_location node_loc =
            *zis_ast_node_obj_location(var.first_element);
        parse_list(p, NTOK, ZIS_TOK_R_PAREN, false, var.args, &node_loc);
        *zis_ast_node_obj_location(var.node) = node_loc;
    } else {
        node_copy_token_loc(var.node, this_token(p));
        struct zis_ast_node_obj_location *loc = zis_ast_node_obj_location(var.node);
        if (loc->column0 > 1)
            loc->column0--;
        if (loc->column1 > 1)
            loc->column1--;
        // FIXME: accurate location is unknown.
    }

    parser_debug_log_node_end(p, "Tuple");
    zis_locals_drop(p, var);
    return var.node;
}

// "[" expr "," ... "]"
static struct zis_ast_node_obj *parse_Array(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "Array");

    struct zis_context *z = parser_z(p);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    zis_locals_zero_1(var, node);

    var.node = zis_ast_node_new(z, Array, true);
    struct zis_array_obj *args = zis_array_obj_new(z, NULL, 0);
    zis_ast_node_set_field(var.node, Array, args, args);

    struct zis_ast_node_obj_location node_loc;
    parse_list(p, ZIS_TOK_L_BRACKET, ZIS_TOK_R_BRACKET, false, args, &node_loc);
    *zis_ast_node_obj_location(var.node) = node_loc;

    parser_debug_log_node_end(p, "Array");
    zis_locals_drop(p, var);
    return var.node;
}

// "{" expr "->" expr "," ... "}"
static struct zis_ast_node_obj *parse_Map(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "Map");

    struct zis_context *z = parser_z(p);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    zis_locals_zero_1(var, node);

    var.node = zis_ast_node_new(z, Map, true);
    struct zis_array_obj *args = zis_array_obj_new(z, NULL, 0);
    zis_ast_node_set_field(var.node, Map, args, args);

    struct zis_ast_node_obj_location node_loc;
    parse_list(p, ZIS_TOK_L_BRACE, ZIS_TOK_R_BRACE, true, args, &node_loc);
    *zis_ast_node_obj_location(var.node) = node_loc;

    parser_debug_log_node_end(p, "Map");
    zis_locals_drop(p, var);
    return var.node;
}

// ? "(" ... ")"; generates a Call node
static struct zis_ast_node_obj *parse_Call_args(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "CallArgs");

    struct zis_context *z = parser_z(p);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    zis_locals_zero_1(var, node);

    var.node = zis_ast_node_new(z, Call, true);
    struct zis_array_obj *args = zis_array_obj_new(z, NULL, 0);
    zis_ast_node_set_field(var.node, Call, args, args);

    struct zis_ast_node_obj_location node_loc;
    parse_list(p, ZIS_TOK_L_PAREN, ZIS_TOK_R_PAREN, false, args, &node_loc);
    *zis_ast_node_obj_location(var.node) = node_loc;

    parser_debug_log_node_end(p, "CallArgs");
    zis_locals_drop(p, var);
    return var.node;
}

// ? "[ ... ]"
static struct zis_ast_node_obj *parse_Subs_args(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "SubsArgs");

    struct zis_context *z = parser_z(p);

    assert(this_token(p)->type == ZIS_TOK_L_BRACKET);
    zis_lexer_ignore_eol_begin(&p->lexer);
    next_token(p); // "["

    if (this_token(p)->type == ZIS_TOK_R_BRACKET)
        error(p, this_token(p)->line0, this_token(p)->column0, "empty subscript");

    struct zis_ast_node_obj *node = parse_expression(p);
    if (this_token(p)->type == ZIS_TOK_R_BRACKET) {
        zis_lexer_ignore_eol_end(&p->lexer);
        next_token(p); // "]"
    } else {
        check_token_type_and_ignore(p, ZIS_TOK_COMMA);
        zis_lexer_ignore_eol_end(&p->lexer);

        zis_locals_decl(
            p, var,
            struct zis_ast_node_obj *first_element;
            struct zis_ast_node_obj *node;
            struct zis_array_obj *args;
        );
        zis_locals_zero(var);

        var.first_element = node;
        var.node = zis_ast_node_new(z, Tuple, true);
        var.args = zis_array_obj_new(z, NULL, 0);
        zis_ast_node_set_field(var.node, Tuple, args, var.args);

        zis_array_obj_append(z, var.args, zis_object_from(var.first_element));
        struct zis_ast_node_obj_location node_loc =
            *zis_ast_node_obj_location(var.first_element);
        parse_list(p, NTOK, ZIS_TOK_R_BRACKET, false, var.args, &node_loc);
        *zis_ast_node_obj_location(var.node) = node_loc;

        node = var.node;
        zis_locals_drop(p, var);
    }

    parser_debug_log_node_end(p, "SubsArgs");
    return node;
}

/// Parse an expression using an existing expr builder.
static struct zis_ast_node_obj *parse_expression_1(
    struct zis_parser *p, struct expr_builder_state *restrict eb
) {
    parser_debug_log_node_begin(p, "expression");

    struct zis_context *const z = parser_z(p);
    bool last_tok_is_operand = false;

    while (true) {
        enum zis_token_type tok_type = this_token(p)->type;
        if (zis_token_type_is_literal(tok_type)) {
            expr_builder_put_operand(eb, z, parse_Constant_explicit(p));
            last_tok_is_operand = true;
        } else if (zis_token_type_is_operator(tok_type)) {
            next_token(p);
            if (zis_unlikely(!last_tok_is_operand)) {
                if (tok_type == ZIS_TOK_OP_ADD)
                    tok_type = ZIS_TOK_OP_POS;
                else if (tok_type == ZIS_TOK_OP_SUB)
                    tok_type = ZIS_TOK_OP_NEG;
            }
            expr_builder_put_operator(eb, p, tok_type);
            last_tok_is_operand = false;
        } else {
            struct zis_ast_node_obj *node;
            switch (tok_type) {
            case ZIS_TOK_L_PAREN: // "("
                if (last_tok_is_operand) {
                    expr_builder_put_operator(eb, p, ZIS_TOK_OP_CALL);
                    node = parse_Call_args(p);
                } else {
                    zis_lexer_ignore_eol_begin(&p->lexer);
                    next_token(p);
                    if ((zis_likely(this_token(p)->type != ZIS_TOK_R_PAREN))) {
                        expr_builder_put_l_paren(eb, z);
                        continue;
                        // "(expr)" -> case ZIS_TOK_R_PAREN
                        // "(expr, ...)" -> case ZIS_TOK_COMMA
                    }
                    zis_lexer_ignore_eol_end(&p->lexer);
                    next_token(p); // ")"
                    node = parse_Tuple_rest(p, NULL); // "()".
                    last_tok_is_operand = true;
                }
                break;
            case ZIS_TOK_R_PAREN: // ")"
                if (!expr_builder_put_r_paren(eb, p))
                    goto end_expr_building;
                zis_lexer_ignore_eol_end(&p->lexer);
                next_token(p);
                last_tok_is_operand = true;
                continue;
            case ZIS_TOK_L_BRACKET: // "["
                if (last_tok_is_operand) {
                    expr_builder_put_operator(eb, p, ZIS_TOK_OP_SUBSCRIPT);
                    node = parse_Subs_args(p);
                } else {
                    node = parse_Array(p);
                }
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_L_BRACE: // "{"
                if (last_tok_is_operand) {
                    error_unexpected_token(p, NTOK);
                } else {
                    node = parse_Map(p);
                }
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_COMMA: // ","
                if (!expr_builder_put_r_paren(eb, p))
                    goto end_expr_building;
                node = expr_builder_pop_operand(eb);
                if (!node)
                    goto end_expr_building;
                next_token(p);
                zis_lexer_ignore_eol_end(&p->lexer); // After `next_token()`!
                node = parse_Tuple_rest(p, node);
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_KW_NIL:
                node = parse_Nil_explicit(p);
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_KW_TRUE:
            case ZIS_TOK_KW_FALSE:
                node = parse_Bool_explicit(p);
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_IDENTIFIER:
                node = parse_Name(p);
                last_tok_is_operand = true;
                break;
            default:
                goto end_expr_building;
            }
            expr_builder_put_operand(eb, z, node);
        }
    }
end_expr_building:;

    parser_debug_log_node_end(p, "expression");

    return expr_builder_generate_expr(eb, p);
}

/// Parse an expression.
static struct zis_ast_node_obj *parse_expression(struct zis_parser *p) {
    zis_locals_decl(
        p, var,
        struct expr_builder_state expr_builder;
    );
    zis_locals_zero(var);
    expr_builder_init(&var.expr_builder, parser_z(p));
    struct zis_ast_node_obj *const node = parse_expression_1(p, &var.expr_builder);
    zis_locals_drop(p, var);
    return node;
}

static struct zis_ast_node_obj *parse_Import(struct zis_parser *p) {
    assert(this_token(p)->type == ZIS_TOK_KW_IMPORT);
    struct zis_ast_node_obj *_node = zis_ast_node_new(parser_z(p), Import, true);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    var.node = _node;
    node_copy_token_loc(var.node, this_token(p));
    next_token(p);
    struct zis_ast_node_obj *_value = parse_expression(p);
    node_copy_loc1(var.node, _value);
    zis_ast_node_set_field(var.node, Import, value, _value);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);
    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_ast_node_obj *parse_Return(struct zis_parser *p) {
    assert(this_token(p)->type == ZIS_TOK_KW_RETURN);
    struct zis_ast_node_obj *_node = zis_ast_node_new(parser_z(p), Return, true);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    var.node = _node;
    node_copy_token_loc(var.node, this_token(p));
    next_token(p);
    if (this_token(p)->type == ZIS_TOK_EOS) {
        struct zis_object *nil = zis_object_from(parser_z(p)->globals->val_nil);
        zis_ast_node_set_field(var.node, Return, value, nil);
        next_token(p);
    } else {
        struct zis_ast_node_obj *expr = parse_expression(p);
        node_copy_loc1(var.node, expr);
        zis_ast_node_set_field(var.node, Return, value, zis_object_from(expr));
        check_token_type_and_ignore(p, ZIS_TOK_EOS);
    }
    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_ast_node_obj *parse_Throw(struct zis_parser *p) {
    assert(this_token(p)->type == ZIS_TOK_KW_THROW);
    struct zis_ast_node_obj *_node = zis_ast_node_new(parser_z(p), Throw, true);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    var.node = _node;
    node_copy_token_loc(var.node, this_token(p));
    next_token(p);
    if (this_token(p)->type == ZIS_TOK_EOS) {
        struct zis_object *nil = zis_object_from(parser_z(p)->globals->val_nil);
        zis_ast_node_set_field(var.node, Throw, value, nil);
        next_token(p);
    } else {
        struct zis_ast_node_obj *expr = parse_expression(p);
        node_copy_loc1(var.node, expr);
        zis_ast_node_set_field(var.node, Throw, value, zis_object_from(expr));
        check_token_type_and_ignore(p, ZIS_TOK_EOS);
    }
    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_ast_node_obj *parse_Break(struct zis_parser *p) {
    assert(this_token(p)->type == ZIS_TOK_KW_BREAK);
    struct zis_ast_node_obj *_node = zis_ast_node_new(parser_z(p), Break, true);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    var.node = _node;
    node_copy_token_loc(var.node, this_token(p));
    next_token(p);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);
    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_array_obj *parse_block(struct zis_parser *p);

static struct zis_ast_node_obj *parse_Continue(struct zis_parser *p) {
    assert(this_token(p)->type == ZIS_TOK_KW_BREAK);
    struct zis_ast_node_obj *_node = zis_ast_node_new(parser_z(p), Break, true);
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    var.node = _node;
    node_copy_token_loc(var.node, this_token(p));
    next_token(p);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);
    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_ast_node_obj *parse_Cond(struct zis_parser *p) {
    struct zis_context *z = parser_z(p);
    zis_locals_decl(
        p, var,
        struct zis_ast_node_obj *node;
        struct zis_array_obj *args;
    );
    zis_locals_zero(var);
    var.node = zis_ast_node_new(z, Cond, true);
    var.args = zis_array_obj_new(z, NULL, 0);
    zis_ast_node_set_field(var.node, Cond, args, var.args);

    node_copy_token_loc(var.node, this_token(p));
    assert(this_token(p)->type == ZIS_TOK_KW_IF);
    do {
        next_token(p);
        struct zis_ast_node_obj *cond_node = parse_expression(p);
        zis_array_obj_append(z, var.args, zis_object_from(cond_node));
        check_token_type_and_ignore(p, ZIS_TOK_EOS);
        struct zis_array_obj *body_node = parse_block(p);
        zis_array_obj_append(z, var.args, zis_object_from(body_node));
    } while (this_token(p)->type == ZIS_TOK_KW_ELIF);

    if (this_token(p)->type == ZIS_TOK_KW_ELSE) {
        next_token(p);
        check_token_type_and_ignore(p, ZIS_TOK_EOS);
        struct zis_array_obj *body_node = parse_block(p);
        zis_array_obj_append(z, var.args, zis_object_from(body_node));
    }

    node_copy_token_loc1(var.node, this_token(p));
    check_token_type_and_ignore(p, ZIS_TOK_KW_END);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);

    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_ast_node_obj *parse_While(struct zis_parser *p) {
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    zis_locals_zero_1(var, node);
    var.node = zis_ast_node_new(parser_z(p), While, true);

    node_copy_token_loc(var.node, this_token(p));
    assert(this_token(p)->type == ZIS_TOK_KW_WHILE);
    next_token(p);

    zis_ast_node_set_field(var.node, While, cond, parse_expression(p));
    check_token_type_and_ignore(p, ZIS_TOK_EOS);

    zis_ast_node_set_field(var.node, While, body, parse_block(p));
    node_copy_token_loc1(var.node, this_token(p));

    check_token_type_and_ignore(p, ZIS_TOK_KW_END);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);

    zis_locals_drop(p, var);
    return var.node;
}

static struct zis_ast_node_obj *parse_Func(struct zis_parser *p) {
    struct zis_context *z = parser_z(p);
    zis_locals_decl(
        p, var,
        struct zis_ast_node_obj *node;
        struct zis_array_obj *args;
    );
    zis_locals_zero(var);
    var.node = zis_ast_node_new(z, Func, true);
    var.args = zis_array_obj_new(z, NULL, 0);
    zis_ast_node_set_field(var.node, Func, args, var.args);

    node_copy_token_loc(var.node, this_token(p));
    assert(this_token(p)->type == ZIS_TOK_KW_FUNC);
    next_token(p);

    check_token_type(p, ZIS_TOK_IDENTIFIER);
    zis_ast_node_set_field(var.node, Func, name, this_token(p)->value_identifier);
    next_token(p);

    zis_lexer_ignore_eol_begin(&p->lexer);
    check_token_type_and_ignore(p, ZIS_TOK_L_PAREN);
    while (this_token(p)->type == ZIS_TOK_IDENTIFIER) {
        zis_array_obj_append(z, var.args, zis_object_from(this_token(p)->value_identifier));
        next_token(p);
        if (this_token(p)->type == ZIS_TOK_R_PAREN)
            break;
        check_token_type_and_ignore(p, ZIS_TOK_COMMA);
    }
    zis_lexer_ignore_eol_end(&p->lexer);
    check_token_type_and_ignore(p, ZIS_TOK_R_PAREN);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);

    zis_ast_node_set_field(var.node, Func, body, parse_block(p));

    node_copy_token_loc1(var.node, this_token(p));
    check_token_type_and_ignore(p, ZIS_TOK_KW_END);
    check_token_type_and_ignore(p, ZIS_TOK_EOS);

    zis_locals_drop(p, var);
    return var.node;
}

/// Parse a statement.
/// If the next token is an end of a block (some keyword or EOF), returns NULL.
static struct zis_ast_node_obj *parse_statement(struct zis_parser *p) {
    while (true) {
        const enum zis_token_type tok_type = this_token(p)->type;
        if (zis_token_type_is_keyword(tok_type)) {
            switch (tok_type) {
            case ZIS_TOK_KW_IMPORT:
                return parse_Import(p);
            case ZIS_TOK_KW_RETURN:
                return parse_Return(p);
            case ZIS_TOK_KW_THROW:
                return parse_Throw(p);
            case ZIS_TOK_KW_BREAK:
                return parse_Break(p);
            case ZIS_TOK_KW_CONTINUE:
                return parse_Continue(p);
            case ZIS_TOK_KW_IF:
                return parse_Cond(p);
            case ZIS_TOK_KW_WHILE:
                return parse_While(p);
            case ZIS_TOK_KW_FUNC:
                return parse_Func(p);
            case ZIS_TOK_KW_ELIF:
            case ZIS_TOK_KW_ELSE:
            case ZIS_TOK_KW_END:
                return NULL; // end
            case ZIS_TOK_KW_NIL:
            case ZIS_TOK_KW_TRUE:
            case ZIS_TOK_KW_FALSE:
                goto parse_expr;
            default:
                error_unexpected_token(p, NTOK);
            }
        }
        if (zis_unlikely(tok_type >= ZIS_TOK_EOS)) {
            static_assert((int)ZIS_TOK_EOS + 1 == (int)ZIS_TOK_EOF, "");
            static_assert((int)ZIS_TOK_EOS + 2 == (int)_ZIS_TOK_COUNT, "");
            if (tok_type == ZIS_TOK_EOS) {
                next_token(p);
                continue; // empty statement
            }
            if (tok_type == ZIS_TOK_EOF) {
                return NULL; // end
            }
            zis_unreachable();
        }
    parse_expr:;
        struct zis_ast_node_obj *node = parse_expression(p);
        assert(node);
        check_token_type_and_ignore(p, ZIS_TOK_EOS);
        return node;
    }
}

/// Parse a block.
static struct zis_array_obj *parse_block(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "block");
    struct zis_context *const z = parser_z(p);
    zis_locals_decl_1(p, var, struct zis_array_obj *stmt_list);
    zis_locals_zero_1(var, stmt_list);
    var.stmt_list = zis_array_obj_new(z, NULL, 0);
    while (true) {
        struct zis_ast_node_obj *node = parse_statement(p);
        if (!node)
            break;
        zis_array_obj_append(z, var.stmt_list, zis_object_from(node));
    }
    parser_debug_log_node_end(p, "block");
    zis_locals_drop(p, var);
    return var.stmt_list;
}

static struct zis_ast_node_obj *parse_Module(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "Module");
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    zis_locals_zero_1(var, node);
    var.node = zis_ast_node_new(parser_z(p), Module, true);
    zis_ast_node_set_field(var.node, Module, file, zis_object_from(zis_string_obj_new(parser_z(p), "", 0)));
    zis_ast_node_set_field(var.node, Module, body, parse_block(p));
    memset(zis_ast_node_obj_location(var.node), 0, sizeof(struct zis_ast_node_obj_location));
    parser_debug_log_node_end(p, "Module");
    zis_locals_drop(p, var);
    return var.node;
}

/* ----- public functions --------------------------------------------------- */

struct zis_parser *zis_parser_create(struct zis_context *z) {
    struct zis_parser *p = zis_mem_alloc(sizeof(struct zis_parser));
    zis_lexer_init(&p->lexer, z);
    p->token.type = ZIS_TOK_EOF;
    p->token.value = zis_smallint_to_ptr(0);
    zis_locals_root_init(&p->locals_root, NULL);
    zis_objmem_add_gc_root(z, p, parser_gc_visit);
    return p;
}

void zis_parser_destroy(struct zis_parser *p, struct zis_context *z) {
    zis_locals_root_fini(&p->locals_root, NULL);
    zis_objmem_remove_gc_root(z, p);
    zis_mem_free(p);
}

#if ZIS_DEBUG_LOGGING

static void _parser_dump_ast(struct zis_context *, FILE *, struct zis_ast_node_obj *, unsigned int);

static void _parser_dump_obj(
    struct zis_context *z, FILE *fp,
    struct zis_object *obj, unsigned int level
) {
    struct zis_context_globals *const g = z->globals;
    const unsigned int level_m1 = level - 1;
    struct zis_type_obj *obj_type;
    if (zis_object_is_smallint(obj)) {
        fprintf(fp, "%*c%lli\n", level_m1, ' ', (long long)zis_smallint_from_ptr(obj));
    } else if ((obj_type = zis_object_type(obj)), obj_type == g->type_AstNode) {
        _parser_dump_ast(z, fp, zis_object_cast(obj, struct zis_ast_node_obj), level);
    } else if (obj_type == g->type_Array) {
        struct zis_array_obj *const arr_obj = zis_object_cast(obj, struct zis_array_obj);
        fprintf(fp, "%*carray>\n", level, '<');
        for (size_t i = 0, n = zis_array_obj_length(arr_obj); i < n; i++) {
            struct zis_object *elem = zis_array_obj_get(arr_obj, i);
            fprintf(fp, "%*c!-- array[%zu] -->\n", level + 1, '<', i + 1);
            _parser_dump_obj(z, fp, elem, level + 1);
        }
        fprintf(fp, "%*c/array>\n", level, '<');
    } else if (obj_type == g->type_Symbol) {
        struct zis_symbol_obj *const sym_obj = zis_object_cast(obj, struct zis_symbol_obj);
        const char *s = zis_symbol_obj_data(sym_obj);
        const int n = (int)zis_symbol_obj_data_size(sym_obj);
        fprintf(fp, "%*c%.*s\n", level_m1, ' ', n, s);
    } else if (obj_type == g->type_String) {
        char buffer[64]; size_t size = sizeof buffer;
        size = zis_string_obj_value(zis_object_cast(obj, struct zis_string_obj), buffer, size);
        if (size != (size_t)-1)
            fprintf(fp, "%*c<![CDATA[%.*s]]>\n", level_m1, ' ', (int)size, buffer);
        else
            fprintf(fp, "%*c(long string)\n", level_m1, ' ');
    } else if (obj_type == g->type_Bool) {
        const char *s = obj == zis_object_from(g->val_true) ? "true" : "false";
        fprintf(fp, "%*c%s\n", level_m1, ' ', s);
    } else {
        if (obj != zis_object_from(g->val_nil))
            fprintf(fp, "%*c...\n", level_m1, ' ');
    }
}

static void _parser_dump_ast(
    struct zis_context *z, FILE *fp,
    struct zis_ast_node_obj *node, unsigned int level
) {
    const enum zis_ast_node_type node_type = zis_ast_node_obj_type(node);
    const struct zis_ast_node_obj_location *const node_loc =
        zis_ast_node_obj_location(node);
    fprintf(
        fp, "%*c%s loc=\"%u:%u-%u:%u\">\n",
        level, '<', zis_ast_node_type_represent(node_type),
        node_loc->line0, node_loc->column0, node_loc->line1, node_loc->column1
    );
    const char *f_names[4]; struct zis_type_obj *f_types[4];
    int f_n = zis_ast_node_type_fields(z, node_type, f_names, f_types);
    assert(f_n >= 0);
    for (int i = 0; i < f_n; i++) {
        fprintf(fp, "%*c%s>\n", level + 1, '<', f_names[i]);
        struct zis_object *const field_obj = node->_data[i];
        _parser_dump_obj(z, fp, field_obj, level + 2);
        fprintf(fp, "%*c/%s>\n", level + 1, '<', f_names[i]);
    }
    fprintf(fp, "%*c/%s>\n", level, '<', zis_ast_node_type_represent(node_type));
}

#endif // ZIS_DEBUG_LOGGING

struct zis_ast_node_obj *zis_parser_parse(
    struct zis_parser *p,
    struct zis_stream_obj *input,
    enum zis_parser_what what
) {
    struct zis_ast_node_obj *result;
    zis_lexer_start(&p->lexer, input, parser_lexer_error_handler);
    assert(!p->locals_root._list);
#if ZIS_DEBUG_LOGGING
    p->tree_depth = 0;
#endif // ZIS_DEBUG_LOGGING

    if (!parser_error_setjmp(p)) {
        next_token(p);
        switch (what) {
        case ZIS_PARSER_EXPR:
            result = parse_expression(p);
            break;
        case ZIS_PARSER_MOD:
        default:
            result = parse_Module(p);
            break;
        }
    } else {
        zis_locals_root_reset(&p->locals_root);
        result = NULL;
    }

    zis_lexer_finish(&p->lexer);
    p->token.value = zis_smallint_to_ptr(0);
    assert(!p->locals_root._list);

#if ZIS_DEBUG_LOGGING
    if (result) {
        zis_debug_log_1(DUMP, "Parser", "zis_parser_parse()", stream, {
            _parser_dump_ast(parser_z(p), stream, result, 1);
        });
    }
#endif // ZIS_DEBUG_LOGGING

    return result;
}

#endif // ZIS_FEATURE_SRC
