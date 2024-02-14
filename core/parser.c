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
#include "lexer.h"
#include "locale.h"
#include "memory.h"
#include "object.h"
#include "objmem.h"
#include "token.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "stringobj.h"

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
    (zis_debug_log(TRACE, "Parser", "%-*c<%s/>", (int)(__p)->tree_depth--, ' ', (__type)))

/* ----- convinient functions ----------------------------------------------- */

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

static void assert_token_type(struct zis_parser *restrict p, enum zis_token_type tt) {
    if (zis_unlikely(this_token(p)->type != tt))
        error_unexpected_token(p, tt);
}

static void assert_token_type_and_ignore(struct zis_parser *restrict p, enum zis_token_type tt) {
    assert_token_type(p, tt);
    next_token(p);
}

static void node_copy_token_pos(
    struct zis_ast_node_obj *restrict node, const struct zis_token *restrict tok
) {
    struct zis_ast_node_obj_position *pos = zis_ast_node_obj_position(node);
    pos->line0 = tok->line0, pos->column0 = tok->column0;
    pos->line1 = tok->line1, pos->column1 = tok->column1;
}

/* ----- parsing implementation --------------------------------------------- */

#define E(NAME, FIELD_LIST) \
    static struct zis_ast_node_obj *parse_##NAME(struct zis_parser *p);
ZIS_AST_NODE_LIST
#undef E

/// Parse an expression.
static struct zis_ast_node_obj *parse_expression(struct zis_parser *p) {
    zis_context_panic(parser_z(p), ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
}

/// Parse a statement. If the next token is an end of a block, returns NULL.
static struct zis_ast_node_obj *parse_statement(struct zis_parser *p) {
    if (zis_token_type_is_keyword(this_token(p)->type)) {
        zis_context_panic(parser_z(p), ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
    }
    return parse_expression(p);
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
    zis_locals_drop(z, var);
    return var.stmt_list;
}

static struct zis_ast_node_obj *parse_Constant(struct zis_parser *p) {
    parser_debug_log_node(p, "Constant");
    const struct zis_token *tok = this_token(p);
    assert(zis_token_type_is_literal(tok->type));
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Constant, false);
    zis_ast_node_set_field(node, Constant, value, tok->value);
    node_copy_token_pos(node, tok);
    next_token(p);
    return node;
}

static struct zis_ast_node_obj *parse_Name(struct zis_parser *p) {
    parser_debug_log_node(p, "Name");
    assert_token_type(p, ZIS_TOK_IDENTIFIER);
    struct zis_ast_node_obj *node = zis_ast_node_new(parser_z(p), Name, false);
    const struct zis_token *tok = this_token(p);
    zis_ast_node_set_field(node, Name, value, tok->value_identifier);
    node_copy_token_pos(node, tok);
    next_token(p);
    return node;
}

static struct zis_ast_node_obj *parse_Module(struct zis_parser *p) {
    parser_debug_log_node_begin(p, "Module");
    zis_locals_decl_1(p, var, struct zis_ast_node_obj *node);
    zis_locals_zero_1(var, node);
    var.node = zis_ast_node_new(parser_z(p), Module, true);
    zis_ast_node_set_field(var.node, Module, file, zis_string_obj_new(parser_z(p), "", 0));
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
    return result;
}

#endif // ZIS_FEATURE_SRC
