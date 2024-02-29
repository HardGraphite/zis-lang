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
#include "locale.h"
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
        TRACE, "Parser", "%-*c<%s pos=\"%u:%u\" />", \
        (int)(__p)->tree_depth + 1, ' ',       \
        (__type), (__p)->token.line0, (__p)->token.column0 \
    ))

#define parser_debug_log_node_begin(__p, __type) \
    (zis_debug_log(                              \
        TRACE, "Parser", "%-*c<%s pos=\"%u:%u\">", \
        (int)++(__p)->tree_depth, ' ',             \
        (__type), (__p)->token.line0, (__p)->token.column0 \
    ))

#define parser_debug_log_node_end(__p, __type) \
    (zis_debug_log(TRACE, "Parser", "%-*c</%s>", (int)(__p)->tree_depth--, ' ', (__type)))

/* ----- convinient functions ----------------------------------------------- */

zis_noreturn static void error_not_implemented(struct zis_parser *restrict p, const char *func) {
    const struct zis_token *tok = this_token(p);
    error(p, tok->line0, tok->column0, "not implemented: %s()", func);
}

zis_noreturn zis_noinline zis_cold_fn static void
error_unexpected_token(struct zis_parser *restrict p, enum zis_token_type expected_tt) {
    const struct zis_token *tok = this_token(p);
    const char *tok_tt_s = zis_token_type_represent(tok->type);
    if ((int)expected_tt == -1)
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
    const struct zis_ast_node_obj_position *const pos = zis_ast_node_obj_position(node);
    const char *node_type_s = zis_ast_node_type_represent(zis_ast_node_obj_type(node));
    if ((int)expected_nt == -1)
        error(p, pos->line0, pos->column0, "unexpected <%s>", node_type_s);
    error(
        p, pos->line0, pos->column0, "expected <%s> but got <%s>",
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

static void node_copy_token_pos(
    struct zis_ast_node_obj *restrict node, const struct zis_token *restrict tok
) {
    struct zis_ast_node_obj_position *pos = zis_ast_node_obj_position(node);
    pos->line0 = tok->line0, pos->column0 = tok->column0;
    pos->line1 = tok->line1, pos->column1 = tok->column1;
}

static void node_copy_pos0(
    struct zis_ast_node_obj *restrict node_dst, struct zis_ast_node_obj *restrict node_src
) {
    struct zis_ast_node_obj_position *pos_dst = zis_ast_node_obj_position(node_dst);
    struct zis_ast_node_obj_position *pos_src = zis_ast_node_obj_position(node_src);
    pos_dst->line0 = pos_src->line0, pos_dst->column0 = pos_src->column0;
}

static void node_copy_pos1(
    struct zis_ast_node_obj *restrict node_dst, struct zis_ast_node_obj *restrict node_src
) {
    struct zis_ast_node_obj_position *pos_dst = zis_ast_node_obj_position(node_dst);
    struct zis_ast_node_obj_position *pos_src = zis_ast_node_obj_position(node_src);
    pos_dst->line1 = pos_src->line1, pos_dst->column1 = pos_src->column1;
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
            node_copy_pos0(result_node, val_node);
            node_copy_pos1(result_node, val_node);
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
            node_copy_pos0(result_node, lhs_node);
            node_copy_pos1(result_node, rhs_node);
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
            node_copy_pos0(result_node, lhs_node);
            node_copy_pos1(result_node, rhs_node);
        } {
            zis_locals_decl_1(p, tmp_var, struct zis_ast_node_obj *op_node);
            tmp_var.op_node = result_node;
            result_node = zis_ast_node_new(z, Assign, false);
            zis_ast_node_set_field(result_node, Assign, lhs, ((struct zis_ast_node_Add_data *)tmp_var.op_node->_data)->lhs);
            zis_ast_node_set_field(result_node, Assign, rhs, tmp_var.op_node);
            zis_object_write_barrier(result_node, tmp_var.op_node);
            node_copy_pos0(result_node, tmp_var.op_node);
            node_copy_pos1(result_node, tmp_var.op_node);
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
            node_copy_pos0(result_node, lhs_node);
            node_copy_pos1(result_node, rhs_node);
        }
        break;

    case ZIS_TOK_OP_COLON:
        assert(zis_token_type_is_bin_op(op_type));
        if (zis_unlikely(zis_array_obj_length(eb->operand_stack) < 2))
            goto too_few_operands;
        result_node = zis_ast_node_new(z, Send, false);
        {
            struct zis_ast_node_obj *call_node = expr_builder_pop_operand(eb);
            struct zis_ast_node_obj *tgt_node = expr_builder_pop_operand(eb);
            assert(tgt_node && call_node);
            check_node_type(p, call_node, ZIS_AST_NODE_Call);
            struct zis_array_obj *args = zis_ast_node_get_field(call_node, Call, args);
            struct zis_ast_node_obj *method_node = zis_ast_node_get_field(call_node, Call, value);
            check_node_type(p, method_node, ZIS_AST_NODE_Name);
            struct zis_symbol_obj *method = zis_ast_node_get_field(method_node, Name, value);
            zis_ast_node_set_field(result_node, Send, target, result_node);
            zis_ast_node_set_field(result_node, Send, method, method);
            zis_ast_node_set_field(result_node, Send, args, args);
            node_copy_pos0(result_node, tgt_node);
            node_copy_pos1(result_node, tgt_node);
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
            node_copy_pos0(result_node, val_node);
        }
        break;

    default:
        zis_unreachable();
    }

    expr_builder_put_operand(eb, z, result_node);
    return;

too_few_operands:;
    // FIXME: the source position of the operator is unknown.
    unsigned int err_ln, err_col;
    struct zis_ast_node_obj *operand = expr_builder_pop_operand(eb);
    if (operand) {
        struct zis_ast_node_obj_position *pos = zis_ast_node_obj_position(operand);
        err_ln = pos->line0, err_col = pos->column0;
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
            struct zis_ast_node_obj_position *pos =
                zis_ast_node_obj_position(zis_object_cast(node, struct zis_ast_node_obj));
            error(p, pos->line0, pos->column0, "unexpected %s", "expression");
        }
    }

    struct zis_object *node = zis_array_slots_obj_get(eb->operand_stack->_data, 0);
    assert(zis_object_type(node) == z->globals->type_AstNode);
    return zis_object_cast(node, struct zis_ast_node_obj);
}

