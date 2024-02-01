#include "compile.h"

#include <setjmp.h>

#include "context.h"
#include "exceptobj.h"
#include "lexer.h"
#include "token.h"

#include "exceptobj.h"
#include "funcobj.h"

#if ZIS_FEATURE_SRC

static jmp_buf _compile_test_jb;
static struct zis_exception_obj *_compile_test_exc = NULL;

zis_noreturn static void _compile_test_lexer_err_handler(
    struct zis_lexer *lexer, const char *restrict msg
) {
    _compile_test_exc = zis_exception_obj_format(
        lexer->z, "syntax", NULL,
        "%u:%u: syntax error: %s", lexer->line, lexer->column, msg
    );
    longjmp(_compile_test_jb, 1);
}

static int _compile_test_compiled_func(struct zis_context *z) {
    zis_unused_var(z);
    return ZIS_OK;
}

struct zis_func_obj *zis_compile_source(
    struct zis_context *z, struct zis_stream_obj *input
) {
    // NOTE: this functions is for lexer tests now, and must be rewritten.

    struct zis_lexer lexer;
    zis_lexer_init(&lexer, z);
    zis_lexer_start(&lexer, input, _compile_test_lexer_err_handler);
    _compile_test_exc = NULL;
    if (!setjmp(_compile_test_jb)) {
        while (true) {
            struct zis_token tok;
            zis_lexer_next(&lexer, &tok);
            if (tok.type == ZIS_TOK_EOF)
                break;
        }
    }
    zis_lexer_finish(&lexer);

    if (_compile_test_exc) {
        zis_context_set_reg0(z, zis_object_from(_compile_test_exc));
        _compile_test_exc = NULL;
        return NULL;
    }

    return zis_func_obj_new_native(
        z, (struct zis_func_obj_meta){.na = 0, .no = 0, .nr = 1},
        _compile_test_compiled_func
    );
}

#endif // ZIS_FEATURE_SRC
