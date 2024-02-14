#include "compile.h"

#include "context.h"
#include "exceptobj.h"
#include "parser.h"

#include "exceptobj.h"
#include "funcobj.h"

#if ZIS_FEATURE_SRC

static int _compile_test_compiled_func(struct zis_context *z) {
    zis_unused_var(z);
    return ZIS_OK;
}

struct zis_func_obj *zis_compile_source(
    struct zis_context *z, struct zis_stream_obj *input
) {
    // NOTE: this functions is for parser tests now, and must be rewritten.

    struct zis_parser *parser = zis_parser_create(z);
    struct zis_ast_node_obj *ast = zis_parser_parse(parser, input, ZIS_PARSER_MOD);
    zis_parser_destroy(parser, z);

    if (!ast)
        return NULL;

    return zis_func_obj_new_native(
        z, (struct zis_func_obj_meta){.na = 0, .no = 0, .nr = 1},
        _compile_test_compiled_func
    );
}

#endif // ZIS_FEATURE_SRC
