//%% [module]
//%% name = repl
//%% description = An implementation of the read-eval-print loop.
//%% force-embedded = YES

#include <stdio.h>
#include <string.h>

#include <zis.h>

#include <core/arrayobj.h>
#include <core/ast.h>
#include <core/codegen.h>
#include <core/context.h>
#include <core/exceptobj.h>
#include <core/globals.h>
#include <core/locals.h>
#include <core/moduleobj.h>
#include <core/object.h>
#include <core/parser.h>
#include <core/stack.h>
#include <core/streamobj.h>
#include <core/stringobj.h>
#include <core/symbolobj.h>
#include <core/tupleobj.h>

#define REPL_LAST_RESULT_VAR "ans"

static void ensure_var_module(zis_t z) {
    if (
        zis_load_global(z, 0, "module", (size_t)-1) != ZIS_OK ||
        !zis_object_type_is(z->callstack->frame[0], z->globals->type_Module)
    ) {
        zis_context_set_reg0(z, zis_object_from(zis_module_obj_new(z, true)));
        zis_store_global(z, 0, "module", (size_t)-1);
    }
}

static void ensure_var_prompt(zis_t z) {
    if (
        zis_load_global(z, 0, "prompt", (size_t)-1) != ZIS_OK ||
        !zis_object_type_is(z->callstack->frame[0], z->globals->type_Tuple) ||
        zis_tuple_obj_length(zis_object_cast(z->callstack->frame[0], struct zis_tuple_obj)) < 2
    ) {
        zis_make_values(z, 0, "(ss)", ">> ", (size_t)-1, ".. ", (size_t)-1);
        zis_store_global(z, 0, "prompt", (size_t)-1);
    }
}

static const void *_memstr(const void *restrict mem, size_t len, const char *restrict str) {
    const size_t str_len = strlen(str);
    if (str_len > len || !str_len)
        return NULL;
    while (len) {
        void *p = memchr(mem, *str, len);
        if (!p)
            break;
        const size_t rest_len = len - (size_t)((const char *)p - (const char *)mem);
        if (rest_len < str_len)
            return NULL;
        if (memcmp(p, str, str_len) == 0)
            return p;
        mem = (const char *)p + 1;
        len = rest_len - 1;
    }
    return NULL;
}

static bool read_need_next_line(zis_t z, struct zis_object *syntax_err) {
    if (!zis_object_type_is(syntax_err, z->globals->type_Exception))
        return false;
    struct zis_exception_obj *exc_obj =
        zis_object_cast(syntax_err, struct zis_exception_obj);
    if (!zis_object_type_is(exc_obj->what, z->globals->type_String))
        return false;
    struct zis_string_obj *err_msg =
        zis_object_cast(exc_obj->what, struct zis_string_obj);
    char err_msg_str[128];
    size_t err_msg_size = zis_string_obj_to_u8str(err_msg, err_msg_str, sizeof err_msg_str);
    if (err_msg_size == (size_t)-1)
        return false;
    return
        _memstr(err_msg_str, err_msg_size, "before `end-of-source'") ||
        _memstr(err_msg_str, err_msg_size, "unexpected `end-of-source'");
}

// input(line_num :: Int) -> line :: String | Nil
//# Reads a line of string from stdin. On success, returns the String;
//# on failure (like stdin is closed), returns nil.
ZIS_NATIVE_FUNC_DEF(F_input, z, {1, 0, 2}) {
    int64_t line_num;
    zis_read_int(z, 1, &line_num);
    //> if line_num > 1; %1 = prompt[2]; else; %1 = prompt[1]; end
    if (zis_load_global(z, 1, "prompt", (size_t)-1) == ZIS_THR)
        return ZIS_THR;
    zis_make_int(z, 0, line_num > 1 ? 2 : 1);
    if (zis_load_element(z, 1, 0, 1) == ZIS_THR)
        return ZIS_THR;
    //> return prelude.input(%1)
    if (zis_load_global(z, 0, "prelude", (size_t)-1) == ZIS_THR)
        return ZIS_THR;
    if (zis_load_field(z, 0, "input", (size_t)-1, 0) == ZIS_THR)
        return ZIS_THR;
    return zis_invoke(z, (unsigned int []){0, 0, 1}, 1);
}

