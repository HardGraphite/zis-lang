#include "compile.h"

#include "codegen.h"
#include "context.h"
#include "locals.h"
#include "parser.h"

#include "exceptobj.h"
#include "funcobj.h"

#if ZIS_FEATURE_SRC

void zis_compilation_bundle_init(struct zis_compilation_bundle *restrict cb, struct zis_context *z) {
    cb->parser  = zis_parser_create(z);
    cb->codegen = zis_codegen_create(z);
    cb->context = z;
}

void zis_compilation_bundle_fini(struct zis_compilation_bundle *restrict cb) {
    struct zis_context *z = cb->context;
    zis_parser_destroy(cb->parser, z);
    zis_codegen_destroy(cb->codegen, z);
}

struct zis_func_obj *zis_compile_source(
    const struct zis_compilation_bundle *restrict comp_bundle,
    struct zis_stream_obj *_input, struct zis_module_obj *_module /* = NULL */
) {
    struct zis_context *const z = comp_bundle->context;
    struct zis_func_obj *func = NULL;
    zis_locals_decl(
        z, var,
        struct zis_stream_obj *input;
        struct zis_module_obj *module;
    );
    zis_locals_zero(var);
    var.input = _input;
    if (_module)
        var.module = _module;

    struct zis_ast_node_obj *ast =
        zis_parser_parse(comp_bundle->parser, var.input, ZIS_PARSER_MOD);
    if (ast)
        func = zis_codegen_generate(comp_bundle->codegen, ast, _module ? var.module : NULL);

    zis_locals_drop(z, var);
    return func;
}

#endif // ZIS_FEATURE_SRC
