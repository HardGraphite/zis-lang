#include "codegen.h"

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

#include "assembly.h"
#include "attributes.h"
#include "ast.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "locals.h"
#include "memory.h"
#include "objmem.h"

#include "exceptobj.h"

#if ZIS_FEATURE_SRC

/* ----- codegen state ------------------------------------------------------ */

struct zis_codegen {
    struct zis_locals_root locals_root;
    struct zis_context *z;
    jmp_buf error_jb;
};

static void codegen_gc_visit(void *_cg, enum zis_objmem_obj_visit_op op) {
    struct zis_codegen *const cg = _cg;
    _zis_locals_root_gc_visit(&cg->locals_root, (int)op);
}

static struct zis_context *codegen_z(struct zis_codegen *restrict cg) {
    return cg->z;
}

/// Error handling: setjmp().
#define codegen_error_setjmp(__codegen) \
    (setjmp((__codegen)->error_jb))

/// Error handling: longjmp().
#define codegen_error_longjmp(__codegen) \
    (longjmp((__codegen)->error_jb, 1))

/// Format an error and do longjump.
zis_printf_fn_attrs(3, 4) zis_noreturn zis_noinline zis_cold_fn
static void error(
    struct zis_codegen *restrict cg,
    struct zis_ast_node_obj *err_node,
    zis_printf_fn_arg_fmtstr const char *restrict fmt, ...
) {
    struct zis_ast_node_obj_location err_loc = *zis_ast_node_obj_location(err_node);
    char msg_buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, sizeof msg_buf, fmt, ap);
    va_end(ap);
    zis_debug_log(WARN, "CGen", "error@(%u,%u): %s", err_loc.line0, err_loc.column0, msg_buf);
    struct zis_context *z = codegen_z(cg);
    struct zis_exception_obj *exc = zis_exception_obj_format(
        z, "syntax", NULL,
        "%u:%u: %s", err_loc.line0, err_loc.column0, msg_buf
    );
    zis_context_set_reg0(z, zis_object_from(exc));
    codegen_error_longjmp(cg);
}

/* ----- convinient functions ----------------------------------------------- */

zis_noreturn zis_noinline zis_cold_fn static void error_not_implemented(
    struct zis_codegen *restrict cg, const char *restrict fn,
    struct zis_ast_node_obj *err_node
) {
    error(cg, err_node, "not implemented: %s()", fn);
}

/* ----- handlers for different AST nodes ----------------------------------- */

/// "No-target", for parameter `tgt_reg` of `codegen_node_handler_t` functions.
#define NTGT UINT_MAX

/// Type for `emit_<Type>()` functions.
/// Parameter `node` is the AST node to handle.
/// Parameter `tgt_reg` is the register index to store the instruction result to; or `NTGT`.
typedef void(*codegen_node_handler_t) \
    (struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg);

#define E(NAME, FIELD_LIST) \
    static void emit_##NAME(struct zis_codegen *, struct zis_ast_node_obj *, unsigned int);
    ZIS_AST_NODE_LIST
#undef E

static const codegen_node_handler_t
codegen_node_handlers[(unsigned int)_ZIS_AST_NODE_TYPE_COUNT] = {
#define E(NAME, FIELD_LIST) [(unsigned int)ZIS_AST_NODE_##NAME] = emit_##NAME,
    ZIS_AST_NODE_LIST
#undef E
};

static void emit_any(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    unsigned int node_type_index = (unsigned int)zis_ast_node_obj_type(node);
    assert(node_type_index < (unsigned int)_ZIS_AST_NODE_TYPE_COUNT);
    codegen_node_handlers[node_type_index](cg, node, tgt_reg);
}

static void emit_Nil(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Nil);
    error_not_implemented(cg, __func__, node);
}

static void emit_Bool(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Bool);
    error_not_implemented(cg, __func__, node);
}

static void emit_Constant(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Constant);
    error_not_implemented(cg, __func__, node);
}

static void emit_Name(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Name);
    error_not_implemented(cg, __func__, node);
}

static void emit_Pos(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Pos);
    error_not_implemented(cg, __func__, node);
}

static void emit_Neg(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Neg);
    error_not_implemented(cg, __func__, node);
}

static void emit_BitNot(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitNot);
    error_not_implemented(cg, __func__, node);
}

static void emit_Not(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Not);
    error_not_implemented(cg, __func__, node);
}