// read() -> ast :: AstNode | Nil
//# Reads a block of code with function `input()` and parses it to AST.
//# Or returns nil to stop the loop.
ZIS_NATIVE_FUNC_DEF(F_read, z, {0, 0, 2}) {
    struct zis_object **const frame = z->callstack->frame;
    for (int64_t line_num = 1; ; line_num++) {
        //> %1 = input(line_num)
        zis_make_int(z, 1, line_num);
        if (zis_load_global(z, 0, "input", (size_t)-1) == ZIS_THR)
            return ZIS_THR;
        if (zis_invoke(z, (unsigned int []){1, 0, 1}, 1) != ZIS_OK) {
            zis_load_nil(z, 0, 1);
            return ZIS_OK;
        }
        if (!zis_object_type_is(frame[1], z->globals->type_String)) {
            zis_move_local(z, 0, 1);
            return ZIS_OK;
        }
        //> if line_num > 1; %2 = %2 + %1; else; %2 = %1; end
        assert(zis_object_type_is(frame[1], z->globals->type_String));
        if (line_num > 1) {
            assert(zis_object_type_is(frame[2], z->globals->type_String));
            frame[2] = zis_object_from(zis_string_obj_concat(
                z,
                zis_object_cast(frame[2], struct zis_string_obj),
                zis_string_obj_concat(
                    z,
                    zis_string_obj_from_char(z, '\n'),
                    zis_object_cast(frame[1], struct zis_string_obj)
                )
            ));
        } else {
            frame[2] = frame[1];
        }
        // try to parse the code
        struct zis_ast_node_obj *ast;
        {
            struct zis_parser *parser = zis_parser_create(z); // TODO: reuse the parser.
            struct zis_stream_obj *stream =
                zis_stream_obj_new_strob(z, zis_object_cast(frame[2], struct zis_string_obj)); // TODO: reuse the stream.
            ast = zis_parser_parse(parser, stream, ZIS_PARSER_MOD);
            zis_parser_destroy(parser, z);
        }
        if (ast) {
            zis_context_set_reg0(z, zis_object_from(ast));
            return ZIS_OK;
        }
        if (!read_need_next_line(z, frame[0])) {
            zis_make_stream(z, 1, ZIS_IOS_STDX, 1); // stdout
            zis_read_exception(z, 0, ZIS_RDE_DUMP, 1);
            line_num = 0;
        }
    }

    return ZIS_OK;
}

static bool ast_node_is_expr(enum zis_ast_node_type type) {
    return
        type == ZIS_AST_NODE_Nil       ||
        type == ZIS_AST_NODE_Bool      ||
        type == ZIS_AST_NODE_Constant  ||
        type == ZIS_AST_NODE_Name      ||
        type == ZIS_AST_NODE_Pos       ||
        type == ZIS_AST_NODE_Neg       ||
        type == ZIS_AST_NODE_BitNot    ||
        type == ZIS_AST_NODE_Not       ||
        type == ZIS_AST_NODE_Add       ||
        type == ZIS_AST_NODE_Sub       ||
        type == ZIS_AST_NODE_Mul       ||
        type == ZIS_AST_NODE_Div       ||
        type == ZIS_AST_NODE_Rem       ||
        type == ZIS_AST_NODE_Shl       ||
        type == ZIS_AST_NODE_Shr       ||
        type == ZIS_AST_NODE_BitAnd    ||
        type == ZIS_AST_NODE_BitOr     ||
        type == ZIS_AST_NODE_BitXor    ||
        type == ZIS_AST_NODE_Assign    ||
        type == ZIS_AST_NODE_Eq        ||
        type == ZIS_AST_NODE_Ne        ||
        type == ZIS_AST_NODE_Lt        ||
        type == ZIS_AST_NODE_Le        ||
        type == ZIS_AST_NODE_Gt        ||
        type == ZIS_AST_NODE_Ge        ||
        type == ZIS_AST_NODE_Cmp       ||
        type == ZIS_AST_NODE_And       ||
        type == ZIS_AST_NODE_Or        ||
        type == ZIS_AST_NODE_Subscript ||
        type == ZIS_AST_NODE_Field     ||
        type == ZIS_AST_NODE_Call      ||
        type == ZIS_AST_NODE_Send      ||
        type == ZIS_AST_NODE_Tuple     ||
        type == ZIS_AST_NODE_Array     ||
        type == ZIS_AST_NODE_Map       ||
        false;
}

