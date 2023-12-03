#include "test.h"

#include <float.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../core/smallint.h" // ZIS_SMALLINT_MIN, ZIS_SMALLINT_MAX

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

zis_test_list(
    // zis-api-values //
    test_nil,
    test_bool,
    test_int,
    test_float,
    test_string,
)
