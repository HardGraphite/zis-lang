#include "test.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/// Compiles and evaluates code. If `get_var` is not NULL puts this variable in REG-0.
static void comp_and_exec_code(zis_t z, const char *restrict code, const char *restrict get_var) {
    int status;
    status = zis_import(z, 0, code, ZIS_IMP_CODE);
    zis_test_assert_eq(status, ZIS_OK);
    if (get_var) {
        status = zis_load_field(z, 0, get_var, (size_t)-1, 0);
        zis_test_assert_eq(status, ZIS_OK);
    } else {
        zis_load_nil(z, 0, 1);
    }
}

/// Compiles and evaluates an expression. Puts the result in REG-0.
static void comp_and_eval_expr(zis_t z, const char *restrict expr) {
    static char buffer[1024];
    const char *const var = "__RESULT__";
    snprintf(buffer, sizeof buffer, "%s = ( %s )", var, expr);
    comp_and_exec_code(z, buffer, var);
}

/// Compiles the code. Expecting a syntex error.
static void comp_wrong_code(zis_t z, const char *restrict code) {
    int status;
    char buffer[128];
    size_t size;

    status = zis_import(z, 0, code, ZIS_IMP_CODE);
    zis_test_assert_eq(status, ZIS_THR);

    zis_move_local(z, 1, 0);
    status = zis_read_exception(z, 1, ZIS_RDE_TYPE, 2);
    zis_test_assert_eq(status, ZIS_OK);
    size = sizeof buffer;
    status = zis_read_symbol(z, 2, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert(strncmp(buffer, "syntax", size) == 0);
    status = zis_read_exception(z, 1, ZIS_RDE_WHAT, 2);
    zis_test_assert_eq(status, ZIS_OK);
    size = sizeof buffer;
    status = zis_read_string(z, 2, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    buffer[size < sizeof buffer ? size : size - 1] = 0;
    zis_test_log(ZIS_TEST_LOG_TRACE, "``%.8s...'': %s", code, buffer);
}

/// Checks whether REG-0 is an Int and is equal to `val`.
static void check_int_value(zis_t z, int64_t val) {
    int status;
    int64_t actual_val;
    status = zis_read_int(z, 0, &actual_val);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(actual_val, val);
}

static void do_test_lit_int(zis_t z, const char *restrict code, int64_t val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "lit_int: %s, value: %" PRIi64, code, val);
    comp_and_eval_expr(z, code);
    check_int_value(z, val);
}

zis_test_define(test_lit_int, z) {
    for (int i = 0; i <= 1000; i++) {
        char buffer[32];
        snprintf(buffer, sizeof buffer, "%i", i);
        do_test_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "%09i", i);
        do_test_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "0o%o", i);
        do_test_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "0O%09o", i);
        do_test_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "%#x", i);
        do_test_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "%#09X", i);
        do_test_lit_int(z, buffer, i);
    }
    do_test_lit_int(z, "1_2_34", 1234);
    do_test_lit_int(z, "0xff_ff", 0xffff);

    comp_wrong_code(z, "0x");
    comp_wrong_code(z, "0a1");
    comp_wrong_code(z, "0b2");
}

static void do_test_lit_float(zis_t z, const char *restrict code, double val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "lit_float: %s, value: %f", code, val);
    int status;
    double actual_val;
    comp_and_eval_expr(z, code);
    status = zis_read_float(z, 0, &actual_val);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(actual_val, val);
}

zis_test_define(test_lit_float, z) {
    for (int i = 0; i <= 1000; i++) {
        char buffer[32];
        const double val = i / 64.0;
        snprintf(buffer, sizeof buffer, "%f", val);
        do_test_lit_float(z, buffer, val);
    }
    do_test_lit_float(z, "0x12.34", 0x12.34p0);
    do_test_lit_float(z, "0xff.ff", 0xff.ffp0);

    comp_wrong_code(z, "1.");
    comp_wrong_code(z, "1.a");
}

static void do_test_lit_string(zis_t z, const char *restrict code, const char *restrict val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "lit_string: <<<%s>>>, value: <<<%s>>>", code, val);
    int status;
    char actual_str[64];
    size_t actual_len = sizeof actual_str;
    comp_and_eval_expr(z, code);
    status = zis_read_string(z, 0, actual_str, &actual_len);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(actual_len, strlen(val));
    zis_test_assert(memcmp(actual_str, val, actual_len) == 0);
}

