#include "codegen.h"

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
#include "mapobj.h"
#include "symbolobj.h"

#if ZIS_FEATURE_SRC

/* ----- code scopes -------------------------------------------------------- */

enum scope_type {
    SCOPE_FRAME,
    SCOPE_VAR,
    SCOPE_LOOP,
};

union scope_ptr {
    struct scope       *any;
    struct frame_scope *frame;
    struct var_scope   *var;
    struct loop_scope  *loop;
};

#define SCOPE_COMMON_HEAD \
    enum scope_type type; \
    union scope_ptr _parent_scope; \
// ^^^ SCOPE_COMMON_HEAD ^^^

struct scope {
    SCOPE_COMMON_HEAD
};

struct frame_scope_free_regs { unsigned int start, end /* exclusive end */; };

/// Frame, a function or module top-level.
struct frame_scope {
    SCOPE_COMMON_HEAD
    struct zis_assembler *as;
    struct zis_map_obj *var_map; // { name (symbol) -> reg (smallint) }
    unsigned int reg_touched_max, reg_allocated_max;
    struct frame_scope_free_regs *free_regs_list; // sorted
    size_t free_regs_list_len, free_regs_list_cap;
};

static struct frame_scope *frame_scope_create(struct zis_context *z) {
    struct frame_scope *fs = zis_mem_alloc(sizeof(struct frame_scope));
    fs->type = SCOPE_FRAME;
    fs->as = zis_assembler_create(z, NULL); // FIXME: the parameter `parent`.
    fs->var_map = zis_map_obj_new(z, 0.0f, 0);
    fs->reg_touched_max = 0, fs->reg_allocated_max = 0;
    fs->free_regs_list = NULL, fs->free_regs_list_len = 0, fs->free_regs_list_cap = 0;
    return fs;
}

static void frame_scope_destory(struct frame_scope *fs, struct zis_context *z) {
    assert(fs->type == SCOPE_FRAME);
    zis_assembler_destroy(fs->as, z, NULL);
    zis_mem_free(fs->free_regs_list);
    zis_mem_free(fs);
}

static void frame_scope_gc_visit(
    struct frame_scope *fs, enum zis_objmem_obj_visit_op op
) {
    assert(fs->type == SCOPE_FRAME);
    struct zis_object **map_p = (struct zis_object **)&fs->var_map;
    zis_objmem_visit_object_vec(map_p, map_p + 1, op);
}

static void frame_scope_reset(struct frame_scope *fs) {
    assert(fs->type == SCOPE_FRAME);
    zis_assembler_clear(fs->as);
    zis_map_obj_clear(fs->var_map);
    fs->reg_touched_max = 0, fs->reg_allocated_max = 0;
    fs->free_regs_list_len = 0;
}

static void frame_scope__free_regs_list_remove(
    struct frame_scope *restrict fs, size_t position
) {
    struct frame_scope_free_regs *const free_regs_list = fs->free_regs_list;
    const size_t free_regs_list_len = fs->free_regs_list_len;
    assert(position < free_regs_list_len);
    if (position != free_regs_list_len - 1) {
        memmove(
            free_regs_list + position, free_regs_list + position + 1,
            (free_regs_list_len - position - 1) * sizeof free_regs_list[0]
        );
    }
    fs->free_regs_list_len = free_regs_list_len - 1;
}

static void frame_scope__free_regs_list_insert(
    struct frame_scope *restrict fs, size_t position,
    struct frame_scope_free_regs data
) {
    struct frame_scope_free_regs *const free_regs_list = fs->free_regs_list;
    const size_t free_regs_list_len = fs->free_regs_list_len;

    assert(position <= fs->free_regs_list_len);
    assert(position == 0 || free_regs_list[position - 1].end <= data.start);
    assert(position == free_regs_list_len || data.end <= free_regs_list[position].start);

    if (position && free_regs_list[position - 1].end == data.start) {
        if (position != free_regs_list_len && data.end == free_regs_list[position].start) {
            free_regs_list[position - 1].end += free_regs_list[position].end;
            frame_scope__free_regs_list_remove(fs, position);
        } else {
            free_regs_list[position - 1].end += data.end;
        }
        return;
    }

    if (position != free_regs_list_len && data.end == free_regs_list[position].start) {
        free_regs_list[position].start = data.start;
        return;
    }

    assert(free_regs_list_len <= fs->free_regs_list_cap);
    if (free_regs_list_len == fs->free_regs_list_cap) {
        const size_t old_cap = fs->free_regs_list_cap;
        const size_t new_cap = old_cap ? old_cap * 2 : 4;
        fs->free_regs_list = zis_mem_realloc(
            fs->free_regs_list,
            new_cap * sizeof(struct frame_scope_free_regs)
        );
        fs->free_regs_list_cap = new_cap;
    }

    if (position != free_regs_list_len) {
        memmove(
            free_regs_list + position + 1, free_regs_list + position,
            (free_regs_list_len - position) * sizeof free_regs_list[0]
        );
    }
    free_regs_list[position] = data;
    fs->free_regs_list_len = free_regs_list_len + 1;
}

