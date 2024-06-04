#include "codegen.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "assembly.h"
#include "ast.h"
#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "locals.h"
#include "memory.h"
#include "objmem.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "floatobj.h"
#include "funcobj.h"
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
    fs->var_map = zis_map_obj_new(z, 0.0F, 0);
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
    assert(free_regs_list);
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
    struct frame_scope_free_regs *free_regs_list = fs->free_regs_list;
    const size_t free_regs_list_len = fs->free_regs_list_len;

    assert(position <= fs->free_regs_list_len);
    assert(position == 0 || free_regs_list[position - 1].end <= data.start);
    assert(position == free_regs_list_len || data.end <= free_regs_list[position].start);

    if (position && free_regs_list[position - 1].end == data.start) {
        if (position != free_regs_list_len && data.end == free_regs_list[position].start) {
            free_regs_list[position - 1].end = free_regs_list[position].end;
            frame_scope__free_regs_list_remove(fs, position);
        } else {
            free_regs_list[position - 1].end = data.end;
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
        free_regs_list = zis_mem_realloc(
            fs->free_regs_list,
            new_cap * sizeof(struct frame_scope_free_regs)
        );
        fs->free_regs_list = free_regs_list;
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
                zis_debug_log(TRACE, "CGen", "frame_scope_alloc_regs(%u) -> %u (free_list[%zu])", n, reg_start, i);
                return reg_start;
            }
            if (regs_i_n > n && regs_i_n < min_n) {
                min_n = regs_i_n;
                min_n_i = i;
            }
        }
        if (min_n != UINT_MAX) {
            assert(min_n > n);
            assert(min_n_i < free_regs_list_len);
            const unsigned int reg = free_regs_list[min_n_i].start;
            free_regs_list[min_n_i].start = reg + n;
            assert(free_regs_list[min_n_i].start + 1 < free_regs_list[min_n_i].end);
            zis_debug_log(TRACE, "CGen", "frame_scope_alloc_regs(%u) -> %u (free_list[%zu][0:%u])", n, reg, min_n_i, n);
            return reg;
        }
    }

    const unsigned int reg = fs->reg_allocated_max + 1;
    fs->reg_allocated_max += n;
    assert(fs->reg_allocated_max >= reg);
    if (fs->reg_allocated_max > fs->reg_touched_max)
        fs->reg_touched_max = fs->reg_allocated_max;
    zis_debug_log(TRACE, "CGen", "frame_scope_alloc_regs(%u) -> %u (new)", n, reg);
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
        if (fs->free_regs_list_len) {
            const size_t last_i = fs->free_regs_list_len - 1;
            if (fs->free_regs_list[last_i].end - 1 == fs->reg_allocated_max) {
                fs->reg_allocated_max = fs->free_regs_list[last_i].start - 1;
                frame_scope__free_regs_list_remove(fs, last_i);
            }
        }
        zis_debug_log(TRACE, "CGen", "frame_scope_free_regs(%u, %u) (shrink tail)", regs_start, n);
        return;
    }

    struct frame_scope_free_regs *const free_regs_list = fs->free_regs_list;
    const unsigned int free_regs_list_len = (unsigned int)fs->free_regs_list_len;

    if (!free_regs_list_len || freed_regs.start < free_regs_list[0].start) {
        frame_scope__free_regs_list_insert(fs, 0, freed_regs);
        zis_debug_log(TRACE, "CGen", "frame_scope_free_regs(%u, %u) (insert front)", regs_start, n);
        return;
    }

    if (freed_regs.start > free_regs_list[free_regs_list_len - 1].end) {
        frame_scope__free_regs_list_insert(fs, free_regs_list_len, freed_regs);
        zis_debug_log(TRACE, "CGen", "frame_scope_free_regs(%u, %u) (insert back)", regs_start, n);
        return;
    }

    for (unsigned int index_l = 0, index_r = free_regs_list_len;;) {
        const unsigned int index_m = index_l + (index_r - index_l) / 2;
        struct frame_scope_free_regs *free_regs_m = &free_regs_list[index_m];
        if (freed_regs.start < free_regs_m->start) {
            assert(freed_regs.end <= free_regs_m->start);
            if (index_m > 0 && freed_regs.start > free_regs_list[index_m - 1].start) {
                frame_scope__free_regs_list_insert(fs, index_m, freed_regs);
                zis_debug_log(TRACE, "CGen", "frame_scope_free_regs(%u, %u) (insert @%u)", regs_start, n, index_m);
                return;
            }
            index_r = index_m - 1;
        } else {
            assert(freed_regs.start > free_regs_m->start);
            assert(freed_regs.start >= free_regs_m->end);
            if (index_m + 1 < free_regs_list_len && freed_regs.start < free_regs_list[index_m + 1].start) {
                frame_scope__free_regs_list_insert(fs, index_m + 1, freed_regs);
                zis_debug_log(TRACE, "CGen", "frame_scope_free_regs(%u, %u) (insert @%u)", regs_start, n, index_m + 1);
                return;
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

/// Update func_meta with argc. Fails (false) if there are too many args.
zis_nodiscard static bool frame_scope_set_argc(
    struct frame_scope *fs, size_t n_args, size_t n_opt_args
) {
    struct zis_assembler *as = fs->as;
    if (zis_unlikely(n_args > UCHAR_MAX || (n_opt_args > UCHAR_MAX && n_opt_args != SIZE_MAX)))
        return false;
    zis_assembler_func_meta(as, &(struct zis_func_obj_meta){
        .na = (unsigned char)n_args,
        .no = (unsigned char)n_opt_args,
        .nr = 0,
    });
    return true;
}

/// Generate function object. Fails (NULL) if too many registers are used.
zis_nodiscard static struct zis_func_obj *frame_scope_gen_func(
    struct frame_scope *fs,
    struct zis_context *z, struct zis_module_obj *module
) {
    const unsigned int reg_max = fs->reg_touched_max;
    if (zis_unlikely(reg_max >= USHRT_MAX))
        return NULL;
    struct zis_assembler *as = fs->as;
    struct zis_func_obj_meta func_meta = *zis_assembler_func_meta(as, NULL);
    func_meta.nr = (unsigned short)(reg_max + 1);
    zis_assembler_func_meta(as, &func_meta);
    return zis_assembler_finish(as, z, module);
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
        for (union scope_ptr s = scopes[i]; s.any; ) {
            const union scope_ptr next_s = s.any->_parent_scope;
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
            s = next_s;
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
    for (union scope_ptr s = ss->_scopes;; s = s.any->_parent_scope, assert(s.any)) {
        if (s.any->type == SCOPE_FRAME) {
            vs->frame = s.frame;
            break;
        }
        if (s.any->type == SCOPE_VAR) {
            vs->frame = s.var->frame;
            break;
        }
    }
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

static struct frame_scope *scope_stack_last_frame_scope(
    struct scope_stack *restrict ss
) {
    union scope_ptr s = ss->_scopes;
    assert(s.any);
    while (true) {
        if (s.any->type == SCOPE_FRAME)
            return s.frame;
        if (s.any->type == SCOPE_VAR)
            return s.var->frame;
        s = s.any->_parent_scope;
        assert(s.any);
    }
}

/// Get the top loop-scope in the current frame. Returns NULL if not found.
static struct loop_scope *scope_stack_last_loop_scope(
    struct scope_stack *restrict ss
) {
    union scope_ptr s = ss->_scopes;
    while (s.any && s.any->type != SCOPE_LOOP) {
        if (s.any->type == SCOPE_FRAME)
            return NULL;
        s = s.any->_parent_scope;
    }
    return s.loop; // May be Null.
}

/* ----- codegen state ------------------------------------------------------ */

struct zis_codegen {
    struct zis_locals_root locals_root;
    struct scope_stack scope_stack;
    struct zis_context *z;
    struct zis_module_obj *module;
    jmp_buf error_jb;
};

static void codegen_gc_visit(void *_cg, enum zis_objmem_obj_visit_op op) {
    struct zis_codegen *const cg = _cg;
    _zis_locals_root_gc_visit(&cg->locals_root, (int)op);
    scope_stack_gc_visitor(&cg->scope_stack, op);
    zis_objmem_visit_object_vec((struct zis_object **)&cg->module, (struct zis_object **)&cg->module + 1, op);
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

zis_noreturn zis_noinline zis_cold_fn static void error_too_many_args(
    struct zis_codegen *restrict cg, struct zis_ast_node_obj *err_node
) {
    error(cg, err_node, "too many arguments");
}

zis_noreturn zis_noinline zis_cold_fn static void error_too_many_regs(
    struct zis_codegen *restrict cg, struct zis_ast_node_obj *err_node
) {
    error(cg, err_node, "too many registers are used");
}

zis_noreturn zis_noinline zis_cold_fn static void error_outside_xxx(
    struct zis_codegen *restrict cg, struct zis_ast_node_obj *err_node, const char *xxx
) {
    const char *const node_type_name =
        zis_ast_node_type_represent(zis_ast_node_obj_type(err_node));
    error(cg, err_node, "<%s> outside %s", node_type_name, xxx);
}

/// Check whether `obj` is an AST node object. Throws and error if not.
static void check_obj_is_node(
    struct zis_codegen *restrict cg,
    struct zis_ast_node_obj *parent_node, struct zis_object *obj
) {
    if (zis_unlikely(!zis_object_type_is(obj, cg->z->globals->type_AstNode)))
        error(cg, parent_node, "sub-node is not a node object");
}

/// Check whether `node` can be a Bool node.
/// Throws an error if `node` is a non-bool constant.
/// Returns -1 if it is false, 1 if it is true, 0 otherwise.
static int check_node_maybe_bool(
    struct zis_codegen *restrict cg, struct zis_ast_node_obj *node
) {
    const enum zis_ast_node_type cond_node_type = zis_ast_node_obj_type(node);
    if (cond_node_type == ZIS_AST_NODE_Bool) {
        struct zis_bool_obj *const cond_node_value =
            zis_ast_node_get_field(node, Bool, value);
        return cond_node_value == codegen_z(cg)->globals->val_true ? 1 : -1;
    } else if (cond_node_type == ZIS_AST_NODE_Nil || cond_node_type == ZIS_AST_NODE_Constant) {
        error(cg, node, "expected boolean expression");
    } else {
        return 0;
    }
}

/// Check whether target register `tgt` is `NTGT`. Throws and error if not.
static void check_tgt_is_ntgt(
    struct zis_codegen *restrict cg,
    struct zis_ast_node_obj *node, unsigned int tgt
) {
    if (zis_unlikely(tgt != UINT_MAX))
        error(cg, node, "unexpected target register");
}

/// Check whether the node is a Nil, a Bool, or a Constant.
zis_nodiscard static bool node_is_constant(struct zis_ast_node_obj *node) {
    enum zis_ast_node_type t = zis_ast_node_obj_type(node);
    return t == ZIS_AST_NODE_Constant || t == ZIS_AST_NODE_Bool || t == ZIS_AST_NODE_Nil;
}

/// Get reg index of a local variable. Returns 0 if not found.
static struct zis_assembler *scope_assembler(struct zis_codegen *restrict cg) {
    union scope_ptr scope = scope_stack_last_frame_or_var_scope(&cg->scope_stack);
    if (scope.any->type == SCOPE_VAR) {
        return scope.var->frame->as;
    } else {
        assert(scope.any->type == SCOPE_FRAME);
        return scope.frame->as;
    }
}

/// Check whether in the toplevel frame scope. `fs` is optional.
static bool scope_frame_is_toplevel(
    struct zis_codegen *restrict cg, struct frame_scope *fs /* = NULL */
) {
    if (!fs)
        fs = scope_stack_last_frame_scope(&cg->scope_stack);
    return !fs->_parent_scope.any;
}

/// Get reg index of a local variable. Allocate one if not found.
/// If in the toplevel frame, returns 0 when variable is not found.
static unsigned int scope_find_or_alloc_var(
    struct zis_codegen *restrict cg, struct zis_context *z,
    struct zis_symbol_obj *name
) {
    unsigned int reg;
    union scope_ptr scope = scope_stack_last_frame_or_var_scope(&cg->scope_stack);
    if (scope.any->type == SCOPE_VAR) {
        reg = frame_scope_find_var(scope.var->frame, name);
        if (!reg && !scope_frame_is_toplevel(cg, scope.var->frame))
            reg = var_scope_alloc_var(scope.var, z, name);
    } else {
        assert(scope.any->type == SCOPE_FRAME);
        reg = frame_scope_find_var(scope.frame, name);
        if (!reg && !scope_frame_is_toplevel(cg, scope.frame))
            reg = frame_scope_alloc_var(scope.frame, z, name);
    }
    return reg;
}

/// Allocate registers in current frame.
/// Prefers low-level functions if the operation is performed server times.
static unsigned int scope_alloc_regs(struct zis_codegen *restrict cg, unsigned int n) {
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    return frame_scope_alloc_regs(fs, n);
}

/* ----- handlers for different AST nodes ----------------------------------- */

/// "No target". See `codegen_node_handler_t`.
#define NTGT UINT_MAX

/// "A target". See `codegen_node_handler_t`.
#define ATGT (UINT_MAX - 1)

/// Get abslute value of an ATGT return value. See `codegen_node_handler_t`.
zis_static_force_inline unsigned int atgt_abs(int reg) {
    return (unsigned int)(reg >= 0 ? reg : -reg); // abs()
}

/// Free an ATGT returned register if it is negative. See `codegen_node_handler_t`.
zis_static_force_inline void atgt_free1(struct frame_scope *fs, int reg) {
    if (reg < 0)
        frame_scope_free_regs(fs, (unsigned int)-reg, 1);
}

/// Type for `emit_<Type>()` functions.
/// Parameter `node` is the AST node to handle.
/// Parameter `tgt_reg` is the register index to store the instruction result to;
/// or `NTGT` indicating that no result is expected; or `ATGT` indicating that
/// an unspecified register should be used (either allocate a temporary register
/// or using an existing one). `ATGT` is preferred.
/// If `tgt_reg` is `ATGT`, returns the used register. If the returned register
/// is a temporarily allocated one, then it shall be negated, and the caller shall free it.
typedef signed int (*codegen_node_handler_t) \
    (struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg);

#define E(NAME, FIELD_LIST) \
    static int emit_##NAME(struct zis_codegen *, struct zis_ast_node_obj *, unsigned int);
    ZIS_AST_NODE_LIST
#undef E

static const codegen_node_handler_t
codegen_node_handlers[(unsigned int)_ZIS_AST_NODE_TYPE_COUNT] = {
#define E(NAME, FIELD_LIST) [(unsigned int)ZIS_AST_NODE_##NAME] = emit_##NAME,
    ZIS_AST_NODE_LIST
#undef E
};

/// Handle a node of any type.
static int emit_any(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    unsigned int node_type_index = (unsigned int)zis_ast_node_obj_type(node);
    assert(node_type_index < (unsigned int)_ZIS_AST_NODE_TYPE_COUNT);
    return codegen_node_handlers[node_type_index](cg, node, tgt_reg);
}

/// Handle a unary operator node.
static int emit_un_op_node(
    struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg,
    enum zis_opcode opcode
) {
    struct zis_ast_node_Pos_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Pos_data);
    if (zis_unlikely(tgt_reg == NTGT)) {
        if (node_is_constant(_node_data->value))
            return 0;
        tgt_reg = 0;
    }
    int atgt;
    const int value_atgt = emit_any(cg, _node_data->value, ATGT);
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    atgt_free1(fs, value_atgt);
    if (tgt_reg == ATGT)
        tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
    else
        atgt = 0;
    struct zis_assembler *const as = scope_assembler(cg);
    zis_assembler_append_ABw(as, opcode, tgt_reg, atgt_abs(value_atgt));
    return atgt;
}

/// Handle a binary operator node.
static int emit_bin_op_node(
    struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg,
    enum zis_opcode opcode
) {
    struct zis_ast_node_Add_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Add_data);
    if (zis_unlikely(tgt_reg == NTGT)) {
        if (node_is_constant(_node_data->lhs) && node_is_constant(_node_data->rhs))
            return 0;
        tgt_reg = 0;
    }
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *lhs, *rhs;
    );
    var.lhs = _node_data->lhs, var.rhs = _node_data->rhs;
    const int lhs_atgt = emit_any(cg, var.lhs, ATGT);
    const int rhs_atgt = emit_any(cg, var.rhs, ATGT);
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    atgt_free1(fs, rhs_atgt);
    atgt_free1(fs, lhs_atgt);
    if (tgt_reg == ATGT)
        tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
    else
        atgt = 0;
    struct zis_assembler *const as = scope_assembler(cg);
    zis_assembler_append_ABC(as, opcode, tgt_reg, atgt_abs(lhs_atgt), atgt_abs(rhs_atgt));
    zis_locals_drop(cg, var);
    return atgt;
}

/// Handle a vector of elements.
static struct zis_ast_node_obj *emit_elements(
    struct zis_codegen *cg,
    struct zis_ast_node_obj *_node, struct zis_array_obj *_elements,
    unsigned int regs_start
) {
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *node;
        struct zis_array_obj *elements;
    );
    var.node = _node, var.elements = _elements;
    for (unsigned int i = 0; ; i++) {
        struct zis_object *sub_node = zis_array_obj_get_checked(var.elements, i);
        if (!sub_node)
            break;
        check_obj_is_node(cg, var.node, sub_node);
        emit_any(cg, zis_object_cast(sub_node, struct zis_ast_node_obj), regs_start + i);
    }
    zis_locals_drop(cg, var);
    return var.node;
}

/// Handle a list-like node (Tuple, Array, Map).
static int emit_list_like_node(
    struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg,
    enum zis_opcode opcode
) {
    struct zis_ast_node_Tuple_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Tuple_data);
    if (zis_unlikely(tgt_reg == NTGT))
        tgt_reg = 0;
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_array_obj *args;
        struct zis_ast_node_obj *node;
    );
    var.args = _node_data->args, var.node = _node;
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    struct zis_assembler *const as = scope_assembler(cg);
    const size_t elem_count = zis_array_obj_length(var.args);
    if (elem_count < 32) {
        unsigned int elem_regs_start;
        if (elem_count == 0) {
            elem_regs_start = 0;
        } else {
            assert(elem_count <= UINT_MAX);
            elem_regs_start = frame_scope_alloc_regs(fs, (unsigned int)elem_count);
            emit_elements(cg, var.node, var.args, elem_regs_start);
            frame_scope_free_regs(fs, elem_regs_start, (unsigned int)elem_count);
        }
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        const unsigned int operand_count =
            (unsigned int)(opcode != ZIS_OPC_MKMAP ? elem_count : elem_count / 2);
        zis_assembler_append_ABC(as, opcode, tgt_reg, elem_regs_start, operand_count);
    } else {
        error_not_implemented(cg, __func__, _node);
        // TODO: handle large lists.
    }
    zis_locals_drop(cg, var);
    return atgt;
}