/* ----- parsing implementation --------------------------------------------- */

// "nil"
static struct zis_ast_node_obj *parse_Nil_explicit(struct zis_parser *p) {
    parser_debug_log_node(p, "Nil");
    assert(this_token(p)->type == ZIS_TOK_KW_NIL);
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Nil, false);
    zis_ast_node_set_field(node, Nil, value, zis_smallint_to_ptr(0));
    node_copy_token_pos(node, this_token(p));
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
    node_copy_token_pos(node, this_token(p));
    next_token(p);
    return node;
}

// literals
static struct zis_ast_node_obj *parse_Constant_explicit(struct zis_parser *p) {
    parser_debug_log_node(p, "Constant");
    assert(zis_token_type_is_literal(this_token(p)->type));
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Constant, false);
    zis_ast_node_set_field(node, Constant, value, this_token(p)->value);
    node_copy_token_pos(node, this_token(p));
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
    node_copy_token_pos(node, tok);
    next_token(p);
    return node;
}

// expr "," ... ")"
static struct zis_ast_node_obj *parse_Tuple_rest(
    struct zis_parser *p, struct zis_ast_node_obj *first_element /* = NULL */
) {
    zis_unused_var(first_element);
    error_not_implemented(p, __func__);
}

// "[" expr "," ... "]"
static struct zis_ast_node_obj *parse_Array(struct zis_parser *p) {
    error_not_implemented(p, __func__);
}

