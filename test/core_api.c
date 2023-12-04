#include "test.h"

#include <float.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../core/smallint.h" // ZIS_SMALLINT_MIN, ZIS_SMALLINT_MAX

#define REG_MAX 100

// zis-api-natives //

#define TEST_NATIVE_BLOCK_ARG   ((void *)1234)
#define TEST_NATIVE_BLOCK_RET   5678
#define TEST_NATIVE_BLOCK_R0I   9876
#define TEST_NATIVE_BLOCK_R0O   5432
#define TEST_NATIVE_BLOCK_REGS  10

static int do_test_native_block(zis_t z, void *arg) {
    const unsigned int reg_max = TEST_NATIVE_BLOCK_REGS;
    int64_t v_i64 = 0;
    // Check function arg.
    zis_test_assert_eq(arg, TEST_NATIVE_BLOCK_ARG);
    // Check REG-0.
    zis_test_assert_eq(zis_read_int(z, 0, &v_i64), ZIS_OK);
    zis_test_assert_eq(v_i64, TEST_NATIVE_BLOCK_R0I);
    // Check register range.
    for (unsigned int i = 0; i <= reg_max; i++)
        zis_test_assert_eq(zis_move_local(z, i, i), ZIS_OK);
    zis_test_assert_eq(zis_move_local(z, reg_max + 1, reg_max + 1), ZIS_E_IDX);
    // Write REG-0.
    zis_make_int(z, 0, TEST_NATIVE_BLOCK_R0O);
    // Return.
    return TEST_NATIVE_BLOCK_RET;
}

zis_test_define(test_native_block, z) {
    int64_t v_i64 = 0;
    // Write REG-0.
    zis_make_int(z, 0, TEST_NATIVE_BLOCK_R0I);
    // Call.
    const int ret = zis_native_block(
        z, TEST_NATIVE_BLOCK_REGS,
        do_test_native_block, TEST_NATIVE_BLOCK_ARG
    );
    // Check return value.
    zis_test_assert_eq(ret, TEST_NATIVE_BLOCK_RET);
    // Check REG-0.
    zis_test_assert_eq(zis_read_int(z, 0, &v_i64), ZIS_OK);
    zis_test_assert_eq(v_i64, TEST_NATIVE_BLOCK_R0O);
}

// zis-api-values //

zis_test_define(test_nil, z) {
    int status;
    status = zis_load_nil(z, 0, 1);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_read_nil(z, 0);
    zis_test_assert_eq(status, ZIS_OK);
    zis_load_bool(z, 0, true);
    status = zis_read_nil(z, 0);
    zis_test_assert_eq(status, ZIS_E_TYPE);
}

static void do_test_bool(zis_t z, bool v) {
    int status;
    bool value;
    zis_test_log(ZIS_TEST_LOG_TRACE, "v=%s", v ? "true" : "false");
    status = zis_load_bool(z, 0, v);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_read_bool(z, 0, &value);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(value, v);
}

zis_test_define(test_bool, z) {
    do_test_bool(z, true);
    do_test_bool(z, false);
}

static void do_test_int64(zis_t z, int64_t v) {
    int status;
    int64_t value;
    zis_test_log(ZIS_TEST_LOG_TRACE, "v=%" PRIi64, v);
    status = zis_make_int(z, 0, v);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_read_int(z, 0, &value);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(value, v);
}

zis_test_define(test_int, z) {
    for (int64_t i = INT8_MIN; i <= INT8_MAX; i++)
        do_test_int64(z, i);
    for (int64_t i = ZIS_SMALLINT_MIN - 5; i <= ZIS_SMALLINT_MIN + 5; i++)
        do_test_int64(z, i);
    for (int64_t i = ZIS_SMALLINT_MAX - 5; i <= ZIS_SMALLINT_MAX + 5; i++)
        do_test_int64(z, i);
    do_test_int64(z, INT64_MIN);
    do_test_int64(z, INT64_MAX);
}

static void do_test_float(zis_t z, double v) {
    int status;
    double value;
    zis_test_log(ZIS_TEST_LOG_TRACE, "v=%g", v);
    status = zis_make_float(z, 0, v);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_read_float(z, 0, &value);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(value, v);
}

zis_test_define(test_float, z) {
    do_test_float(z, 0.0);
    do_test_float(z, 0.1);
    do_test_float(z, 1.0);
    do_test_float(z, DBL_EPSILON);
    do_test_float(z, DBL_MIN);
    do_test_float(z, DBL_TRUE_MIN);
    do_test_float(z, DBL_MAX);
}

static void do_test_string_n(zis_t z, const char *s, size_t n) {
    int status;
    char * out_buf;
    size_t out_len;

    zis_test_log(ZIS_TEST_LOG_TRACE, "s=\"%s\", n=%zi", s, n);

    // Create string.
    status = zis_make_string(z, 0, s, n);
    zis_test_assert_eq(status, ZIS_OK);
    if (n == (size_t)-1)
        n = strlen(s);

    // Get expected buffer size.
    status = zis_read_string(z, 0, NULL, &out_len);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(out_len, n);

    // Get string.
    out_buf = malloc(out_len);
    status = zis_read_string(z, 0, out_buf, &out_len);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(out_len, n);
    zis_test_assert_eq(memcmp(out_buf, s, n), 0);
    free(out_buf);

    // Try a smaller buffer.
    if (n > 1) {
        char tiny_buf[1];
        out_len = sizeof tiny_buf;
        status = zis_read_string(z, 0, tiny_buf, &out_len);
        zis_test_assert_eq(status, ZIS_E_BUF);
    }
}

static void do_test_string(zis_t z, const char *s) {
    do_test_string_n(z, s, strlen(s));
}

static void do_test_bad_string(zis_t z, const char *s, size_t n) {
    int status;
    assert(n != (size_t)-1);
    zis_test_log(ZIS_TEST_LOG_TRACE, "s=\"%s\", n=%zi", s, n);
    status = zis_make_string(z, 0, s, n);
    zis_test_assert_eq(status, ZIS_E_ARG);
}

zis_test_define(test_string, z) {
    do_test_string(z, "Hello, World!");
    do_test_string(z, u8"你好，世界！"); // U+4F60 U+597D U+FF0C U+4E16 U+754C U+FF01
    do_test_string(z, u8"Olá, mundo!"); // U+004F U+006C U+00E1 U+002C U+0020 U+006D U+0075 U+006E U+0064 U+006F
    do_test_string(z, u8"😃, 🌏!"); // U+1F603 U+002C U+0020 U+1F30F U+0021
    do_test_string_n(z, "Hello\0World", 12);
    do_test_bad_string(z, "\xff", 1);
    do_test_bad_string(z, u8"你好", 4); // U+4F60 U+597D => [e4 bd a0] [e5 a5 bd]
}

// main

zis_test_list(
    REG_MAX,
    // zis-api-native //
    test_native_block,
    // zis-api-values //
    test_nil,
    test_bool,
    test_int,
    test_float,
    test_string,
)