/// Handle a Call-like node (Call, Send).
static int emit_call_node(
    struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg,
    struct zis_array_obj *_args, struct zis_object *_func_or_meth /* func: Node; meth: Symbol */
) {
    bool is_send_node;
    {
        const enum zis_ast_node_type node_type = zis_ast_node_obj_type(_node);
        assert(node_type == ZIS_AST_NODE_Call || node_type == ZIS_AST_NODE_Send);
        is_send_node = node_type == ZIS_AST_NODE_Send;
    }
    if (zis_unlikely(tgt_reg == NTGT))
        tgt_reg = 0;
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_array_obj *args;
        struct zis_object *func_or_meth;
        struct zis_ast_node_obj *node;
    );
    var.args = _args, var.func_or_meth = _func_or_meth, var.node = _node;
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    struct zis_assembler *const as = scope_assembler(cg);
    const unsigned int argc = (unsigned int)zis_array_obj_length(var.args);
    if (argc <= 3 && fs->reg_allocated_max + 3 < 63) {
        int arg_atgt_list[3];
        unsigned int operand_args = argc << 18;
        for (unsigned int i = 0; i < argc; i++) {
            struct zis_object *arg = zis_array_obj_get(var.args, i);
            check_obj_is_node(cg, var.node, arg);
            const int arg_atgt =
                emit_any(cg, zis_object_cast(arg, struct zis_ast_node_obj), ATGT);
            arg_atgt_list[i] = arg_atgt;
            operand_args |= (atgt_abs(arg_atgt) & 63) << (6 * i);
        }
        if (!is_send_node) {
            check_obj_is_node(cg, var.node, var.func_or_meth);
            emit_any(cg, zis_object_cast(var.func_or_meth, struct zis_ast_node_obj), 0);
        } else {
            assert(zis_object_type_is(var.func_or_meth, codegen_z(cg)->globals->type_Symbol));
            const unsigned int method_name_sym = zis_assembler_func_symbol(
                as, codegen_z(cg),
                zis_object_cast(var.func_or_meth, struct zis_symbol_obj)
            );
            assert(argc >= 1);
            zis_assembler_append_ABw(as, ZIS_OPC_LDMTH, atgt_abs(arg_atgt_list[0]), method_name_sym);
        }
        for (unsigned int i = 0; i < argc; i++)
            atgt_free1(fs, arg_atgt_list[i]);
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        if (tgt_reg <= 31) {
            zis_assembler_append_Aw(as, ZIS_OPC_CALL, operand_args | (tgt_reg << 20));
        } else {
            zis_assembler_append_Aw(as, ZIS_OPC_CALL, operand_args);
            zis_assembler_append_ABw(as, ZIS_OPC_STLOC, 0, tgt_reg);
        }
    } else if (argc < 64) {
        const unsigned int arg_regs_start = frame_scope_alloc_regs(fs, argc);
        emit_elements(cg, var.node, var.args, arg_regs_start);
        if (!is_send_node) {
            check_obj_is_node(cg, var.node, var.func_or_meth);
            emit_any(cg, zis_object_cast(var.func_or_meth, struct zis_ast_node_obj), 0);
        } else {
            assert(zis_object_type_is(var.func_or_meth, codegen_z(cg)->globals->type_Symbol));
            const unsigned int method_name_sym = zis_assembler_func_symbol(
                as, codegen_z(cg),
                zis_object_cast(var.func_or_meth, struct zis_symbol_obj)
            );
            zis_assembler_append_ABw(as, ZIS_OPC_LDMTH, arg_regs_start, method_name_sym);
        }
        frame_scope_free_regs(fs, arg_regs_start, argc);
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        zis_assembler_append_ABC(as, ZIS_OPC_CALLV, tgt_reg, arg_regs_start, argc);
    } else {
        error_not_implemented(cg, __func__, var.node);
        // TODO: handle large number of arguments.
    }
    zis_locals_drop(cg, var);
    return atgt;
}

