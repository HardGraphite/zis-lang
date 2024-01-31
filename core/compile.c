#include "compile.h"

#include "context.h"
#include "exceptobj.h"
#include "lexer.h"
#include "token.h"

#if ZIS_FEATURE_SRC

struct zis_func_obj *zis_compile(
    struct zis_context *z, struct zis_stream_obj *input
) {
    // NOTE: this functions is for lexer tests now, and must be rewritten.

    struct zis_lexer lexer;
    zis_lexer_init(&lexer, z);
    zis_lexer_start(&lexer, input, NULL);
    while (true) {
        struct zis_token tok;
        zis_lexer_next(&lexer, &tok);
        if (tok.type == ZIS_TOK_EOF)
            break;
    }
    zis_lexer_finish(&lexer);

    struct zis_exception_obj *exc =
        zis_exception_obj_format(z, NULL, NULL, "the compiler is not implemented yet");
    zis_context_set_reg0(z, zis_object_from(exc));
    return NULL;
}

#endif // ZIS_FEATURE_SRC