static unsigned int frame_scope_alloc_regs(
    struct frame_scope *fs, unsigned int n
) {
    assert(n);

    if (fs->free_regs_list_len) {
        struct frame_scope_free_regs *const free_regs_list = fs->free_regs_list;
        const size_t free_regs_list_len = fs->free_regs_list_len;
        unsigned int min_n = UINT_MAX; size_t min_n_i = 0;
        for (size_t i = 0; i < free_regs_list_len; i++) {
            struct frame_scope_free_regs *regs_i = &free_regs_list[i];
            const unsigned int regs_i_n = regs_i->end - regs_i->start;
            if (regs_i_n == n) {
                const unsigned int reg_start = regs_i->start;
                frame_scope__free_regs_list_remove(fs, i);
                return reg_start;
            }
            if (regs_i_n < min_n && regs_i_n >= n) {
                min_n = regs_i_n;
                min_n_i = i;
            }
        }
        free_regs_list[min_n_i].end -= n;
        return free_regs_list[min_n_i].start;
    }

    const unsigned int reg = fs->reg_allocated_max + 1;
    fs->reg_allocated_max += n;
    assert(fs->reg_allocated_max > reg);
    if (fs->reg_allocated_max > fs->reg_touched_max)
        fs->reg_touched_max = fs->reg_allocated_max;
    return reg;
}

static unsigned int frame_scope_alloc_var(
    struct frame_scope *fs, struct zis_context *z,
    struct zis_symbol_obj *name
) {
    assert(!zis_map_obj_sym_get(fs->var_map, name));

    unsigned int reg = frame_scope_alloc_regs(fs, 1);
    zis_map_obj_sym_set(z, fs->var_map, name, zis_smallint_to_ptr((zis_smallint_t)reg));
    return reg;
}

static void frame_scope_free_regs(
    struct frame_scope *fs, unsigned int regs_start, unsigned int n
) {
    const struct frame_scope_free_regs freed_regs = {
        .start = regs_start,
        .end = regs_start + n,
    };

    assert(n);
    assert(freed_regs.end - 1 <= fs->reg_allocated_max);

    if (freed_regs.end - 1 == fs->reg_allocated_max) {
        fs->reg_allocated_max -= n;
        return;
    }

    struct frame_scope_free_regs *const free_regs_list = fs->free_regs_list;
    const unsigned int free_regs_list_len = (unsigned int)fs->free_regs_list_len;

    if (!free_regs_list_len || freed_regs.start < free_regs_list[0].start) {
        frame_scope__free_regs_list_insert(fs, 0, freed_regs);
        return;
    }

    if (freed_regs.start > free_regs_list[free_regs_list_len - 1].end) {
        frame_scope__free_regs_list_insert(fs, free_regs_list_len, freed_regs);
        return;
    }

    for (unsigned int index_l = 0, index_r = free_regs_list_len;;) {
        const unsigned int index_m = index_l + (index_r - index_l) / 2;
        struct frame_scope_free_regs *free_regs_m = &free_regs_list[index_m];
        if (freed_regs.start < free_regs_m->start) {
            assert(freed_regs.end <= free_regs_m->start);
            if (index_m > 0 && freed_regs.start > free_regs_list[index_m - 1].start) {
                frame_scope__free_regs_list_insert(fs, index_m, freed_regs);
                return;
            }
            index_r = index_m - 1;
        } else {
            assert(freed_regs.start > free_regs_m->start);
            assert(freed_regs.start >= free_regs_m->end);
            if (index_m + 1 < free_regs_list_len && freed_regs.start < free_regs_list[index_m + 1].start) {
                frame_scope__free_regs_list_insert(fs, index_m + 1, freed_regs);
                break;
            }
            index_l = index_m + 1;
        }
        assert((int)index_l <= (int)index_r);
    }
}