zis_test_define(test_lit_string, z) {
    do_test_lit_string(z, "''", "");
    do_test_lit_string(z, "'abc'", "abc");
    do_test_lit_string(z, "\"abc\"", "abc");
    do_test_lit_string(z, u8"'你好，世界！'", u8"你好，世界！");
    do_test_lit_string(z, "'\\\\'", "\\");
    do_test_lit_string(z, "'\\''", "'");
    do_test_lit_string(z, "'a\nb'", "a\nb");
    do_test_lit_string(z, "'\\x7e1'", "~1");
    do_test_lit_string(z, "'\\u{4f60}\\u{597D}^_^'", u8"你好^_^");
    do_test_lit_string(z, "@'\\\\\\'", "\\\\\\");

    comp_wrong_code(z, "'abc");
    comp_wrong_code(z, "\"abc'");
    comp_wrong_code(z, "'\\x1'");
    comp_wrong_code(z, "'\\x1g'");
    comp_wrong_code(z, "'\\xff'");
    comp_wrong_code(z, "'\\u{123'");
    comp_wrong_code(z, "'\\u{}'");
    comp_wrong_code(z, "'\\z'");
    comp_wrong_code(z, "'\\'");
    comp_wrong_code(z, "'\\\\\\'");
}

static void do_test_identifier(zis_t z, const char *restrict code, const char *restrict val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "identifier: <<<%s>>>, value: <<<%s>>>", code, val);
    char buffer[64];
    int status;
    int64_t actual_val;
    snprintf(buffer, sizeof buffer, "%s = 1234", code);
    comp_and_eval_expr(z, buffer);
    status = zis_read_int(z, 0, &actual_val);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(actual_val, 1234);
}

zis_test_define(test_identifier, z) {
    do_test_identifier(z, "abc", "abc");
    do_test_identifier(z, "ab12_", "ab12_");
    do_test_identifier(z, " abc ", "abc");
    do_test_identifier(z, "\tabc\n", "abc");
}

zis_test_define(test_comment, z) {
    comp_wrong_code(z, "'");
    comp_and_exec_code(z, " # '", NULL);
    comp_and_exec_code(z, "'  # '", NULL);
}

zis_test_define(test_expr, z) {
    const int64_t a = 2, b = 3, c = 4;
    comp_and_exec_code(z, "a = 2; b = 3; c = 4; Y = a + b * c", "Y");
    check_int_value(z, a + b * c);
    comp_and_exec_code(z, "a = 2; b = 3; c = 4; Y = a * b + c", "Y");
    check_int_value(z, a * b + c);
    comp_and_exec_code(z, "a = 2; b = 3; c = 4; Y = a * (b + c)", "Y");
    check_int_value(z, a * (b + c));
}

zis_test_define(test_cond_stmt, z) {
    comp_and_exec_code(z,
        "a = 10 \n"
        "if a < 20 \n"
        "    Y = 1 \n"
        "elif a < 30 \n"
        "    Y = 2 \n"
        "else \n"
        "    Y = 3 \n"
        "end \n"
    , "Y");
    check_int_value(z, 1);
    comp_and_exec_code(z,
        "a = 10 \n"
        "if a < 10 \n"
        "    Y = 1 \n"
        "elif a < 20 \n"
        "    Y = 2 \n"
        "else \n"
        "    Y = 3 \n"
        "end \n"
    , "Y");
    check_int_value(z, 2);
    comp_and_exec_code(z,
        "a = 10 \n"
        "if a < 5 \n"
        "    Y = 1 \n"
        "elif a < 10 \n"
        "    Y = 2 \n"
        "else \n"
        "    Y = 3 \n"
        "end \n"
    , "Y");
    check_int_value(z, 3);
}

zis_test_define(test_while_stmt, z) {
    comp_and_exec_code(z,
        "i = 0 \n"
        "while i < 1000 \n"
        "    i += 1 \n"
        "end \n"
    , "i");
    check_int_value(z, 1000);
    comp_and_exec_code(z,
        "i = 0 \n"
        "while true \n"
        "    i += 1 \n"
        "    if i < 1000 \n"
        "        continue \n"
        "    else \n"
        "        break \n"
        "    end \n"
        "    i = 0 \n"
        "end \n"
    , "i");
    check_int_value(z, 1000);
}

zis_test_define(test_func_stmt, z) {
    comp_and_exec_code(z,
        "func fibonacci(i) \n"
        "    if i < 2 \n"
        "        return i \n"
        "    end \n"
        "    return fibonacci(i - 1) + fibonacci(i - 2) \n"
        "end \n"
        "Y = fibonacci(10) \n"
    , "Y");
    check_int_value(z, 55);
}

zis_test_list(
    100,
    test_lit_int,
    test_lit_float,
    test_lit_string,
    test_identifier,
    test_comment,
    test_expr,
    test_cond_stmt,
    test_while_stmt,
    test_func_stmt,
)