/// Handle test-and-jump code (if `cond_node` == `jump_when` then goto `jump_to_label`).
/// If the `cond_node` is true, does not emit code and returns 1; if the `cond_node`
/// is false, does no emit code and returns -1; otherise emit code and returns 0.
zis_nodiscard static int emit_branch(
    struct zis_codegen *cg, struct zis_ast_node_obj *cond_node,
    bool jump_when, int jump_to_label
) {
    {
        const int x = check_node_maybe_bool(cg, cond_node);
        if (x)
            return x;
    }
    struct zis_assembler *const as = scope_assembler(cg);
    const int atgt = emit_any(cg, cond_node, ATGT); // TODO: more efficient test and jump.
    atgt_free1(scope_stack_last_frame_scope(&cg->scope_stack), atgt);
    const enum zis_opcode opcode = jump_when ? ZIS_OPC_JMPT : ZIS_OPC_JMPF;
    zis_assembler_append_jump_AsBw(as, opcode, jump_to_label, atgt_abs(atgt));
    return 0;
}

/// Handle a block (an array of nodes).
static struct zis_ast_node_obj *emit_block(
    struct zis_codegen *cg,
    struct zis_ast_node_obj *_node, struct zis_array_obj *_block
) {
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *node;
        struct zis_array_obj *block;
    );
    var.node = _node, var.block = _block;
    for (size_t i = 0; ; i++) {
        struct zis_object *sub_node = zis_array_obj_get_checked(var.block, i);
        if (!sub_node)
            break;
        check_obj_is_node(cg, var.node, sub_node);
        emit_any(cg, zis_object_cast(sub_node, struct zis_ast_node_obj), NTGT);
    }
    zis_locals_drop(cg, var);
    return var.node;
}