static void frame_scope_free_var(
    struct frame_scope *fs, struct zis_context *z,
    struct zis_symbol_obj *name
) {
    struct zis_object *reg_smi = zis_map_obj_sym_get(fs->var_map, name);
    assert(reg_smi && zis_object_is_smallint(reg_smi));
    bool ok = zis_map_obj_unset(z, fs->var_map, zis_object_from(name)) == 0; // TODO: faster unset
    zis_unused_var(ok), assert(ok);
    frame_scope_free_regs(fs, (unsigned int)zis_smallint_from_ptr(reg_smi), 1);
}

static unsigned int frame_scope_find_var(
    struct frame_scope *fs,
    struct zis_symbol_obj *name
) {
    struct zis_object *reg_smi = zis_map_obj_sym_get(fs->var_map, name);
    if (!reg_smi)
        return 0; // no such a variable
    assert(zis_object_is_smallint(reg_smi));
    return (unsigned int)zis_smallint_from_ptr(reg_smi);
}

/// Variable scope.
struct var_scope {
    SCOPE_COMMON_HEAD
    struct frame_scope *frame;
    struct zis_symbol_obj **vars;
    size_t vars_len, vars_cap;
};

static struct var_scope *var_scope_create(void) {
    struct var_scope *vs = zis_mem_alloc(sizeof(struct var_scope));
    vs->type = SCOPE_VAR;
    vs->vars = NULL, vs->vars_len = 0, vs->vars_cap = 0;
    return vs;
}

static void var_scope_destory(struct var_scope *vs) {
    assert(vs->type == SCOPE_VAR);
    zis_mem_free(vs->vars);
    zis_mem_free(vs);
}

static void var_scope_gc_visit(
    struct var_scope *vs, enum zis_objmem_obj_visit_op op
) {
    assert(vs->type == SCOPE_VAR);
    struct zis_object **vars_p = (struct zis_object **)vs->vars;
    zis_objmem_visit_object_vec(vars_p, vars_p + vs->vars_len, op);
}

static void var_scope_reset(struct var_scope *vs) {
    assert(vs->type == SCOPE_VAR);
    vs->vars_len = 0;
}

static unsigned int var_scope_alloc_var(
    struct var_scope *vs, struct zis_context *z,
    struct zis_symbol_obj *name
) {
    assert(vs->vars_len <= vs->vars_cap);
    if (vs->vars_len == vs->vars_cap) {
        const size_t old_cap = vs->vars_cap;
        const size_t new_cap = old_cap ? old_cap * 2 : 8;
        vs->vars = zis_mem_realloc(vs->vars, new_cap * sizeof(void *));
        vs->vars_cap = new_cap;
    }
    vs->vars[vs->vars_len++] = name;
    return frame_scope_alloc_var(vs->frame, z, name);
}

static void var_scope_free_vars(
    struct var_scope *vs, struct zis_context *z
) {
    struct frame_scope *vs_fs = vs->frame;
    for (size_t i = vs->vars_len; i > 0; i--) {
        frame_scope_free_var(vs_fs, z, vs->vars[i - 1]);
    }
    vs->vars_len = 0;
}

/// Loop, supporting continue and break.
struct loop_scope {
    SCOPE_COMMON_HEAD
    int label_continue, label_break;
};

static struct loop_scope *loop_scope_create(void) {
    struct loop_scope *ls = zis_mem_alloc(sizeof(struct loop_scope));
    ls->type = SCOPE_LOOP;
    ls->label_continue = -1, ls->label_break = -1;
    return ls;
}

static void loop_scope_destory(struct loop_scope *ls) {
    assert(ls->type == SCOPE_LOOP);
    zis_mem_free(ls);
}

static void loop_scope_reset(struct loop_scope *ls) {
    assert(ls->type == SCOPE_LOOP);
    ls->label_continue = -1, ls->label_break = -1;
}