// "{" expr "->" expr "," ... "}"
static struct zis_ast_node_obj *parse_Map(struct zis_parser *p) {
    error_not_implemented(p, __func__);
}

// "(" ... ")"
static struct zis_ast_node_obj *parse_Call_args(struct zis_parser *p) {
    error_not_implemented(p, __func__);
}

// "[ ... ]"
static struct zis_ast_node_obj *parse_Subs_args(struct zis_parser *p) {
    error_not_implemented(p, __func__);
}

/// Parse an expression.
static struct zis_ast_node_obj *parse_expression(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "expression");

    struct zis_context *const z = parser_z(p);
    bool last_tok_is_operand = false;
    zis_locals_decl(
        p, var,
        struct expr_builder_state expr_builder;
    );
    zis_locals_zero(var);
    expr_builder_init(&var.expr_builder, z);

    while (true) {
        enum zis_token_type tok_type = this_token(p)->type;
        if (zis_token_type_is_literal(tok_type)) {
            expr_builder_put_operand(&var.expr_builder, z, parse_Constant_explicit(p));
            last_tok_is_operand = true;
        } else if (zis_token_type_is_operator(tok_type)) {
            next_token(p);
            if (zis_unlikely(!last_tok_is_operand)) {
                if (tok_type == ZIS_TOK_OP_ADD)
                    tok_type = ZIS_TOK_OP_POS;
                else if (tok_type == ZIS_TOK_OP_SUB)
                    tok_type = ZIS_TOK_OP_NEG;
            }
            expr_builder_put_operator(&var.expr_builder, p, tok_type);
            last_tok_is_operand = false;
        } else {
            struct zis_ast_node_obj *node;
            switch (tok_type) {
            case ZIS_TOK_L_PAREN: // "("
                if (last_tok_is_operand) {
                    expr_builder_put_operator(&var.expr_builder, p, ZIS_TOK_OP_CALL);
                    node = parse_Call_args(p);
                } else {
                    next_token(p);
                    if ((zis_likely(this_token(p)->type != ZIS_TOK_R_PAREN))) {
                        expr_builder_put_l_paren(&var.expr_builder, z);
                        continue;
                        // "(a, b, ...)" -> case ZIS_TOK_COMMA.
                    }
                    node = parse_Tuple_rest(p, NULL); // "()".
                }
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_R_PAREN: // ")"
                next_token(p);
                if (!expr_builder_put_r_paren(&var.expr_builder, p))
                    goto end_expr_building;
                last_tok_is_operand = true;
                continue;
            case ZIS_TOK_L_BRACKET: // "["
                if (last_tok_is_operand) {
                    expr_builder_put_operator(&var.expr_builder, p, ZIS_TOK_OP_SUBSCRIPT);
                    node = parse_Subs_args(p);
                } else {
                    node = parse_Array(p);
                }
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_L_BRACE: // "{"
                if (last_tok_is_operand) {
                    error_unexpected_token(p, (enum zis_token_type)-1);
                } else {
                    node = parse_Map(p);
                }
                last_tok_is_operand = true;
                break;
            case ZIS_TOK_COMMA: // ","
                if (!expr_builder_put_r_paren(&var.expr_builder, p))
                    goto end_expr_building;
                node = expr_builder_pop_operand(&var.expr_builder);
                if (!node)
                    goto end_expr_building;
                next_token(p);
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
            expr_builder_put_operand(&var.expr_builder, z, node);
        }
    }
end_expr_building:;

    parser_debug_log_node_end(p, "expression");

    struct zis_ast_node_obj *node = expr_builder_generate_expr(&var.expr_builder, p);
    zis_locals_drop(p, var);
    assert(node);
    return node;
}