static int emit_Nil(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(node);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Nil);
    if (zis_unlikely(tgt_reg == NTGT))
        return 0;
    int atgt;
    if (tgt_reg == ATGT)
        tgt_reg = scope_alloc_regs(cg, 1), atgt = -(int)tgt_reg;
    else
        atgt = 0;
    struct zis_assembler *const as = scope_assembler(cg);
    zis_assembler_append_ABw(as, ZIS_OPC_LDNIL, tgt_reg, 1);
    return atgt;
}

static int emit_Bool(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Bool);
    if (zis_unlikely(tgt_reg == NTGT))
        return 0;
    int atgt;
    if (tgt_reg == ATGT)
        tgt_reg = scope_alloc_regs(cg, 1), atgt = -(int)tgt_reg;
    else
        atgt = 0;
    struct zis_assembler *const as = scope_assembler(cg);
    struct zis_bool_obj *true_v = codegen_z(cg)->globals->val_true;
    const bool x = zis_ast_node_get_field(node, Bool, value) == true_v;
    zis_assembler_append_ABw(as, ZIS_OPC_LDBLN, tgt_reg, x ? 1 : 0);
    return atgt;
}

static int emit_Constant(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Constant);
    if (zis_unlikely(tgt_reg == NTGT))
        return 0;
    int atgt;
    if (tgt_reg == ATGT)
        tgt_reg = scope_alloc_regs(cg, 1), atgt = -(int)tgt_reg;
    else
        atgt = 0;
    struct zis_assembler *const as = scope_assembler(cg);
    struct zis_object *v = zis_ast_node_get_field(node, Constant, value);
    if (zis_object_is_smallint(v)) {
        const zis_smallint_t x = zis_smallint_from_ptr(v);
        if (ZIS_INSTR_I16_MIN <= x && x <= ZIS_INSTR_I16_MAX) {
            zis_assembler_append_ABsw(as, ZIS_OPC_MKINT, tgt_reg, (int32_t)x);
            return atgt;
        }
    } else if (zis_object_type(v) == codegen_z(cg)->globals->type_Float) {
        const double x = zis_float_obj_value(zis_object_cast(v, struct zis_float_obj));
        double frac; int exp; // x = frac * pow2(exp)
        frac = frexp(x, &exp); // frexp(~, ~) \in (-1,-0.5] \cup [0.5,1)
        if (frac != 0.0) {
            frac = frac * 128; // \in (-128,-64] \cup [64,128)
            exp  = exp - 7;
        }
        if (
            (trunc(frac) == frac) &&
            (ZIS_INSTR_I8_MIN <= exp && exp <= ZIS_INSTR_I8_MAX)
        ) {
            assert(ZIS_INSTR_I8_MIN <= frac && frac <= ZIS_INSTR_I8_MAX);
            zis_assembler_append_ABsCs(as, ZIS_OPC_MKFLT, tgt_reg, (int)frac, exp);
            return atgt;
        }
    }
    const unsigned int cid = zis_assembler_func_constant(as, codegen_z(cg), v);
    zis_assembler_append_ABw(as, ZIS_OPC_LDCON, tgt_reg, cid);
    return atgt;
}

