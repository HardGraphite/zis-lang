#include <zis.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clopts.h"
#include "zis_config.h"

static const struct clopts_program program;

struct command_line_args {
    const char **rest_args;
    size_t rest_args_num;
};

static void oh_help(struct clopts_context *ctx, const char *arg, void *_data) {
    assert(!arg), (void)arg, (void)_data;
    FILE *stream = stdout;

    clopts_help(&program, stream, ctx);

    const char *const environ_helps[] = {
#ifdef ZIS_ENVIRON_NAME_PATH
        ZIS_ENVIRON_NAME_PATH "\0A semicolon-separated list of module search paths.",
#endif // ZIS_ENVIRON_NAME_PATH
#if ZIS_DEBUG_LOGGING && defined(ZIS_ENVIRON_NAME_DEBUG_LOG)
        ZIS_ENVIRON_NAME_DEBUG_LOG
        "\0Debug logging configuration. Syntax: \"[LEVEL]:[GROUP]:[FILE]\"", // See "core/debug.c"
#endif // ZIS_DEBUG_LOGGING && ZIS_ENVIRON_NAME_DEBUG_LOG
        NULL,
    };
    fputs("\nEnvironment variables:\n", stream);
    for (const char *const *p = environ_helps; *p; p++)
        fprintf(stream, "  %-13s %s\n", *p, strchr(*p, 0) + 1);

    clopts_handler_break(ctx);
}

static void oh_version(struct clopts_context *ctx, const char *arg, void *_data) {
    assert(!arg), (void)arg, (void)_data;
    fprintf(
        stdout, ZIS_DISPLAY_NAME " %u.%u.%u\n",
        zis_version[0], zis_version[1], zis_version[2]
    );
    clopts_handler_break(ctx);
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

static const struct clopts_option program_options[] = {
    {'h', NULL, oh_help, "print help message and exit"},
    {'v', NULL, oh_version, "print version and exit"},
    {0, 0, 0, 0},
};

static const struct clopts_program program = {
    .usage_args = "[OPTION...] [FILE|@MODULE [ARGUMENT...]]",
    .options = program_options,
    .rest_args = rest_args_handler,
};

static void parse_command_line_args(
    int argc, char *argv[], struct command_line_args *args
) {
    memset(args, 0, sizeof *args);
    const int ret = clopts_parse(&program, args, stderr, argc, argv);
    if (ret == 0)
        return;
    exit(ret > 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int start(zis_t z, void *_args) {
    struct command_line_args *const args = _args;
    int exit_status = EXIT_SUCCESS;

    if (args->rest_args_num) {
        const char *module = args->rest_args[0];
        int imp_flags = 0;
        if (module[0] == '@') {
            module++;
            imp_flags |= ZIS_IMP_NAME;
        } else {
            imp_flags |= ZIS_IMP_PATH;
        }
        if (zis_import(z, module, imp_flags) == ZIS_THR) {
            char msg[64];
            size_t msg_sz = sizeof msg;
            zis_read_exception(z, 0, 0, 0, 0);
            if (zis_read_string(z, 0, msg, &msg_sz) == ZIS_OK)
                fprintf(stderr, ZIS_DISPLAY_NAME ": error: %.*s\n", (int)msg_sz, msg);
            exit_status = EXIT_FAILURE;
        }
    }

    return exit_status;
}

static int zis_main(int argc, char *argv[]) {
    struct command_line_args args;
    parse_command_line_args(argc, argv, &args);
    struct zis_context *const z = zis_create();
    int exit_status = zis_native_block(z, 0, start, &args);
    zis_destroy(z);
    return exit_status;
}

#ifdef _WIN32

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
