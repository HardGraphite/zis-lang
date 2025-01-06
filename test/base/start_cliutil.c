#include "test.h"

#include <stdlib.h>
#include <string.h>

#include "start/cliutil.c"

struct data {
    int a;
    char b;
    const char **rest;
    int rest_n;
};

static const struct clopts_program program;

static void oh_help(struct clopts_context *ctx, const char *arg, void *_data) {
    zis_test_assert(!arg);
    (void)_data;
    clopts_help(&program, stdout, ctx);
    clopts_handler_break(ctx);
}

static void oh_a(struct clopts_context *ctx, const char *arg, void *_data) {
    (void)ctx;
    zis_test_assert(arg);
    struct data *data = _data;
    data->a = atoi(arg);
}

static void oh_b(struct clopts_context *ctx, const char *arg, void *_data) {
    zis_test_assert(arg);
    struct data *data = _data;
    if (!(arg[0] && !arg[1]))
        clopts_handler_error(ctx, "bad argument: `%s'", arg);
    data->b = arg[0];
}

static void rest_args_handler(
    struct clopts_context *ctx, const char *argv[], int argc, void *_data
) {
    (void )ctx;
    zis_test_assert(argv);
    zis_test_assert(argc > 0);
    struct data *data = _data;
    data->rest = argv;
    data->rest_n = argc;
}

static const struct clopts_option program_options[] = {
    {'h', 0, oh_help, "help"},
    {'a', "INT", oh_a, "int a"},
    {'b', "CHAR", oh_b, "char b"},
    {0, 0, 0, 0},
};

static const struct clopts_program program = {
    .usage_args = "[OPTION...]",
    .options = program_options,
    .rest_args = rest_args_handler,
};

zis_test0_define(help) {
    int argc = 2;
    const char *argv[] = {"test", "-h", 0};
    const int n = clopts_parse(&program, NULL, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 1);
}

zis_test0_define(opt_with_arg) {
    int argc = 5;
    const char *argv[] = {"test", "-a", "123", "-b", "*", 0};
    struct data data;
    memset(&data, 0, sizeof data);
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 0);
    zis_test_assert_eq(data.a, 123);
    zis_test_assert_eq(data.b, '*');
}

zis_test0_define(opt_with_arg_2) {
    int argc = 3;
    const char *argv[] = {"test", "-a456", "-b+", 0};
    struct data data;
    memset(&data, 0, sizeof data);
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 0);
    zis_test_assert_eq(data.a, 456);
    zis_test_assert_eq(data.b, '+');
}

zis_test0_define(opt_with_arg_3) {
    int argc = 3;
    const char *argv[] = {"test", "-a", "-24", 0};
    struct data data;
    memset(&data, 0, sizeof data);
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 0);
    zis_test_assert_eq(data.a, -24);
}

zis_test0_define(rest_args) {
    int argc = 3;
    const char *argv[] = {"test", "A", "B", 0};
    struct data data;
    memset(&data, 0, sizeof data);
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 0);
    zis_test_assert_eq(data.rest_n, 2);
    zis_test_assert_eq(data.rest, argv + 1);
}

zis_test0_define(rest_args_2) {
    int argc = 3;
    const char *argv[] = {"test", "-", "B", 0};
    struct data data;
    memset(&data, 0, sizeof data);
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 0);
    zis_test_assert_eq(data.rest_n, 2);
    zis_test_assert_eq(data.rest, argv + 1);
}

zis_test0_define(rest_args_3) {
    int argc = 3;
    const char *argv[] = {"test", "--", "B", 0};
    struct data data;
    memset(&data, 0, sizeof data);
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, 0);
    zis_test_assert_eq(data.rest_n, 1);
    zis_test_assert_eq(data.rest, argv + 2);
}

zis_test0_define(bad_arg) {
    int argc = 3;
    const char *argv[] = {"test", "-b", "xxx", 0};
    struct data data;
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, -1);
}

zis_test0_define(too_few_arg) {
    int argc = 2;
    const char *argv[] = {"test", "-a", 0};
    struct data data;
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, -1);
}

zis_test0_define(too_few_arg_2) {
    int argc = 3;
    const char *argv[] = {"test", "-a", "-b", 0};
    struct data data;
    const int n = clopts_parse(&program, &data, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, -1);
}

zis_test0_define(bad_opt) {
    int argc = 2;
    const char *argv[] = {"test", "-x", 0};
    const int n = clopts_parse(&program, NULL, stderr, argc, (char **)argv);
    zis_test_assert_eq(n, -1);
}

zis_test0_list(
    start_cliutil,
    zis_test0_case(help),
    zis_test0_case(opt_with_arg),
    zis_test0_case(opt_with_arg_2),
    zis_test0_case(opt_with_arg_3),
    zis_test0_case(rest_args),
    zis_test0_case(rest_args_2),
    zis_test0_case(rest_args_3),
    zis_test0_case(bad_arg),
    zis_test0_case(too_few_arg),
    zis_test0_case(too_few_arg_2),
    zis_test0_case(bad_opt),
)