/// Parse a statement.
/// If the next token is an end of a block (some keyword or EOF), returns NULL.
static struct zis_ast_node_obj *parse_statement(struct zis_parser *p) {
    struct zis_ast_node_obj *node;
    while (true) {
        const enum zis_token_type tok_type = this_token(p)->type;
        if (zis_token_type_is_keyword(tok_type)) {
            error_not_implemented(p, __func__);
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
        if ((node = parse_expression(p))) {
            check_token_type_and_ignore(p, ZIS_TOK_EOS);
            return node;
        }
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
    memset(zis_ast_node_obj_position(var.node), 0, sizeof(struct zis_ast_node_obj_position));
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

static void _parser_dump_ast(
    struct zis_context *z, FILE *fp,
    struct zis_ast_node_obj *node, unsigned int level
) {
    const enum zis_ast_node_type node_type = zis_ast_node_obj_type(node);
    const struct zis_ast_node_obj_position *const node_pos =
        zis_ast_node_obj_position(node);
    struct zis_context_globals *const g = z->globals;
    fprintf(
        fp, "%*c%s pos=\"%u:%u-%u:%u\">\n",
        level, '<', zis_ast_node_type_represent(node_type),
        node_pos->line0, node_pos->column0, node_pos->line1, node_pos->column1
    );
    const char *f_names[4]; struct zis_type_obj *f_types[4];
    int f_n = zis_ast_node_type_fields(z, node_type, f_names, f_types);
    assert(f_n >= 0);
    for (int i = 0; i < f_n; i++) {
        fprintf(fp, "%*c%s>\n", level + 1, '<', f_names[i]);
        struct zis_object *const field_obj = node->_data[i];
        struct zis_type_obj *field_obj_type;
        if (zis_object_is_smallint(field_obj)) {
            fprintf(fp, "%*c%lli\n", level + 1, ' ', (long long)zis_smallint_from_ptr(field_obj));
        } else if ((field_obj_type = zis_object_type(field_obj)), field_obj_type == g->type_AstNode) {
            _parser_dump_ast(z, fp, zis_object_cast(field_obj, struct zis_ast_node_obj), level + 2);
        } else if (field_obj_type == g->type_Array) {
            struct zis_array_obj *const arr_obj = zis_object_cast(field_obj, struct zis_array_obj);
            for (size_t i = 0, n = zis_array_obj_length(arr_obj); i < n; i++) {
                struct zis_object *elem = zis_array_obj_get(arr_obj, i);
                if (!zis_object_is_smallint(elem) && zis_object_type(elem) == g->type_AstNode)
                    _parser_dump_ast(z, fp, zis_object_cast(elem, struct zis_ast_node_obj), level + 2);
                else
                    fprintf(fp, "%*c...\n", level + 1, ' ');
            }
        } else if (field_obj_type == g->type_Symbol) {
            struct zis_symbol_obj *const sym_obj = zis_object_cast(field_obj, struct zis_symbol_obj);
            const char *s = zis_symbol_obj_data(sym_obj);
            const int n = (int)zis_symbol_obj_data_size(sym_obj);
            fprintf(fp, "%*c%.*s\n", level + 1, ' ', n, s);
        } else if (field_obj_type == g->type_String) {
            char buffer[64]; size_t size = sizeof buffer;
            size = zis_string_obj_value(zis_object_cast(field_obj, struct zis_string_obj), buffer, size);
            if (size != (size_t)-1)
                fprintf(fp, "%*c<![CDATA[%.*s]]>\n", level + 1, ' ', (int)size, buffer);
            else
                fprintf(fp, "%*c(long string)\n", level + 1, ' ');
        } else if (field_obj_type == g->type_Bool) {
            const char *s = field_obj == zis_object_from(g->val_true) ? "true" : "false";
            fprintf(fp, "%*c%s\n", level + 1, ' ', s);
        } else {
            fprintf(fp, "%*c...\n", level + 1, ' ');
        }
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
