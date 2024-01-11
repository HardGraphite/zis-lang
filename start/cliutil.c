#include "cliutil.h"

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#    include <io.h>
#    include <Windows.h>
#    define CLI_WINDOWS 1
#elif defined(__unix__) || defined(__unix) || defined(unix) \
        || (defined(__has_include) && __has_include(<unistd.h>))
#    include <sys/ioctl.h>
#    include <unistd.h>
#    define CLI_POSIX 1
#endif

/* ----- terminal info ------------------------------------------------------ */

bool cli_stdout_isatty(void) {
#if CLI_POSIX
    return isatty(STDOUT_FILENO);
#elif CLI_WINDOWS
    return _isatty(_fileno(stdout));
#else
    return true;
#endif
}

size_t cli_stdout_term_width(void) {
#if CLI_POSIX
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col)
        return w.ws_col;
#elif CLI_WINDOWS
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return (size_t)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
#endif
    return 80;
}

/* ----- Command-line options (arguments) parsing --------------------------- */

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
    const struct clopts_program *restrict def,
    void *data, FILE *restrict err_stream, int argc, char *argv[]
) {
    assert(argc >= 1);

    struct clopts_context context;
    context.error_stream = err_stream;
    context.program_name = clopts_path_filename(argv[0]);
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

static void print_text(
    FILE *restrict stream, const char *restrict text,
    size_t line_width, size_t line_indent, size_t beginning_column
) {
    if (line_width < line_indent + 20) {
        fputs(text, stream);
        fputc('\n', stream);
        return;
    }

    const size_t line_text_wid = line_width - line_indent;

    if (beginning_column >= line_indent) {
        fputc(' ', stream);
        beginning_column = 0;
        fputc('\n', stream);
    }
    for (size_t i = beginning_column; i < line_indent; i++)
        fputc(' ', stream);

    for (const char *p = text, *p_end = text - 1; ; p = p_end) {
        bool incomplete_word = false, end_of_text = false;
        while (true) {
            const char *p_end_1 = strchr(p_end + 1, ' ');
            if (!p_end_1) {
                p_end_1 = p_end + strlen(p_end + 1) + 1;
                end_of_text = true;
            }
            if ((size_t)(p_end_1 - p) > line_text_wid) {
                end_of_text = false;
                if (p_end == p) {
                    p_end = p + line_text_wid - 1;
                    incomplete_word = true;
                }
                break;
            }
            p_end = p_end_1;
            if (end_of_text)
                break;
        }
        assert(p <= p_end);
        fwrite(p, 1, (size_t)(p_end - p), stream);

        if (incomplete_word)
            fputc('-', stream);
        else
            p_end++;
        fputc('\n', stream);

        if (end_of_text)
            break;

        for (size_t i = 0; i < line_indent; i++)
            fputc(' ', stream);
    }
}

#define ESC_SEQ_SEC "\x1b[1m"
#define ESC_SEQ_KEY "\x1b[1m"
#define ESC_SEQ_ARG "\x1b[3m"
#define ESC_SEQ_END "\x1b[0m"

void clopts_help(
    const struct clopts_program *restrict def, FILE *restrict stream,
    struct clopts_context *restrict ctx
) {
    const bool use_esc_code = cli_stdout_isatty();
    const size_t width = cli_stdout_term_width();
    const size_t indent = 16;

    if (use_esc_code)
        fputs(ESC_SEQ_SEC, stream);
    fputs("Usage: ", stream);
    if (use_esc_code)
        fputs(ESC_SEQ_END, stream);
    fputs(ctx->program_name, stream);
    fputc(' ', stream);
    fputs(def->usage_args, stream);

    if (use_esc_code)
        fputs(ESC_SEQ_SEC, stream);
    fputs("\n\nOptions:\n", stream);
    if (use_esc_code)
        fputs(ESC_SEQ_END, stream);
    for (const struct clopts_option *o = def->options; o->name; o++) {
        const char *s_arg = o->arg_name, *s_help = o->help;
        const char *fmt_str = !use_esc_code ?
            "  -%c %-10s" :
            "  " ESC_SEQ_KEY "-%c" ESC_SEQ_END " " ESC_SEQ_ARG "%-10s" ESC_SEQ_END;
        int n = fprintf(stream, fmt_str, o->name, s_arg ? s_arg : "");
        if (use_esc_code)
            n -= (int)(sizeof(ESC_SEQ_KEY) + sizeof(ESC_SEQ_ARG) + sizeof(ESC_SEQ_END) * 2 - 4);
        assert(n > 0);
        if (s_help)
            print_text(stream, s_help, width, indent, (size_t)n);
    }
}

void clopts_help_print_list(
    FILE *restrict stream,
    const char *restrict title, const char *const restrict list[]
) {
    const bool use_esc_code = cli_stdout_isatty();
    const size_t width = cli_stdout_term_width();
    const size_t indent = 16;

    fputc('\n', stream);
    if (title) {
        if (use_esc_code)
            fputs(ESC_SEQ_SEC, stream);
        fputs(title, stream);
        fputs(":\n", stream);
        if (use_esc_code)
            fputs(ESC_SEQ_END, stream);
    }

    for (const char *const restrict *p = list; *p; p++) {
        const char *s = *p;
        fputs("  ", stream);
        const size_t n = strlen(s);
        if (use_esc_code)
            fputs(ESC_SEQ_KEY, stream);
        fwrite(s, 1, n, stream);
        if (use_esc_code)
            fputs(ESC_SEQ_END, stream);
        print_text(stream, s + n + 1, width, indent, n + 2);
    }
}

const char *clopts_path_filename(const char *s) {
#ifdef _WIN32
    const char *p1 = strrchr(s, '\\');
    if (!p1) {
        const char *p = strrchr(s, '/');
        return p ? p + 1 : s;
    }
    const char *p2 = strrchr(p1, '/');
    return (p2 ? p2 : p1) + 1;

#else // !_WIN32

    const char *p = strrchr(s, '/');
    return p ? p + 1 : s;

#endif // _WIN32
}