static int emit_Name(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Name);
    if (zis_unlikely(tgt_reg == NTGT))
        return 0;
    int atgt;
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    struct zis_assembler *const as = scope_assembler(cg);
    struct zis_symbol_obj *name = zis_ast_node_get_field(node, Name, value);
    const unsigned int var_reg = frame_scope_find_var(fs, name);
    if (var_reg) {
        if (tgt_reg == ATGT) {
            atgt = (int)var_reg;
        } else {
            atgt = 0;
            zis_assembler_append_ABw(as, ZIS_OPC_LDLOC, tgt_reg, var_reg);
        }
    } else {
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        const unsigned int yid = zis_assembler_func_symbol(as, codegen_z(cg), name);
        zis_assembler_append_ABw(as, ZIS_OPC_LDGLB, tgt_reg, yid);
    }
    return atgt;
}

static int emit_Pos(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(cg), zis_unused_var(node), zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Pos);
    error_not_implemented(cg, __func__, node);
    // TODO: add an instruction to support this kind of node.
}

static int emit_Neg(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Neg);
    return emit_un_op_node(cg, node, tgt_reg, ZIS_OPC_NEG);
}

static int emit_BitNot(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitNot);
    return emit_un_op_node(cg, node, tgt_reg, ZIS_OPC_BITNOT);
}

static int emit_Not(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Not);
    return emit_un_op_node(cg, node, tgt_reg, ZIS_OPC_NOT);
}

static int emit_Add(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Add);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_ADD);
}

static int emit_Sub(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Sub);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_SUB);
}

static int emit_Mul(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Mul);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_MUL);
}

static int emit_Div(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Div);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_DIV);
}

static int emit_Rem(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Rem);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_REM);
}

static int emit_Shl(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Shl);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_SHL);
}

static int emit_Shr(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Shr);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_SHR);
}

static int emit_BitAnd(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitAnd);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_BITAND);
}

static int emit_BitOr(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitOr);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_BITOR);
}

static int emit_BitXor(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_BitXor);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_BITXOR);
}

static int emit_Pow(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Pow);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_POW);
}

static int emit_Assign(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Assign);
    struct zis_ast_node_Assign_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Assign_data);
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *lhs, *rhs;
    );
    var.lhs = _node_data->lhs, var.rhs = _node_data->rhs;
    const enum zis_ast_node_type lhs_type = zis_ast_node_obj_type(var.lhs);
    struct zis_assembler *const as = scope_assembler(cg);
    if (lhs_type == ZIS_AST_NODE_Name) {
        unsigned int var_reg = scope_find_or_alloc_var(
            cg, codegen_z(cg),
            zis_ast_node_get_field(var.lhs, Name, value)
        );
        if (var_reg) {
            emit_any(cg, var.rhs, var_reg);
        } else {
            const bool rhs_atgt_valid =
                zis_ast_node_obj_type(var.rhs) == ZIS_AST_NODE_Name || tgt_reg == ATGT;
            const int rhs_atgt = emit_any(cg, var.rhs, rhs_atgt_valid ? ATGT : 0);
            if (rhs_atgt_valid) {
                atgt_free1(scope_stack_last_frame_scope(&cg->scope_stack), rhs_atgt);
                var_reg = atgt_abs(rhs_atgt);
            }
            const unsigned int name_cid = zis_assembler_func_symbol(
                as, codegen_z(cg),
                zis_ast_node_get_field(var.lhs, Name, value)
            );
            zis_assembler_append_ABw(as, ZIS_OPC_STGLB, var_reg, name_cid);
        }
        if (tgt_reg == NTGT) {
            atgt = 0;
        } else if (tgt_reg == ATGT) {
            atgt = (int)var_reg;
        } else {
            atgt = 0;
            zis_assembler_append_ABw(as, ZIS_OPC_LDLOC, tgt_reg, var_reg);
        }
    } else {
        int rhs_atgt; unsigned int rhs_reg;
        const bool tgt_reg_is_normal = tgt_reg != NTGT && tgt_reg != ATGT;
        if (!tgt_reg_is_normal) {
            rhs_atgt = emit_any(cg, var.rhs, ATGT);
            rhs_reg = atgt_abs(rhs_atgt);
        } else {
            rhs_atgt = 0, rhs_reg = tgt_reg;
            emit_any(cg, var.rhs, tgt_reg);
        }
        struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);

        if (lhs_type == ZIS_AST_NODE_Field) {
            const int value_atgt =
                emit_any(cg, zis_ast_node_get_field(var.lhs, Field, value), ATGT);
            const unsigned int name_sid =
                zis_assembler_func_symbol(as, codegen_z(cg), zis_ast_node_get_field(var.lhs, Field, name));
            atgt_free1(fs, value_atgt);
            zis_assembler_append_ABC(as, ZIS_OPC_STFLDY, name_sid, rhs_reg, atgt_abs(value_atgt));
        } else if (lhs_type == ZIS_AST_NODE_Subscript) {
            const int value_atgt =
                emit_any(cg, zis_ast_node_get_field(var.lhs, Subscript, value), ATGT);
            struct zis_ast_node_obj *_subs_key_node = zis_ast_node_get_field(var.lhs, Subscript, key);
            if (node_is_constant(_subs_key_node)) {
                do {
                    if (zis_ast_node_obj_type(_subs_key_node) == ZIS_AST_NODE_Constant) {
                        struct zis_object *key = zis_ast_node_get_field(_subs_key_node, Constant, value);
                        if (zis_object_is_smallint(key)) {
                            const zis_smallint_t key_smi = zis_smallint_from_ptr(key);
                            if (ZIS_INSTR_I9_MIN <= key_smi && key_smi <= ZIS_INSTR_I9_MAX) {
                                zis_assembler_append_AsBC(
                                    as, ZIS_OPC_STELMI,
                                    (int32_t)key_smi, rhs_reg, atgt_abs(value_atgt)
                                );
                                break;
                            }
                        }
                    }
                    emit_any(cg, _subs_key_node, 0);
                    zis_assembler_append_ABC(as, ZIS_OPC_STELM, 0, rhs_reg, atgt_abs(value_atgt));
                } while (false);
            } else {
                const int key_atgt =
                    emit_any(cg, zis_ast_node_get_field(var.lhs, Subscript, key), ATGT);
                zis_assembler_append_ABC(as, ZIS_OPC_STELM, atgt_abs(key_atgt), rhs_reg, atgt_abs(value_atgt));
                atgt_free1(fs, key_atgt);
            }
            atgt_free1(fs, value_atgt);
        } else {
            const char *const lhs_type_name = zis_ast_node_type_represent(lhs_type);
            error(cg, var.lhs, "cannot assign to <%s>", lhs_type_name);
        }

        if (tgt_reg == NTGT)
            atgt_free1(fs, rhs_atgt), atgt = 0;
        else if (tgt_reg == ATGT)
            atgt = rhs_atgt;
        else
            atgt = 0;
    }
    zis_locals_drop(cg, var);
    return atgt;
}

