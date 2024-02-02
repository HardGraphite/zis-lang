#include "test.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int try_compile(zis_t z, const char *restrict code) {
    return zis_import(z, 0, code, ZIS_IMP_CODE);
}

static void do_compile_expecting_success(zis_t z, const char *restrict code) {
    int status = try_compile(z, code);
    zis_test_assert_eq(status, ZIS_OK);
}

static void do_compile_expecting_error(zis_t z, const char *restrict code) {
    int status;
    char buffer[128];
    size_t size;

    status = try_compile(z, code);
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

static void do_test_lexer_lit_int(zis_t z, const char *restrict code, int64_t val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "lit_int: %s, value: %" PRIi64, code, val);
    int status = try_compile(z, code);
    zis_test_assert_eq(status, ZIS_OK);
    (void)val; // TODO: check value.
}

zis_test_define(test_lexer_lit_int, z) {
    for (int i = 0; i <= 1000; i++) {
        char buffer[32];
        snprintf(buffer, sizeof buffer, "%i", i);
        do_test_lexer_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "%09i", i);
        do_test_lexer_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "0o%o", i);
        do_test_lexer_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "0O%09o", i);
        do_test_lexer_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "%#x", i);
        do_test_lexer_lit_int(z, buffer, i);
        snprintf(buffer, sizeof buffer, "%#09X", i);
        do_test_lexer_lit_int(z, buffer, i);
    }
    do_test_lexer_lit_int(z, "1_2_34", 1234);
    do_test_lexer_lit_int(z, "0xff_ff", 0xffff);

    do_compile_expecting_error(z, "0x");
    do_compile_expecting_error(z, "0a1");
    do_compile_expecting_error(z, "0b2");
}

static void do_test_lexer_lit_float(zis_t z, const char *restrict code, double val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "lit_float: %s, value: %f", code, val);
    int status = try_compile(z, code);
    zis_test_assert_eq(status, ZIS_OK);
    (void)val; // TODO: check value.
}

zis_test_define(test_lexer_lit_float, z) {
    for (int i = 0; i <= 1000; i++) {
        char buffer[32];
        snprintf(buffer, sizeof buffer, "%f", i / 96.0);
        do_test_lexer_lit_float(z, buffer, i);
    }
    do_test_lexer_lit_float(z, "0x12.34", 0x12.34p0);
    do_test_lexer_lit_float(z, "0xff.ff", 0xff.ffp0);

    do_compile_expecting_error(z, "1.");
    do_compile_expecting_error(z, "1.a");
}

static void do_test_lexer_lit_string(zis_t z, const char *restrict code, const char *restrict val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "lit_string: <<<%s>>>, value: <<<%s>>>", code, val);
    int status = try_compile(z, code);
    zis_test_assert_eq(status, ZIS_OK);
    (void)val; // TODO: check value.
}

zis_test_define(test_lexer_lit_string, z) {
    do_test_lexer_lit_string(z, "''", "");
    do_test_lexer_lit_string(z, "'abc'", "abc");
    do_test_lexer_lit_string(z, "\"abc\"", "abc");
    do_test_lexer_lit_string(z, u8"'你好，世界！'", u8"你好，世界！");
    do_test_lexer_lit_string(z, "'\\\\'", "\\");
    do_test_lexer_lit_string(z, "'\\''", "'");
    do_test_lexer_lit_string(z, "'a\nb'", "a\nb");
    do_test_lexer_lit_string(z, "'\\x7e1'", "~1");
    do_test_lexer_lit_string(z, "'\\u{4f60}\\u{597D}^_^'", "~1");
    do_test_lexer_lit_string(z, "@'\\\\\\'", "\\\\\\");

    do_compile_expecting_error(z, "'abc");
    do_compile_expecting_error(z, "\"abc'");
    do_compile_expecting_error(z, "'\\x1'");
    do_compile_expecting_error(z, "'\\x1g'");
    do_compile_expecting_error(z, "'\\xff'");
    do_compile_expecting_error(z, "'\\u{123'");
    do_compile_expecting_error(z, "'\\u{}'");
    do_compile_expecting_error(z, "'\\z'");
    do_compile_expecting_error(z, "'\\'");
    do_compile_expecting_error(z, "'\\\\\\'");
}

static void do_test_lexer_identifier(zis_t z, const char *restrict code, const char *restrict val) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "identifier: <<<%s>>>, value: <<<%s>>>", code, val);
    int status = try_compile(z, code);
    zis_test_assert_eq(status, ZIS_OK);
    (void)val; // TODO: check value.
}

zis_test_define(test_lexer_identifier, z) {
    do_test_lexer_identifier(z, "abc", "abc");
    do_test_lexer_identifier(z, "ab12_", "ab12_");
    do_test_lexer_identifier(z, " abc ", "abc");
    do_test_lexer_identifier(z, "\tabc\n", "abc");
}

static void do_test_lexer_keyword(zis_t z, const char *restrict kw) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "keyword: %s", kw);
    int status = try_compile(z, kw);
    zis_test_assert_eq(status, ZIS_OK);
}

zis_test_define(test_lexer_keyword, z) {
    // See "../core/token.h".
    const char *const kw_list[] = {
        "nil",
        "true",
        "false",
        "func",
        "struct",
        "if",
        "elif",
        "else",
        "while",
        "for",
        "break",
        "continue",
        "return",
        "throw",
        "end"  ,
    };
    for (unsigned int i = 0; i < sizeof kw_list / sizeof kw_list[0]; i++)
        do_test_lexer_keyword(z, kw_list[i]);
}

zis_test_define(test_lexer_comment, z) {
    do_compile_expecting_error(z, "'");
    do_compile_expecting_success(z, " # '");
    do_compile_expecting_success(z, "'  # '");
}

zis_test_list(
    100,
    test_lexer_lit_int,
    test_lexer_lit_float,
    test_lexer_lit_string,
    test_lexer_identifier,
    test_lexer_keyword,
    test_lexer_comment,
)
