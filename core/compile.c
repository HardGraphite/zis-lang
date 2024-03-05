#include "compile.h"

#include "codegen.h"
#include "context.h"
#include "exceptobj.h"
#include "parser.h"

#include "exceptobj.h"
#include "funcobj.h"

#if ZIS_FEATURE_SRC

struct zis_func_obj *zis_compile_source(
    struct zis_context *z, struct zis_stream_obj *input
) {
    struct zis_parser *parser = zis_parser_create(z);
    struct zis_ast_node_obj *ast = zis_parser_parse(parser, input, ZIS_PARSER_MOD);
    zis_parser_destroy(parser, z);
    if (!ast)
        return NULL;

    struct zis_codegen *codegen = zis_codegen_create(z);
    struct zis_func_obj *func = zis_codegen_generate(codegen, ast);
    zis_codegen_destroy(codegen, z);
    return func;
}

#endif // ZIS_FEATURE_SRC