static int emit_Eq(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Eq);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMPEQ);
}

static int emit_Ne(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Ne);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMPNE);
}

static int emit_Lt(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Lt);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMPLT);
}

static int emit_Le(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Le);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMPLE);
}

static int emit_Gt(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Gt);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMPGT);
}

static int emit_Ge(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Ge);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMPGE);
}

static int emit_Cmp(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Cmp);
    return emit_bin_op_node(cg, node, tgt_reg, ZIS_OPC_CMP);
}

static int emit_And(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_And);
    struct zis_ast_node_And_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_And_data);
    if (zis_unlikely(tgt_reg == NTGT)) {
        if (node_is_constant(_node_data->lhs) && node_is_constant(_node_data->rhs))
            return 0;
        tgt_reg = 0;
    }
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *lhs, *rhs;
    );
    var.lhs = _node_data->lhs, var.rhs = _node_data->rhs;
    const int lhs_bx = check_node_maybe_bool(cg, var.lhs);
    check_node_maybe_bool(cg, var.rhs);
    if (lhs_bx == 0) {
        struct zis_assembler *const as = scope_assembler(cg);
        struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
        const int label1 = zis_assembler_alloc_label(as);
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        atgt = emit_any(cg, var.lhs, tgt_reg);
        zis_assembler_append_jump_AsBw(as, ZIS_OPC_JMPF, label1, tgt_reg);
        emit_any(cg, var.rhs, tgt_reg);
        zis_assembler_place_label(as, label1);
    } else {
        atgt = emit_any(cg, lhs_bx == 1 ? var.rhs : var.lhs, tgt_reg);
    }
    zis_locals_drop(cg, var);
    return atgt;
}

static int emit_Or(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Or);
    struct zis_ast_node_Or_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Or_data);
    if (zis_unlikely(tgt_reg == NTGT)) {
        if (node_is_constant(_node_data->lhs) && node_is_constant(_node_data->rhs))
            return 0;
        tgt_reg = 0;
    }
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *lhs, *rhs;
    );
    var.lhs = _node_data->lhs, var.rhs = _node_data->rhs;
    const int lhs_bx = check_node_maybe_bool(cg, var.lhs);
    check_node_maybe_bool(cg, var.rhs);
    if (lhs_bx == 0) {
        struct zis_assembler *const as = scope_assembler(cg);
        struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
        const int label1 = zis_assembler_alloc_label(as);
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        atgt = emit_any(cg, var.lhs, tgt_reg);
        zis_assembler_append_jump_AsBw(as, ZIS_OPC_JMPT, label1, tgt_reg);
        emit_any(cg, var.rhs, tgt_reg);
        zis_assembler_place_label(as, label1);
    } else {
        atgt = emit_any(cg, lhs_bx == -1 ? var.rhs : var.lhs, tgt_reg);
    }
    zis_locals_drop(cg, var);
    return atgt;
}

static int emit_Subscript(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Subscript);
    struct zis_ast_node_Subscript_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Subscript_data);
    if (zis_unlikely(tgt_reg == NTGT))
        tgt_reg = 0;
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *value;
        struct zis_ast_node_obj *key;
    );
    var.value = _node_data->value, var.key = _node_data->key;
    struct zis_assembler *const as = scope_assembler(cg);
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    const int value_atgt = emit_any(cg, var.value, ATGT);
    if (node_is_constant(var.key)) {
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        do {
            if (zis_ast_node_obj_type(var.key) == ZIS_AST_NODE_Constant) {
                struct zis_object *key = zis_ast_node_get_field(var.key, Constant, value);
                if (zis_object_is_smallint(key)) {
                    const zis_smallint_t key_smi = zis_smallint_from_ptr(key);
                    if (ZIS_INSTR_I9_MIN <= key_smi && key_smi <= ZIS_INSTR_I9_MAX) {
                        zis_assembler_append_AsBC(
                            as, ZIS_OPC_LDELMI,
                            (int32_t)key_smi, tgt_reg, atgt_abs(value_atgt)
                        );
                        break;
                    }
                }
            }
            emit_any(cg, var.key, 0);
            zis_assembler_append_ABC(as, ZIS_OPC_LDELM, 0, tgt_reg, atgt_abs(value_atgt));
        } while (false);
    } else {
        const int key_atgt = emit_any(cg, var.key, ATGT);
        atgt_free1(fs, key_atgt);
        if (tgt_reg == ATGT)
            tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
        else
            atgt = 0;
        zis_assembler_append_ABC(as, ZIS_OPC_LDELM, atgt_abs(key_atgt), tgt_reg, atgt_abs(value_atgt));
    }
    atgt_free1(fs, value_atgt);
    zis_locals_drop(cg, var);
    return atgt;
}

static int emit_Field(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Field);
    struct zis_ast_node_Field_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Field_data);
    if (zis_unlikely(tgt_reg == NTGT))
        tgt_reg = 0;
    int atgt;
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *value;
        struct zis_symbol_obj *name;
    );
    var.value = _node_data->value, var.name = _node_data->name;
    struct zis_assembler *const as = scope_assembler(cg);
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    const int value_atgt = emit_any(cg, var.value, ATGT);
    const unsigned int name_sid = zis_assembler_func_symbol(as, codegen_z(cg), var.name);
    atgt_free1(fs, value_atgt);
    if (tgt_reg == ATGT)
        tgt_reg = frame_scope_alloc_regs(fs, 1), atgt = -(int)tgt_reg;
    else
        atgt = 0;
    zis_assembler_append_ABC(as, ZIS_OPC_LDFLDY, name_sid, tgt_reg, atgt_abs(value_atgt));
    zis_locals_drop(cg, var);
    return atgt;
}

static int emit_Call(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Call);
    struct zis_ast_node_Call_data *node_data =
        _zis_ast_node_obj_data_as(node, struct zis_ast_node_Call_data);
    return emit_call_node(cg, node, tgt_reg, node_data->args, zis_object_from(node_data->value));
}

static int emit_Send(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Send);
    struct zis_ast_node_Send_data *node_data =
        _zis_ast_node_obj_data_as(node, struct zis_ast_node_Send_data);
    return emit_call_node(cg, node, tgt_reg, node_data->args, zis_object_from(node_data->method));
}