/// Stack of scopes.
struct scope_stack {
    union scope_ptr     _scopes;
    struct frame_scope *_free_frame_scopes;
    struct var_scope   *_free_var_scopes;
    struct loop_scope  *_free_loop_scopes;
};

static void scope_stack_init(struct scope_stack *restrict ss) {
    ss->_scopes.any        = NULL;
    ss->_free_frame_scopes = NULL;
    ss->_free_var_scopes   = NULL;
    ss->_free_loop_scopes  = NULL;
}

static void scope_stack_fini(struct scope_stack *restrict ss, struct zis_context *z) {
    union scope_ptr scopes[4];
    scopes[0].frame = ss->_free_frame_scopes;
    scopes[1].var   = ss->_free_var_scopes;
    scopes[2].loop  = ss->_free_loop_scopes;
    scopes[3]       = ss->_scopes;

    for (unsigned int i = 0; i < sizeof scopes / sizeof scopes[0]; i++) {
        for (union scope_ptr s = scopes[i]; s.any; s = s.any->_parent_scope) {
            switch (s.any->type) {
            case SCOPE_LOOP:
                loop_scope_destory(s.loop);
                break;
            case SCOPE_VAR:
                var_scope_destory(s.var);
                break;
            case SCOPE_FRAME:
                frame_scope_destory(s.frame, z);
                break;
            default:
                zis_unreachable();
            }
        }
    }
}

static void scope_stack_gc_visitor(struct scope_stack *ss, enum zis_objmem_obj_visit_op op) {
    union scope_ptr scopes[4];
    scopes[0]       = ss->_scopes;
    scopes[1].frame = ss->_free_frame_scopes;
    scopes[2].var   = ss->_free_var_scopes;
    scopes[3].loop  = ss->_free_loop_scopes;

    for (unsigned int i = 0; i < sizeof scopes / sizeof scopes[0]; i++) {
        for (union scope_ptr s = scopes[i]; s.any; s = s.any->_parent_scope) {
            switch (s.any->type) {
            case SCOPE_LOOP:
                break;
            case SCOPE_VAR:
                var_scope_gc_visit(s.var, op);
                break;
            case SCOPE_FRAME:
                frame_scope_gc_visit(s.frame, op);
                break;
            default:
                zis_unreachable();
            }
        }
    }
}

static struct frame_scope *scope_stack_push_frame_scope(
    struct scope_stack *restrict ss, struct zis_context *z
) {
    struct frame_scope *fs;
    if (ss->_free_frame_scopes) {
        fs = ss->_free_frame_scopes;
        assert(!fs->_parent_scope.any || fs->_parent_scope.any->type == SCOPE_FRAME);
        ss->_free_frame_scopes = fs->_parent_scope.frame;
    } else {
        fs = frame_scope_create(z);
    }
    fs->_parent_scope = ss->_scopes;
    ss->_scopes.frame = fs;
    return fs;
}

static struct var_scope *scope_stack_push_var_scope(
    struct scope_stack *restrict ss
) {
    struct var_scope *vs;
    if (ss->_free_var_scopes) {
        vs = ss->_free_var_scopes;
        assert(!vs->_parent_scope.any || vs->_parent_scope.any->type == SCOPE_VAR);
        ss->_free_var_scopes = vs->_parent_scope.var;
    } else {
        vs = var_scope_create();
    }
    vs->_parent_scope = ss->_scopes;
    ss->_scopes.var = vs;
    return vs;
}

static struct loop_scope *scope_stack_push_loop_scope(
    struct scope_stack *restrict ss
) {
    struct loop_scope *ls;
    if (ss->_free_loop_scopes) {
        ls = ss->_free_loop_scopes;
        assert(!ls->_parent_scope.any || ls->_parent_scope.any->type == SCOPE_LOOP);
        ss->_free_loop_scopes = ls->_parent_scope.loop;
    } else {
        ls = loop_scope_create();
    }
    ls->_parent_scope = ss->_scopes;
    ss->_scopes.loop = ls;
    return ls;
}