static bool ast_make_last_expr_assignment(
    struct zis_context *z,
    const char *restrict var_name,
    struct zis_ast_node_obj **ast_ref
) {
    bool modified = false;
    zis_locals_decl(
        z, var,
        struct zis_ast_node_obj *ast;
        struct zis_array_obj *mod_body;
        struct zis_ast_node_obj *last_node;
        struct zis_ast_node_obj *name_node;
        struct zis_ast_node_obj *assignment_node;
    );
    zis_locals_zero(var);
    var.ast = *ast_ref;

    do {
        if (zis_ast_node_obj_type(var.ast) != ZIS_AST_NODE_Module)
            break;
        var.mod_body = zis_ast_node_get_field(var.ast, Module, body);
        const size_t mod_body_len = zis_array_obj_length(var.mod_body);
        if (!mod_body_len)
            break;
        struct zis_object *last_node_obj = zis_array_obj_get(var.mod_body, mod_body_len - 1);
        if (!zis_object_type_is(last_node_obj, z->globals->type_AstNode))
            break;
        var.last_node = zis_object_cast(last_node_obj, struct zis_ast_node_obj);
        if (!ast_node_is_expr(zis_ast_node_obj_type(var.last_node)))
            break;

        var.assignment_node = zis_ast_node_new(z, Assign, false);
        zis_array_obj_set(var.mod_body, mod_body_len - 1, zis_object_from(var.assignment_node));
        var.name_node = zis_ast_node_new(z, Name, true);
        zis_ast_node_set_field(var.name_node, Name, value, zis_symbol_registry_get(z, var_name, (size_t)-1));
        zis_ast_node_set_field(var.assignment_node, Assign, lhs, var.name_node);
        zis_ast_node_set_field(var.assignment_node, Assign, rhs, var.last_node);
        modified = true;
    } while (false);

    zis_locals_drop(z, var);
    *ast_ref = var.ast;
    return modified;
}

// eval(ast :: AstNode) -> result
//# Execute the given code. Returns the result if the code is a non-assignment expression.
//# If an uncaught object is throw, it is printed and nil is returned.
ZIS_NATIVE_FUNC_DEF(F_eval, z, {1, 0, 1}) {
    struct zis_object **const frame = z->callstack->frame;
    if (!zis_object_type_is(frame[1], z->globals->type_AstNode)) {
        zis_move_local(z, 0, 1); // Returns the value it self if it is not an AST node.
        return ZIS_OK;
    }
    struct zis_ast_node_obj *ast = zis_object_cast(frame[1], struct zis_ast_node_obj);
    const bool ast_modified = ast_make_last_expr_assignment(z, REPL_LAST_RESULT_VAR, &ast);
    if (zis_load_global(z, 1, "module", (size_t)-1) == ZIS_THR)
        return ZIS_THR;
    if (!zis_object_type_is(frame[1], z->globals->type_Module)) {
        ensure_var_module(z);
        assert(zis_object_type_is(frame[1], z->globals->type_Module));
    }
    struct zis_module_obj *const module = zis_object_cast(frame[1], struct zis_module_obj);
    struct zis_codegen *codegen = zis_codegen_create(z); // TODO: reuse the code-generator.
    struct zis_func_obj *fn = zis_codegen_generate(codegen, ast, module);
    zis_codegen_destroy(codegen, z);
    if (!fn || zis_module_obj_do_init(z, fn) == ZIS_THR) {
        zis_make_stream(z, 1, ZIS_IOS_STDX, 1); // stdout
        zis_read_exception(z, 0, ZIS_RDE_DUMP, 1);
        zis_load_nil(z, 0, 1);
        return ZIS_OK;
    }
    if (ast_modified) {
        assert(zis_object_type_is(frame[1], z->globals->type_Module));
        zis_load_field(z, 1, REPL_LAST_RESULT_VAR, (size_t)-1, 0);
    } else {
        zis_load_nil(z, 0, 1);
    }
    return ZIS_OK;
}

