#include <zis.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cliutil.h"
#include "zis_config.h"

#define EXIT_BADARGS (EXIT_FAILURE + 1) // Illegal command line arguments
static_assert(EXIT_BADARGS != EXIT_SUCCESS, "");

static void oh_help(struct clopts_context *, const char *, void *);
static void oh_version(struct clopts_context *, const char *, void *);
static void oh_interactive(struct clopts_context *, const char *, void *);
static void rest_args_handler(struct clopts_context *, const char *[], int, void *);

static const struct clopts_option program_options[] = {
    {'h', NULL, oh_help, "Print help message and exit."},
    {'v', NULL, oh_version, "Print version and build information, and exit."},
    {'i', NULL, oh_interactive, "Enter the interactive mode."},
    {0, 0, 0, 0},
};

static const struct clopts_program program = {
    .usage_args = "[OPTION...] [[--] -|FILE|@MODULE|=CODE [ARGUMENT...]]",
    .options = program_options,
    .rest_args = rest_args_handler,
};

static const char *const program_prog_specifier_helps[] = {
    "-\0"         "Read source code from stdin.",
    "<FILE>\0"    "Run program in the file. <FILE> is the path to the file.",
    "@<MODULE>\0" "Run module as a program. <MODULE> is the name of the module.",
    "=<CODE>\0"   "Execute sourc code string <CODE>.",
    "(empty)\0"   "Enter interactive mode if stdin is a terminal; otherwise read source code from stdin.",
    NULL
};

static const char *const program_environ_helps[] = {
#ifdef ZIS_ENVIRON_NAME_PATH
    ZIS_ENVIRON_NAME_PATH
    "\0A semicolon-separated list of module search paths.",
#endif // ZIS_ENVIRON_NAME_PATH
#ifdef ZIS_ENVIRON_NAME_MEMS
    ZIS_ENVIRON_NAME_MEMS
    "\0Object memory configuration. "
    "Syntax: \"STACK_SZ;<heap_opts>\", "
    "syntax for <heap_opts>: \"NEW_SPC,OLD_SPC_NEW:OLD_SPC_MAX,BIG_SPC_NEW:BIG_SPC_MAX\".",
    // See "core/context.c"
#endif // ZIS_ENVIRON_NAME_MEMS
#if ZIS_DEBUG_LOGGING && defined(ZIS_ENVIRON_NAME_DEBUG_LOG)
    ZIS_ENVIRON_NAME_DEBUG_LOG
    "\0Debug logging configuration. Syntax: \"[LEVEL]:[GROUP]:[FILE]\".",
    // See "core/debug.c"
#endif // ZIS_DEBUG_LOGGING && ZIS_ENVIRON_NAME_DEBUG_LOG
    NULL
};

struct command_line_args {
    const char **rest_args;
    size_t rest_args_num;
    bool force_interactive;
};

static void oh_help(struct clopts_context *ctx, const char *arg, void *_data) {
    assert(!arg), (void)arg, (void)_data;
    FILE *stream = stdout;

    clopts_help(&program, stream, ctx);
    clopts_help_print_list(stream, "Program specifiers", program_prog_specifier_helps);
    clopts_help_print_list(stream, "Environment variables", program_environ_helps);

    clopts_handler_break(ctx);
}

static void oh_version(struct clopts_context *ctx, const char *arg, void *_data) {
    assert(!arg), (void)arg, (void)_data;
    FILE *const stream = stdout;

    char time_str[32];
    const time_t timestamp = (time_t)zis_build_info.timestamp * 60;
    strftime(time_str, sizeof time_str, "%F %R %z", localtime(&timestamp));

    fprintf(
        stream, ZIS_DISPLAY_NAME " %u.%u.%u\n" "[%s %s; %s; %s]\n",
        zis_build_info.version[0], zis_build_info.version[1], zis_build_info.version[2],
        zis_build_info.system, zis_build_info.machine, zis_build_info.compiler, time_str
    );
    if (zis_build_info.extra) {
        fputc('\n', stream);
        fputs(zis_build_info.extra, stream);
        fputc('\n', stream);
    }
    clopts_handler_break(ctx);
}

static void oh_interactive(struct clopts_context *ctx, const char *arg, void *_data) {
    assert(!arg), (void)ctx, (void)arg;
    struct command_line_args *args = _data;
    args->force_interactive = true;
}

static void rest_args_handler(
    struct clopts_context *ctx, const char *argv[], int argc, void *_data
) {
    (void)ctx;
    struct command_line_args *args = _data;
    assert(argc >= 0);
    args->rest_args = argv;
    args->rest_args_num = (size_t)argc;
}

static void parse_command_line_args(
    int argc, char *argv[], struct command_line_args *args
) {
    memset(args, 0, sizeof *args);
    const int ret = clopts_parse(&program, args, stderr, argc, argv);
    if (ret == 0)
        return;
    exit(ret > 0 ? EXIT_SUCCESS : EXIT_BADARGS);
}

static int start(zis_t z, void *_args) {
    struct command_line_args *const args = _args;
    int exit_status;
    const char *imp_what;
    int imp_flags;

    if (args->force_interactive) {
        imp_what = "repl";
        imp_flags = ZIS_IMP_NAME;
    } else if (args->rest_args_num) {
        const char *module = args->rest_args[0];
        if (module[0] == '-' && !module[1]) {
            zis_make_stream(z, 0, ZIS_IOS_STDX, 0); // stdin
            imp_what = NULL;
            imp_flags = ZIS_IMP_CODE;
        } else if (module[0] == '=') {
            imp_what = module + 1;
            imp_flags = ZIS_IMP_CODE;
        } else if (module[0] == '@') {
            imp_what = module + 1;
            imp_flags = ZIS_IMP_NAME;
        } else {
            imp_what = module;
            imp_flags = ZIS_IMP_PATH;
        }
    } else {
        if (cli_stdin_isatty()) {
            imp_what = "repl";
            imp_flags = ZIS_IMP_NAME;
        } else {
            zis_make_stream(z, 0, ZIS_IOS_STDX, 0); // stdin
            imp_what = NULL;
            imp_flags = ZIS_IMP_CODE;
        }
    }

    imp_flags |= ZIS_IMP_MAIN;
    zis_make_int(z, 1, (int64_t)args->rest_args_num);
    zis_make_int(z, 2, (intptr_t)args->rest_args);
    const int status = zis_import(z, 0, imp_what, imp_flags);
    if (status >= 0) {
        exit_status = status;
    } else {
        assert(status == ZIS_THR);
        zis_move_local(z, 1, 0);
        zis_make_stream(z, 2, ZIS_IOS_STDX, 2); // stderr
        zis_read_exception(z, 1, ZIS_RDE_DUMP, 2);
        exit_status = EXIT_FAILURE;
    }

    return exit_status;
}

static int zis_main(int argc, char *argv[]) {
    struct command_line_args args;
    parse_command_line_args(argc, argv, &args);
    struct zis_context *const z = zis_create();
    int exit_status = zis_native_block(z, 2, start, &args);
    zis_destroy(z);
    return exit_status;
}

#if defined(_WIN32) && !defined(__MINGW32__)

#include "winutil.h"

static char **u8_argv = NULL;

int wmain(int argc, wchar_t *wargv[]) {
    assert(!u8_argv);
    assert(argc >= 0);
    u8_argv = win_wstrv_to_utf8(wargv, (size_t)(argc + 1));
    win_utf8_init();
    win_term_init();
    return zis_main(argc, u8_argv);
}

#else // !_WIN32

int main(int argc, char *argv[]) {
    return zis_main(argc, argv);
}

#endif // _WIN32