static void scope_stack_pop_frame_scope(
    struct scope_stack *restrict ss
) {
    assert(ss->_scopes.any && ss->_scopes.any->type == SCOPE_FRAME);
    struct frame_scope *fs = ss->_scopes.frame;
    ss->_scopes = fs->_parent_scope;
    frame_scope_reset(fs);
    fs->_parent_scope.frame = ss->_free_frame_scopes;
    ss->_free_frame_scopes = fs;
}

static void scope_stack_pop_var_scope(
    struct scope_stack *restrict ss, struct zis_context *z
) {
    assert(ss->_scopes.any && ss->_scopes.any->type == SCOPE_VAR);
    struct var_scope *vs = ss->_scopes.var;
    ss->_scopes = vs->_parent_scope;
    var_scope_free_vars(vs, z);
    var_scope_reset(vs);
    vs->_parent_scope.var = ss->_free_var_scopes;
    ss->_free_var_scopes = vs;
}

static void scope_stack_pop_loop_scope(
    struct scope_stack *restrict ss
) {
    assert(ss->_scopes.any && ss->_scopes.any->type == SCOPE_LOOP);
    struct loop_scope *ls = ss->_scopes.loop;
    ss->_scopes = ls->_parent_scope;
    loop_scope_reset(ls);
    ls->_parent_scope.loop = ss->_free_loop_scopes;
    ss->_free_loop_scopes = ls;
}

static union scope_ptr scope_stack_current_scope(struct scope_stack *restrict ss) {
    assert(ss->_scopes.any);
    return ss->_scopes;
}

static union scope_ptr scope_stack_last_frame_or_var_scope(
    struct scope_stack *restrict ss
) {
    union scope_ptr s = ss->_scopes;
    assert(s.any);
    while (!(s.any->type == SCOPE_FRAME || s.any->type == SCOPE_VAR)) {
        s = s.any->_parent_scope;
        assert(s.any);
    }
    return s;
}

static struct loop_scope *scope_stack_last_loop_scope(
    struct scope_stack *restrict ss
) {
    union scope_ptr s = ss->_scopes;
    while (s.any && s.any->type != SCOPE_LOOP)
        s = s.any->_parent_scope;
    return s.loop; // May be Null.
}

/* ----- codegen state ------------------------------------------------------ */

struct zis_codegen {
    struct zis_locals_root locals_root;
    struct scope_stack scope_stack;
    struct zis_context *z;
    jmp_buf error_jb;
};

static void codegen_gc_visit(void *_cg, enum zis_objmem_obj_visit_op op) {
    struct zis_codegen *const cg = _cg;
    _zis_locals_root_gc_visit(&cg->locals_root, (int)op);
    scope_stack_gc_visitor(&cg->scope_stack, op);
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

/// Get reg index of a local variable. Returns 0 if not found.
static unsigned int scope_find_var(
    struct zis_codegen *restrict cg,
    struct zis_symbol_obj *name
) {
    union scope_ptr scope = scope_stack_last_frame_or_var_scope(&cg->scope_stack);
    struct frame_scope *fs;
    if (scope.any->type == SCOPE_VAR) {
        fs = scope.var->frame;
    } else {
        assert(scope.any->type == SCOPE_FRAME);
        fs = scope.frame;
    }
    return frame_scope_find_var(fs, name);
}

/// Get reg index of a local variable. Allocate one if not found.
static unsigned int scope_find_or_alloc_var(
    struct zis_codegen *restrict cg, struct zis_context *z,
    struct zis_symbol_obj *name
) {
    unsigned int reg;
    union scope_ptr scope = scope_stack_last_frame_or_var_scope(&cg->scope_stack);
    if (scope.any->type == SCOPE_VAR) {
        reg = frame_scope_find_var(scope.var->frame, name);
        if (!reg)
            reg = var_scope_alloc_var(scope.var, z, name);
    } else {
        assert(scope.any->type == SCOPE_FRAME);
        reg = frame_scope_find_var(scope.frame, name);
        if (!reg)
            reg = frame_scope_alloc_var(scope.frame, z, name);
    }
    return reg;
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
    scope_stack_init(&cg->scope_stack);
    cg->z = z;
    zis_objmem_add_gc_root(z, cg, codegen_gc_visit);
    return cg;
}

void zis_codegen_destroy(struct zis_codegen *cg, struct zis_context *z) {
    assert(cg->z == z);
    scope_stack_fini(&cg->scope_stack, z);
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