static void emit_Add(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Add);
    error_not_implemented(cg, __func__, node);
}

static void emit_Sub(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Sub);
    error_not_implemented(cg, __func__, node);
}

static void emit_Mul(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Mul);
    error_not_implemented(cg, __func__, node);
}

static void emit_Div(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Div);
    error_not_implemented(cg, __func__, node);
}

static void emit_Rem(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Rem);
    error_not_implemented(cg, __func__, node);
}

static void emit_Shl(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Shl);
    error_not_implemented(cg, __func__, node);
}

static void emit_Shr(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Shr);
    error_not_implemented(cg, __func__, node);
}

static void emit_BitAnd(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitAnd);
    error_not_implemented(cg, __func__, node);
}

static void emit_BitOr(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitOr);
    error_not_implemented(cg, __func__, node);
}

static void emit_BitXor(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitXor);
    error_not_implemented(cg, __func__, node);
}

static void emit_Assign(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Assign);
    error_not_implemented(cg, __func__, node);
}

static void emit_Eq(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Eq);
    error_not_implemented(cg, __func__, node);
}

static void emit_Ne(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Ne);
    error_not_implemented(cg, __func__, node);
}

static void emit_Lt(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Lt);
    error_not_implemented(cg, __func__, node);
}

static void emit_Le(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Le);
    error_not_implemented(cg, __func__, node);
}

static void emit_Gt(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Gt);
    error_not_implemented(cg, __func__, node);
}

static void emit_Ge(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Ge);
    error_not_implemented(cg, __func__, node);
}

static void emit_And(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_And);
    error_not_implemented(cg, __func__, node);
}

static void emit_Or(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Or);
    error_not_implemented(cg, __func__, node);
}

static void emit_Subscript(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Subscript);
    error_not_implemented(cg, __func__, node);
}

static void emit_Field(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Field);
    error_not_implemented(cg, __func__, node);
}

static void emit_Call(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Call);
    error_not_implemented(cg, __func__, node);
}

static void emit_Send(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Send);
    error_not_implemented(cg, __func__, node);
}

static void emit_Tuple(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Tuple);
    error_not_implemented(cg, __func__, node);
}

static void emit_Array(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Array);
    error_not_implemented(cg, __func__, node);
}

static void emit_Map(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Map);
    error_not_implemented(cg, __func__, node);
}

static void emit_Import(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Import);
    error_not_implemented(cg, __func__, node);
}

static void emit_Return(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Return);
    error_not_implemented(cg, __func__, node);
}

static void emit_Throw(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Throw);
    error_not_implemented(cg, __func__, node);
}

static void emit_Break(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Break);
    error_not_implemented(cg, __func__, node);
}

static void emit_Continue(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Continue);
    error_not_implemented(cg, __func__, node);
}

static void emit_Cond(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Cond);
    error_not_implemented(cg, __func__, node);
}

static void emit_While(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_While);
    error_not_implemented(cg, __func__, node);
}

static void emit_Func(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Func);
    error_not_implemented(cg, __func__, node);
}

static void emit_Module(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Module);
    error_not_implemented(cg, __func__, node);
}

/* ----- public functions --------------------------------------------------- */

struct zis_codegen *zis_codegen_create(struct zis_context *z) {
    struct zis_codegen *cg = zis_mem_alloc(sizeof(struct zis_codegen));
    zis_locals_root_init(&cg->locals_root, NULL);
    cg->z = z;
    zis_objmem_add_gc_root(z, cg, codegen_gc_visit);
    return cg;
}

void zis_codegen_destroy(struct zis_codegen *cg, struct zis_context *z) {
    assert(cg->z == z);
    zis_locals_root_fini(&cg->locals_root, NULL);
    zis_objmem_remove_gc_root(z, cg);
    zis_mem_free(cg);
}

struct zis_func_obj *zis_codegen_generate(
    struct zis_codegen *cg,
    struct zis_ast_node_obj *ast
) {
    assert(!cg->locals_root._list);

    struct zis_func_obj *result;
    if (!codegen_error_setjmp(cg)) {
        emit_any(cg, ast, NTGT);
        error_not_implemented(cg, __func__, ast);
    } else {
        zis_locals_root_reset(&cg->locals_root);
        result = NULL;
    }

    assert(!cg->locals_root._list);

    return result;
}

#endif // ZIS_FEATURE_SRC