static int emit_Tuple(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Tuple);
    return emit_list_like_node(cg, node, tgt_reg, ZIS_OPC_MKTUP);
}

static int emit_Array(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Array);
    return emit_list_like_node(cg, node, tgt_reg, ZIS_OPC_MKARR);
}

static int emit_Map(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Map);
    return emit_list_like_node(cg, node, tgt_reg, ZIS_OPC_MKMAP);
}

static int emit_Import(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Import);
    check_tgt_is_ntgt(cg, _node, tgt_reg);
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *what;
    );
    var.what = zis_ast_node_get_field(_node, Import, value);
    const enum zis_ast_node_type what_node_type = zis_ast_node_obj_type(var.what);
    struct zis_assembler *const as = scope_assembler(cg);
    if (what_node_type == ZIS_AST_NODE_Name) {
        const unsigned int name_sid =
            zis_assembler_func_symbol(as, codegen_z(cg), zis_ast_node_get_field(var.what, Name, value));
        const unsigned int value_reg =
            scope_find_or_alloc_var(cg, codegen_z(cg), zis_ast_node_get_field(var.what, Name, value));
        zis_assembler_append_ABw(as, ZIS_OPC_IMP, value_reg, name_sid);
        if (value_reg == 0)
            zis_assembler_append_ABw(as, ZIS_OPC_STGLB, 0, name_sid);
    } else {
        error_not_implemented(cg, __func__, var.what);
        // TODO: complex import statement.
    }
    zis_locals_drop(cg, var);
    return 0;
}

static int emit_Return(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Return);
    check_tgt_is_ntgt(cg, node, tgt_reg);
    struct frame_scope *fs = scope_stack_last_frame_scope(&cg->scope_stack);
    if (scope_frame_is_toplevel(cg, fs))
        error_outside_xxx(cg, node, "function");
    struct zis_assembler *const as = scope_assembler(cg);
    struct zis_object *value = zis_ast_node_get_field(node, Return, value);
    if (value == zis_object_from(codegen_z(cg)->globals->val_nil)) {
        zis_assembler_append_Aw(as, ZIS_OPC_RETNIL, 0);
    } else {
        assert(zis_object_type_is(value, codegen_z(cg)->globals->type_AstNode));
        struct zis_ast_node_obj *value_node = zis_object_cast(value, struct zis_ast_node_obj);
        unsigned int value_reg;
        if (node_is_constant(value_node)) {
            emit_any(cg, value_node, 0);
            value_reg = 0;
        } else {
            const int value_atgt = emit_any(cg, value_node, ATGT);
            atgt_free1(fs, value_atgt);
            value_reg = atgt_abs(value_atgt);
        }
        zis_assembler_append_Aw(as, ZIS_OPC_RET, value_reg);
    }
    return 0;
}

static int emit_Throw(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Throw);
    check_tgt_is_ntgt(cg, node, tgt_reg);
    struct zis_assembler *const as = scope_assembler(cg);
    struct zis_object *value = zis_ast_node_get_field(node, Throw, value);
    if (value == zis_object_from(codegen_z(cg)->globals->val_nil)) {
        error_not_implemented(cg, __func__, node);
        // TODO: Throw statement without argument.
    } else {
        assert(zis_object_type_is(value, codegen_z(cg)->globals->type_AstNode));
        struct zis_ast_node_obj *value_node = zis_object_cast(value, struct zis_ast_node_obj);
        unsigned int value_reg;
        if (node_is_constant(value_node)) {
            emit_any(cg, value_node, 0);
            value_reg = 0;
        } else {
            const int value_atgt = emit_any(cg, value_node, ATGT);
            atgt_free1(scope_stack_last_frame_scope(&cg->scope_stack), value_atgt);
            value_reg = atgt_abs(value_atgt);
        }
        zis_assembler_append_Aw(as, ZIS_OPC_THR, value_reg);
    }
    return 0;
}

static int emit_Break(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(node);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Break);
    check_tgt_is_ntgt(cg, node, tgt_reg);
    struct loop_scope *ls = scope_stack_last_loop_scope(&cg->scope_stack);
    if (!ls)
        error_outside_xxx(cg, node, "loop");
    struct zis_assembler *const as = scope_assembler(cg);
    zis_assembler_append_jump_Asw(as, ZIS_OPC_JMP, ls->label_break);
    return 0;
}

static int emit_Continue(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(node);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Continue);
    check_tgt_is_ntgt(cg, node, tgt_reg);
        struct loop_scope *ls = scope_stack_last_loop_scope(&cg->scope_stack);
    if (!ls)
        error_outside_xxx(cg, node, "loop");
    struct zis_assembler *const as = scope_assembler(cg);
    zis_assembler_append_jump_Asw(as, ZIS_OPC_JMP, ls->label_continue);
    return 0;
}

static int emit_Cond(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Cond);
    check_tgt_is_ntgt(cg, _node, tgt_reg);
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *node;
        struct zis_array_obj *args;
        struct zis_ast_node_obj *branch_cond;
        struct zis_array_obj *branch_body;
    );
    zis_locals_zero(var);
    var.node = _node, var.args = zis_ast_node_get_field(_node, Cond, args);
    int label_next_branch, label_end;
    struct zis_assembler *const as = scope_assembler(cg);
    label_next_branch = zis_assembler_alloc_label(as);
    label_end = zis_assembler_alloc_label(as);
    for (size_t i = 0, n = zis_array_obj_length(var.args); i < n ; i += 2) {
        {
            struct zis_object *x0 = zis_array_obj_get_checked(var.args, i);
            assert(x0);
            struct zis_object *x1 = zis_array_obj_get_checked(var.args, i + 1);
            if (zis_unlikely(!x1)) {
                error(
                    cg, var.node, "illegal <%s> node args (%zu): %s",
                    zis_ast_node_type_represent(ZIS_AST_NODE_Cond), i + 2, "missing"
                );
            }
            check_obj_is_node(cg, var.node, x0);
            var.branch_cond = zis_object_cast(x0, struct zis_ast_node_obj);
            if (zis_unlikely(!zis_object_type_is(x1, codegen_z(cg)->globals->type_Array))) {
                error(
                    cg, var.node, "illegal <%s> node args (%zu): %s",
                    zis_ast_node_type_represent(ZIS_AST_NODE_Cond), i + 2, "not an Array"
                );
            }
            var.branch_body = zis_object_cast(x1, struct zis_array_obj);
        }
        zis_assembler_place_label(as, label_next_branch);
        label_next_branch = zis_assembler_alloc_label(as);
        const int bx = emit_branch(cg, var.branch_cond, false, label_next_branch);
        if (bx >= 0) {
            emit_block(cg, var.node, var.branch_body);
            if (i + 2 < n) // the last branch does not need a trailing jump.
                zis_assembler_append_jump_Asw(as, ZIS_OPC_JMP, label_end);
        }
    }
    zis_assembler_place_label(as, label_next_branch);
    zis_assembler_place_label(as, label_end);
    zis_locals_drop(cg, var);
    return 0;
}

