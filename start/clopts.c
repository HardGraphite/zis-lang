#include "clopts.h"

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

#define STATUS_OK     0
#define STATUS_ERROR  (-1)
#define STATUS_BREAK  1

struct clopts_context {
    jmp_buf jump_buffer;
    FILE *error_stream;
    const char *program_name;
    const struct clopts_option *this_option;
};

_Noreturn void clopts_handler_error(struct clopts_context *ctx, const char *fmt, ...) {
    if (ctx->error_stream) {
        va_list ap;
        va_start(ap, fmt);
        char buffer[256];
        if (vsnprintf(buffer, sizeof buffer, fmt, ap) < 0)
            buffer[0] = 0;
        fprintf(
            ctx->error_stream, "%s: option `-%c': %s\n",
            ctx->program_name, ctx->this_option->name, buffer
        );
        va_end(ap);
    }

    longjmp(ctx->jump_buffer, STATUS_ERROR);
}

_Noreturn void clopts_handler_break(struct clopts_context *ctx) {
    longjmp(ctx->jump_buffer, STATUS_BREAK);
}

_Noreturn static void option_error(struct clopts_context *ctx, const char *fmt, ...) {
    if (ctx->error_stream) {
        va_list ap;
        va_start(ap, fmt);
        char buffer[256];
        if (vsnprintf(buffer, sizeof buffer, fmt, ap) < 0)
            buffer[0] = 0;
        fprintf(ctx->error_stream, "%s: %s\n", ctx->program_name, buffer);
        va_end(ap);
    }

    longjmp(ctx->jump_buffer, STATUS_ERROR);
}

static const struct clopts_option *
find_option(const struct clopts_program *def, char opt_name) {
    for (const struct clopts_option *o = def->options; o->name; o++) {
        if (o->name == opt_name)
            return o;
    }
    return NULL;
}

int clopts_parse(
    const struct clopts_program *def,
    void *data, FILE *err_stream, int argc, char *argv[]
) {
    assert(argc >= 1);

    struct clopts_context context;
    context.error_stream = err_stream;
    context.program_name = argv[0];
    context.this_option  = NULL;

    const int jump_target = setjmp(context.jump_buffer);
    if (jump_target == STATUS_BREAK || jump_target == STATUS_ERROR)
        return jump_target;
    assert(jump_target == STATUS_OK);

    for (int i = 1; i < argc; i++) {
        const char *const s = argv[i];
        if (s[0] == '-') {
            const char s1 = s[1], s2 = s[2];
            if (s1 == '-' && s2 == '\0') {
                i++;
                goto rest_arg;
            } else if (s1 == '\0') {
                goto rest_arg;
            }

            const struct clopts_option *const opt = find_option(def, s1);
            if (!opt)
                option_error(&context, "unrecognized option `-%c'", s1);
            const char *opt_arg = NULL;
            if (opt->arg_name) {
                if (s2 != '\0')
                    opt_arg = s + 2;
                else if (i + 1 == argc || (argv[i + 1][0] == '-' && find_option(def, argv[i + 1][1])))
                    option_error(&context, "missing argument to option `-%c'", s1);
                else
                    opt_arg = argv[++i];
            }
            context.this_option = opt;
            opt->handler(&context, opt_arg, data);
        } else {
        rest_arg:
            def->rest_args(&context, (const char **)argv + i, argc - i, data);
            break;
        }
    }
    return STATUS_OK;
}

void clopts_help(
    const struct clopts_program *def, FILE *stream, struct clopts_context *ctx
) {
    fputs("Usage: ", stream);
    fputs(ctx->program_name, stream);
    fputc(' ', stream);
    fputs(def->usage_args, stream);
    fputs("\n\nOptions:\n", stream);
    for (const struct clopts_option *o = def->options; o->name; o++) {
        const char *s_arg = o->arg_name, *s_help = o->help;
        if (!s_arg)
            s_arg = "";
        if (!s_help)
            s_help = "";
        fprintf(stream, "  -%c %-10s %s\n", o->name, s_arg, s_help);
    }
}
