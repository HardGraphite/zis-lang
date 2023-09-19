#include <zis.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "clopts.h"

static const struct clopts_program program;

struct command_line_args {
    int _;
};

static void oh_help(struct clopts_context *ctx, const char *arg, void *_data) {
    assert(!arg);
    (void)_data;
    clopts_help(&program, stdout, ctx);
    clopts_handler_break(ctx);
}

static void rest_args_handler(
    struct clopts_context *ctx, const char *argv[], int argc, void *_data
) {
    (void)ctx, (void)argv, (void)argc, (void)_data;
}

static const struct clopts_option program_options[] = {
    {'h', 0, oh_help, "print help message and exit"},
    {0, 0, 0, 0},
};

static const struct clopts_program program = {
    .usage_args = "[OPTION...]",
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

static int zis_main(int argc, char *argv[]) {
    struct command_line_args args;
    parse_command_line_args(argc, argv, &args);

    struct zis_context *const z = zis_create();
    zis_destroy(z);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    return zis_main(argc, argv);
}