static int emit_While(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_While);
    check_tgt_is_ntgt(cg, _node, tgt_reg);
    struct zis_ast_node_While_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_While_data);
    zis_locals_decl(
        cg, var,
        struct zis_ast_node_obj *cond;
        struct zis_array_obj *body;
        struct zis_ast_node_obj *node;
    );
    var.cond = _node_data->cond, var.body = _node_data->body, var.node = _node;
    struct zis_assembler *const as = scope_assembler(cg);
    scope_stack_push_var_scope(&cg->scope_stack);
    struct loop_scope *ls = scope_stack_push_loop_scope(&cg->scope_stack);
    ls->label_continue = zis_assembler_alloc_label(as);
    ls->label_break = zis_assembler_alloc_label(as);
    zis_assembler_place_label(as, ls->label_continue);
    if (emit_branch(cg, var.cond, false, ls->label_break) >= 0) {
        emit_block(cg, var.node, var.body);
        zis_assembler_append_jump_Asw(as, ZIS_OPC_JMP, ls->label_continue);
    }
    zis_assembler_place_label(as, ls->label_break);
    scope_stack_pop_loop_scope(&cg->scope_stack);
    scope_stack_pop_var_scope(&cg->scope_stack, codegen_z(cg));
    zis_locals_drop(cg, var);
    return 0;
}

static int emit_Func(struct zis_codegen *cg, struct zis_ast_node_obj *_node, unsigned int tgt_reg) {
    assert(zis_ast_node_obj_type(_node) == ZIS_AST_NODE_Func);
    check_tgt_is_ntgt(cg, _node, tgt_reg);
    struct zis_ast_node_Func_data *_node_data =
        _zis_ast_node_obj_data_as(_node, struct zis_ast_node_Func_data);
    zis_locals_decl(
        cg, var,
        struct zis_symbol_obj *name;
        struct zis_array_obj *args, *body;
        struct zis_ast_node_obj *node;
    );
    var.name = _node_data->name, var.args = _node_data->args, var.body = _node_data->body;
    var.node = _node;

    struct frame_scope *fs = scope_stack_push_frame_scope(&cg->scope_stack, codegen_z(cg));
    if (!frame_scope_set_argc(fs, zis_array_obj_length(var.args), 0))
        error_too_many_args(cg, var.node);
    struct zis_type_obj *type_sym = codegen_z(cg)->globals->type_Symbol;
    for (size_t i = 0; ; i++) {
        struct zis_object *arg_decl = zis_array_obj_get_checked(var.args, i);
        if (!arg_decl)
            break;
        if (!zis_object_type_is(arg_decl, type_sym)) // TODO: support optional arguments.
            error(cg, var.node, "formal argument is not symbol");
        struct zis_symbol_obj *arg_name = zis_object_cast(arg_decl, struct zis_symbol_obj);
        if (frame_scope_find_var(fs, arg_name)) {
            const char *s = zis_symbol_obj_data(arg_name);
            const size_t n = zis_symbol_obj_data_size(arg_name);
            error(cg, var.node, "duplicate argument `%.*s'", (int)n, s);
        }
        frame_scope_alloc_var(fs, codegen_z(cg), arg_name);
    }
    emit_block(cg, var.node, var.body);
    assert(scope_stack_current_scope(&cg->scope_stack).any->type == SCOPE_FRAME);
    struct zis_func_obj *const result = frame_scope_gen_func(fs, codegen_z(cg), cg->module);
    if (!result)
        error_too_many_regs(cg, var.node);
    scope_stack_pop_frame_scope(&cg->scope_stack);

    struct zis_assembler *const as = scope_assembler(cg);
    const unsigned int func_cid =
        zis_assembler_func_constant(as, codegen_z(cg), zis_object_from(result));
    const unsigned int name_reg =
        scope_find_or_alloc_var(cg, codegen_z(cg), var.name);
    zis_assembler_append_ABw(as, ZIS_OPC_LDCON, name_reg, func_cid);
    if (name_reg == 0) {
        const unsigned int name_sid = zis_assembler_func_symbol(as, codegen_z(cg), var.name);
        zis_assembler_append_ABw(as, ZIS_OPC_STGLB, 0, name_sid);
    }

    zis_locals_drop(cg, var);
    return 0;
}

static int emit_Module(struct zis_codegen *cg, struct zis_ast_node_obj *node, unsigned int tgt_reg) {
    zis_unused_var(tgt_reg);
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Module);
    error(cg, node, "nested module");
}

/// Generate bytecode from a module. Should be used instead of `emit_Module()`.
static struct zis_func_obj *gen_module(struct zis_codegen *cg, struct zis_ast_node_obj *node) {
    assert(zis_ast_node_obj_type(node) == ZIS_AST_NODE_Module);
    scope_stack_push_frame_scope(&cg->scope_stack, codegen_z(cg));
    assert(scope_stack_current_scope(&cg->scope_stack).any->type == SCOPE_FRAME);
    if (!frame_scope_set_argc(scope_stack_current_scope(&cg->scope_stack).frame, 0, 0))
        zis_unreachable();
    node = emit_block(cg, node, zis_ast_node_get_field(node, Module, body));
    assert(scope_stack_current_scope(&cg->scope_stack).any->type == SCOPE_FRAME);
    struct zis_func_obj *const result =
        frame_scope_gen_func(scope_stack_current_scope(&cg->scope_stack).frame, codegen_z(cg), cg->module);
    if (!result)
        error_too_many_regs(cg, node);
    scope_stack_pop_frame_scope(&cg->scope_stack);
    return result;
}

/* ----- public functions --------------------------------------------------- */

struct zis_codegen *zis_codegen_create(struct zis_context *z) {
    struct zis_codegen *cg = zis_mem_alloc(sizeof(struct zis_codegen));
    zis_locals_root_init(&cg->locals_root, NULL);
    scope_stack_init(&cg->scope_stack);
    cg->z = z;
    cg->module = z->globals->val_mod_unnamed;
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
    struct zis_ast_node_obj *ast,
    struct zis_module_obj *_module
) {
    if (_module)
        cg->module = _module;

    assert(!cg->locals_root._list);
    struct zis_func_obj *result;
    if (!codegen_error_setjmp(cg)) {
        if (zis_ast_node_obj_type(ast) != ZIS_AST_NODE_Module)
            error(cg, ast, "the toplevel node must be a Module");
        result = gen_module(cg, ast);
    } else {
        zis_locals_root_reset(&cg->locals_root);
        result = NULL;
    }
    assert(!cg->locals_root._list);

    cg->module = cg->z->globals->val_mod_unnamed;

    return result;
}

#endif // ZIS_FEATURE_SRC