// print(result)
//# Print the result if it is not nil.
ZIS_NATIVE_FUNC_DEF(F_print, z, {1, 0, 1}) {
    //> if %1 == nil; return; end
    if (zis_read_nil(z, 1) == ZIS_OK) {
        zis_load_nil(z, 0, 1);
        return ZIS_OK; // Don't print nil.
    }
    //> prelude.print(%1)
    if (zis_load_global(z, 0, "prelude", (size_t)-1) == ZIS_THR)
        return ZIS_THR;
    if (zis_load_field(z, 0, "print", (size_t)-1, 0) == ZIS_THR)
        return ZIS_THR;
    return zis_invoke(z, (unsigned int []){0, 0, 1}, 1);
}

// loop()
//# Run the REPL.
ZIS_NATIVE_FUNC_DEF(F_loop, z, {0, 0, 1}) {
    ensure_var_module(z);
    ensure_var_prompt(z);
    while (true) {
        //> %0 = read()
        if (zis_load_global(z, 0, "read", (size_t)-1) == ZIS_THR)
            return ZIS_THR;
        if (zis_invoke(z, (unsigned int []){0, 0}, 0) == ZIS_THR)
            return ZIS_THR;
        if (zis_read_nil(z, 0) == ZIS_OK)
            break;
        //> %1 = eval(%0)
        zis_move_local(z, 1, 0);
        if (zis_load_global(z, 0, "eval", (size_t)-1) == ZIS_THR)
            return ZIS_THR;
        if (zis_invoke(z, (unsigned int []){1, 0, 1}, 1) == ZIS_THR) {
            z->callstack->frame[0] = zis_object_from(z->globals->val_stream_stdout);
            zis_read_exception(z, 1, ZIS_RDE_DUMP, 0);
            continue;
        }
        //> print(%1)
        if (zis_load_global(z, 0, "print", (size_t)-1) == ZIS_THR)
            return ZIS_THR;
        if (zis_invoke(z, (unsigned int []){0, 0, 1}, 1) == ZIS_THR)
            return ZIS_THR;
    }
    return ZIS_OK;
}

// main()
ZIS_NATIVE_FUNC_DEF(F_main, z, {0, 0, 0}) {
    ensure_var_module(z);
    ensure_var_prompt(z);
    // print the banner
    {
        char banner[64];
        snprintf(
            banner, sizeof banner, "ZiS (version %u.%u.%u) REPL\nType %s to quit.\n\n",
            zis_build_info.version[0], zis_build_info.version[1], zis_build_info.version[2],
#ifdef _WIN32
            "<Ctrl-Z><Return>"
#else
            "<Ctrl-D>"
#endif
        );
        struct zis_stream_obj *const stream = z->globals->val_stream_stdout;
        zis_stream_obj_write_chars(stream, banner, strlen(banner));
        zis_stream_obj_flush_chars(stream);
    }
    //> loop()
    if (zis_load_global(z, 0, "loop", (size_t)-1) == ZIS_THR)
        return ZIS_THR;
    if (zis_invoke(z, (unsigned[]){0, 0}, 0) == ZIS_THR)
        return ZIS_THR;
    // clear line
    {
        const char *const s = "(quit)\n";
        struct zis_stream_obj *const stream = z->globals->val_stream_stdout;
        zis_stream_obj_write_chars(stream, s, strlen(s));
        zis_stream_obj_flush_chars(stream);
    }
    return ZIS_OK;
}

// <module_init>()
ZIS_NATIVE_FUNC_DEF(F_init, z, {0, 0, 0}) {
    //> import prelude
    if (zis_import(z, 0, "prelude", ZIS_IMP_NAME) == ZIS_THR)
        return ZIS_THR;
    zis_store_global(z, 0, "prelude", (size_t)-1);
    // OK
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    D_functions,
    { NULL   , &F_init  },
    { "input", &F_input },
    { "read" , &F_read  },
    { "eval" , &F_eval  },
    { "print", &F_print },
    { "loop" , &F_loop  },
    { "main" , &F_main  },
);

ZIS_NATIVE_MODULE(repl) = {
    .functions = D_functions,
    .types     = NULL,
    .variables = NULL,
};
