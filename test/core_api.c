#include "test.h"

#include <float.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "../core/smallint.h" // ZIS_SMALLINT_MIN, ZIS_SMALLINT_MAX

#define REG_MAX 100

// zis-api-context //

static jmp_buf test_at_panic_jb;

static void panic_sov_handler(zis_t z, int c) {
    (void)z;
    zis_test_log(ZIS_TEST_LOG_TRACE, "panic code=%i", c);
    longjmp(test_at_panic_jb, 1);
}

zis_test_define(test_at_panic, z) {
    bool panicked = false;
    zis_at_panic(z, panic_sov_handler);
    if (!setjmp(test_at_panic_jb)) {
        zis_native_block(z, SIZE_MAX - 1, NULL, NULL); // triggers stack overflow
        zis_test_assert(false);
    } else {
        panicked = true;
        zis_at_panic(z, NULL);
    }
    zis_test_assert(panicked);
}

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

static void do_test_symbol(zis_t z, const char *str_in) {
    int status;
    const size_t str_in_sz = strlen(str_in);
    char buffer[64];
    size_t out_sz;

    status = zis_make_symbol(z, 1, str_in, str_in_sz);
    zis_test_assert_eq(status, ZIS_OK);
    out_sz = sizeof buffer;
    status = zis_read_symbol(z, 1, buffer, &out_sz);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(out_sz, str_in_sz);
    zis_test_assert_eq(memcmp(str_in, buffer, str_in_sz), 0);
}

zis_test_define(test_symbol, z) {
    do_test_symbol(z, "Hello, World!");
    do_test_symbol(z, "12345678");
    do_test_symbol(z, "");
}

zis_test_define(test_exception, z) {
    int status;
    const char *type = "test";
    const char *what = "Hello!";
    char buffer[16];
    size_t size;
    bool v_bool;

    zis_load_bool(z, 0, true);
    status = zis_make_exception(z, 0, type, 0, "%s", what);
    zis_test_assert_eq(status, ZIS_OK);

    status = zis_read_exception(z, 0, 1, 2, 3);
    zis_test_assert_eq(status, ZIS_OK);

    size = sizeof buffer;
    status = zis_read_symbol(z, 1, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    const size_t type_strlen = strlen(type);
    zis_test_assert_eq(size, type_strlen);
    zis_test_assert_eq(memcmp(buffer, type, type_strlen), 0);

    v_bool = false;
    status = zis_read_bool(z, 2, &v_bool);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_bool, true);

    size = sizeof buffer;
    status = zis_read_string(z, 3, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    const size_t what_strlen = strlen(what);
    zis_test_assert_eq(size, what_strlen);
    zis_test_assert_eq(memcmp(buffer, what, what_strlen), 0);

    zis_load_nil(z, REG_MAX - 2, 3);
}

static void do_test_make_values__basic(zis_t z) {
    int status;
    const int64_t rand_num = 13579;
    const bool in_bool = true;
    const int64_t in_i64 = 24680;
    const double in_double = 3.14;
    const char *const in_str = "Hello, World!";
    bool v_bool;
    int64_t v_i64;
    double v_double;
    char v_str[64];
    size_t v_size;

    zis_make_int(z, 20, rand_num);
    status = zis_make_values(
        z, 1, "%nxifs(ifs)[ifs][*i]{isis}y",
        // CNT 1234567890 1234 5 6 78901 2
        // REG 1234567    8    9   0     1
        20, in_bool, in_i64, in_double, in_str, (size_t)-1,
        in_i64, in_double, in_str, (size_t)-1,
        in_i64, in_double, in_str, (size_t)-1,
        (size_t)100, in_i64,
        1, "1", 1, 2, "2", 1,
        in_str, (size_t)-1
    );
    zis_test_assert_eq(status, 22);

    status = zis_read_int(z, 1, &v_i64);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_i64, rand_num);

    status = zis_read_nil(z, 2);
    zis_test_assert_eq(status, ZIS_OK);

    status = zis_read_bool(z, 3, &v_bool);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_bool, in_bool);

    status = zis_read_int(z, 4, &v_i64);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_i64, in_i64);

    status = zis_read_float(z, 5, &v_double);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_double, in_double);

    v_size = sizeof v_str;
    status = zis_read_string(z, 6, v_str, &v_size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_size, strlen(in_str));
    zis_test_assert_eq(memcmp(v_str, in_str, v_size), 0);

    for (unsigned int reg = 7; reg <= 8; reg++) {
        zis_make_int(z, 0, 1);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_int(z, 0, &v_i64);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v_i64, in_i64);

        zis_make_int(z, 0, 2);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_float(z, 0, &v_double);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v_double, in_double);

        zis_make_int(z, 0, 3);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_OK);
        v_size = sizeof v_str;
        status = zis_read_string(z, 0, v_str, &v_size);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v_size, strlen(in_str));
        zis_test_assert_eq(memcmp(v_str, in_str, v_size), 0);

        zis_make_int(z, 0, 4);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_E_ARG); // out of range
    }

    {
        const unsigned int reg = 9;

        zis_make_int(z, 0, 1);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_int(z, 0, &v_i64);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v_i64, in_i64);

        zis_make_int(z, 0, 2);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_E_ARG); // out of range
    }

    {
        const unsigned int reg = 10;

        for (int i = 1; i <= 2; i++) {
            zis_make_int(z, 0, i);
            status = zis_load_element(z, reg, 0, 0);
            zis_test_assert_eq(status, ZIS_OK);
            v_size   = sizeof v_str;
            status = zis_read_string(z, 0, v_str, &v_size);
            zis_test_assert_eq(status, ZIS_OK);
            zis_test_assert_eq(v_size, 1);
            zis_test_assert_eq(v_str[0], '0' + i);
        }

        zis_make_int(z, 0, -1);
        status = zis_load_element(z, reg, 0, 0);
        zis_test_assert_eq(status, ZIS_E_ARG); // key not found
    }

    v_size = sizeof v_str;
    status = zis_read_symbol(z, 11, v_str, &v_size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(v_size, strlen(in_str));
    zis_test_assert_eq(memcmp(v_str, in_str, v_size), 0);
}

static void do_test_make_values__insufficient_regs(zis_t z) {
    int status;

    status = zis_make_values(z, REG_MAX + 1, "n");
    zis_test_assert_eq(status, ZIS_E_IDX);

    status = zis_make_values(z, REG_MAX, "n");
    zis_test_assert_eq(status, 1);

    status = zis_make_values(z, REG_MAX, "nn");
    zis_test_assert_eq(status, 1);
}

static void do_test_make_values__nested_collections(zis_t z) {
    int status;

    status = zis_make_values(z, 1, "(())");
    zis_test_assert_eq(status, ZIS_E_ARG);

    status = zis_make_values(z, 1, "[()]");
    zis_test_assert_eq(status, ZIS_E_ARG);

    status = zis_make_values(z, 1, "[[]]");
    zis_test_assert_eq(status, ZIS_E_ARG);

    status = zis_make_values(z, 1, "{{}}");
    zis_test_assert_eq(status, ZIS_E_ARG);
}

zis_test_define(test_make_values, z) {
    do_test_make_values__basic(z);
    do_test_make_values__insufficient_regs(z);
    do_test_make_values__nested_collections(z);
}

static void do_test_read_values__basic(zis_t z) {
    int status;
    const bool in_bool = true;
    const int64_t in_i64 = 24680;
    const double in_double = 3.14;
    const char *const in_str = "Hello, World!";
    bool v_bool;
    int64_t v_i64;
    double v_double;
    char v_str[64];
    size_t v_size;

    zis_load_bool(z, 1, in_bool);
    zis_make_int(z, 2, in_i64);
    zis_make_float(z, 3, in_double);
    zis_make_string(z, 4, in_str, (size_t)- 1);

    v_size = sizeof v_str;
    status = zis_read_values(z, 1, "xifs", &v_bool, &v_i64, &v_double, &v_str, &v_size);
    zis_test_assert_eq(status, 4);
    zis_test_assert_eq(v_bool, in_bool);
    zis_test_assert_eq(v_i64, in_i64);
    zis_test_assert_eq(v_double, in_double);
    zis_test_assert_eq(v_size, strlen(in_str));
    zis_test_assert_eq(memcmp(v_str, in_str, v_size), 0);

    zis_make_values(z, 1, "(if)[if]", in_i64, in_double, in_i64, in_double);

    status = zis_read_values(z, 1, "(*if)", &v_size, &v_i64, &v_double);
    zis_test_assert_eq(status, 3);
    zis_test_assert_eq(v_size, 2);
    zis_test_assert_eq(v_i64, in_i64);
    zis_test_assert_eq(v_double, in_double);
    status = zis_read_values(z, 2, "[*if]", &v_size, &v_i64, &v_double);
    zis_test_assert_eq(status, 3);
    zis_test_assert_eq(v_size, 2);
    zis_test_assert_eq(v_i64, in_i64);
    zis_test_assert_eq(v_double, in_double);
}

static void do_test_read_values__ignore_type_err(zis_t z) {
    int status;
    const int64_t in[2] = { 6, 10 };
    int64_t v[2] = { 0, 0 };

    zis_make_values(z, 1, "nn");
    status = zis_read_values(z, 1, "ii", &v[0], &v[1]);
    zis_test_assert_eq(status, ZIS_E_TYPE);

    v[0] = in[0], v[1] = in[1];
    status = zis_read_values(z, 1, "?ii", &v[0], &v[1]);
    zis_test_assert_eq(status, 2);
    zis_test_assert_eq(v[0], in[0]);
    zis_test_assert_eq(v[1], in[1]);

    zis_make_values(z, 1, "in", in[0]);
    v[0] = in[0], v[1] = in[1];
    status = zis_read_values(z, 1, "i?i", &v[0], &v[1]);
    zis_test_assert_eq(status, 2);
    zis_test_assert_eq(v[0], in[0]);
    zis_test_assert_eq(v[1], in[1]);

    zis_make_values(z, 1, "ff", 0.0, 0.0);
    status = zis_read_values(z, 1, "?ii", &v[0], &v[1]);
    zis_test_assert_eq(status, ZIS_E_TYPE);
}

zis_test_define(test_read_values, z) {
    do_test_read_values__basic(z);
    do_test_read_values__ignore_type_err(z);
}

// zis-api-code //

static int F_add_int(zis_t z) {
    int64_t lhs, rhs;
    if (zis_read_values(z, 1, "ii", &lhs, &rhs) != 2) {
        zis_make_exception(z, 0, "type", (unsigned int)-1, "wrong argument types");
        return ZIS_THR;
    }
    zis_make_int(z, 0, lhs + rhs);
    return ZIS_OK;
}

static void do_test_function__check_exception(zis_t z, unsigned reg, const char *type) {
    int status;
    char buffer[128];
    size_t size;

    status = zis_read_exception(z, reg, REG_MAX - 3, REG_MAX - 2, REG_MAX - 1);
    zis_test_assert_eq(status, ZIS_OK);

    size = sizeof buffer;
    status = zis_read_symbol(z, REG_MAX - 3, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    const size_t type_strlen = strlen(type);
    zis_test_assert_eq(size, type_strlen);
    zis_test_assert_eq(memcmp(buffer, type, type_strlen), 0);

    size = sizeof buffer;
    status = zis_read_string(z, REG_MAX - 1, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_log(ZIS_TEST_LOG_TRACE, "exception (%s): %.*s", type, (int)size, buffer);

    zis_load_nil(z, REG_MAX - 3, 3);
}

static void do_test_function__F_add_int(zis_t z) {
    int status;
    int64_t v_i64;
    struct zis_native_func_def func_def;

    // make function
    func_def.name = NULL;
    func_def.meta = (struct zis_func_meta){ 2, 0, 3 };
    func_def.code = F_add_int;
    status = zis_make_function(z, 1, &func_def);
    zis_test_assert_eq(status, ZIS_OK);

    // call
    for (int64_t i = -10; i <= 10; i++) {
        for (int64_t j = -10; j <= 10; j++) {
            const int64_t k = i + j;
            zis_make_values(z, 2, "ii", i, j);
            // #1
            status = zis_invoke(z, (unsigned[]){0, 1, 2, 3}, 2);
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_read_int(z, 0, &v_i64);
            zis_test_assert_eq(status, ZIS_OK);
            zis_test_assert_eq(v_i64, k);
            // #2
            status = zis_invoke(z, (unsigned[]){0, 1, 2, (unsigned)-1}, 2);
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_read_int(z, 0, &v_i64);
            zis_test_assert_eq(status, ZIS_OK);
            zis_test_assert_eq(v_i64, k);
            // #3
            zis_make_values(z, 4, "(%%)", 2, 3);
            status = zis_invoke(z, (unsigned[]){0, 1, 4}, (size_t)-1);
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_read_int(z, 0, &v_i64);
            zis_test_assert_eq(status, ZIS_OK);
            zis_test_assert_eq(v_i64, k);
        }
    }

    // wrong argc
    zis_make_values(z, 2, "iii", 0, 0, 0);
    status = zis_invoke(z, (unsigned[]){0, 1, 2, (unsigned)-1}, 3);
    zis_test_assert_eq(status, ZIS_THR);
    do_test_function__check_exception(z, 0, "type");

    // throws exception
    zis_make_values(z, 2, "if", INT64_C(1), 2.0);
    status = zis_invoke(z, (unsigned[]){0, 1, 2, 3}, 2);
    zis_test_assert_eq(status, ZIS_THR);
    do_test_function__check_exception(z, 0, "type");
}

static void do_test_function__not_callable(zis_t z) {
    int status;
    zis_load_nil(z, 1, 1);
    status = zis_invoke(z, (unsigned[]){0, 1}, 0);
    zis_test_assert_eq(status, ZIS_THR);
}

zis_test_define(test_function, z) {
    do_test_function__F_add_int(z);
    do_test_function__not_callable(z);
}

zis_test_define(test_module, z) {
    int status;

    // Create a module.
    const struct zis_native_func_def mod_funcs[] = {
        { "add_int", { 2, 0, 3 }, F_add_int },
        { NULL, {0}, NULL },
    };
    const struct zis_native_type_def mod_types[] = {
        { "some_type", 0, 0, NULL, NULL, NULL },
        { NULL, 0, 0, NULL, NULL, NULL },
    };
    const struct zis_native_module_def mod_def = {
        .name = NULL,
        .functions = mod_funcs,
        .types = mod_types,
    };
    status = zis_make_module(z, 1, &mod_def);
    zis_test_assert_eq(status, ZIS_OK);

    // Read pre-defined variables.
    status = zis_load_field(z, 1, "add_int", (size_t)-1, 0);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_load_field(z, 1, "some_type", (size_t)-1, 0);
    zis_test_assert_eq(status, ZIS_OK);

    // Set and get variables.
    status = zis_load_field(z, 1, "num", -1, 0);
    zis_test_assert_eq(status, ZIS_E_ARG);
    for (int64_t i = 100; i < 110; i++) {
        int64_t v_i64;
        zis_make_int(z, 0, i);
        status = zis_store_field(z, 1, "num", (size_t)-1, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_load_field(z, 1, "num", (size_t)-1, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_int(z, 0, &v_i64);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(i, v_i64);
    }
}

// zis-api-variables //

static void do_test_load_element__array_and_tuple(zis_t z) {
    int status;

    const double in[] = { 0.618, 2.71, 3.14 };
    double v_double;

    status = zis_make_values(z, 1, "(fff)[fff]", in[0], in[1], in[2], in[0], in[1], in[2]);
    zis_test_assert_eq(status, 8);

    for (unsigned int i = 1; i <= 2; i++) {
        for (int j = -5; j <= 5; j++) {
            const int jx = j >= 0 ? j : (3 + 1) + j;
            status = zis_make_int(z, 0, j); // index
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_load_element(z, i, 0, 0);
            if (1 <= jx && jx <= 3) {
                zis_test_assert_eq(status, ZIS_OK);
                status = zis_read_float(z, 0, &v_double);
                zis_test_assert_eq(status, ZIS_OK);
                zis_test_assert_eq(v_double, in[jx - 1]);
            } else {
                zis_test_assert_eq(status, ZIS_E_ARG); // out of range
            }
        }
    }
}

static void do_test_load_element__map(zis_t z) {
    int status;

    const double in[] = { 0.618, 2.71, 3.14 };
    double v_double;

    status = zis_make_values(z, 1, "{ififif}", 0LL, in[0], 1LL, in[1], 2LL, in[2]);
    zis_test_assert_eq(status, 7);

    for (int i = 0; i < 5; i++) {
        status = zis_make_int(z, 0, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_load_element(z, 1, 0, 0);
        if (i < 3) {
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_read_float(z, 0, &v_double);
            zis_test_assert_eq(status, ZIS_OK);
            zis_test_assert_eq(v_double, in[i]);
        } else {
            zis_test_assert_eq(status, ZIS_E_ARG);
        }
    }
}

static void do_test_load_element__bad_type(zis_t z) {
    int status;

    status = zis_load_nil(z, 1, 1);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_make_int(z, 0, 1);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_load_element(z, 1, 0, 0);
    zis_test_assert_eq(status, ZIS_E_TYPE);
}

zis_test_define(test_load_element, z) {
    do_test_load_element__array_and_tuple(z);
    do_test_load_element__map(z);
    do_test_load_element__bad_type(z);
}

static void do_test_store_element__array_and_tuple(zis_t z) {
    int status;

    const double in[] = { 0.618, 2.71, 3.14 };

    status = zis_make_values(z, 1, "(nnn)[nnn]");
    zis_test_assert_eq(status, 8);
    for (unsigned int i = 1; i <= 2; i++) {
        for (int j = 1; j <= 5; j++) {
            status = zis_make_int(z, 0, j); // index
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_make_float(z, 3, in[j > 3 ? 0 : j - 1]);
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_store_element(z, i, 0, 3);
            zis_test_assert_eq(status, i == 1 ? ZIS_E_TYPE : j > 3 ? ZIS_E_ARG : ZIS_OK);
        }
    }
    {
        double v[3];
        status = zis_read_values(z, 1, "(nnn)[fff]", &v[0], &v[1], &v[2]);
        zis_test_assert_eq(status, 6);
        for (int i = 0; i < 3; i++)
            zis_test_assert_eq(in[i], v[i]);
    }
}

static void do_test_store_element__map(zis_t z) {
    int status;

    const double in[] = { 0.618, 2.71, 3.14 };

    status = zis_make_values(z, 1, "{}");
    zis_test_assert_eq(status, 1);

    for (int i = 0; i < 3; i++) {
        status = zis_make_int(z, 0, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_store_element(z, 1, 0, 0);
        zis_test_assert_eq(status, ZIS_OK);
    }

    for (int i = 0; i < 3; i++) {
        status = zis_make_int(z, 0, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_make_float(z, 2, in[i]);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_store_element(z, 1, 0, 2);
        zis_test_assert_eq(status, ZIS_OK);
    }

    for (int i = 0; i < 3; i++) {
        double v;
        status = zis_make_int(z, 0, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_load_element(z, 1, 0, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_float(z, 0, &v);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v, in[i]);
    }
}

static void do_test_store_element__bad_type(zis_t z) {
    int status;
    status = zis_load_nil(z, 1, 1);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_make_int(z, 0, 1);
    zis_test_assert_eq(status, ZIS_OK);
    status = zis_store_element(z, 1, 0, 0);
    zis_test_assert_eq(status, ZIS_E_TYPE);
}

zis_test_define(test_store_element, z) {
    do_test_store_element__array_and_tuple(z);
    do_test_store_element__map(z);
    do_test_store_element__bad_type(z);
}

static void do_test_insert_element__array(zis_t z) {
    const struct case_ { int64_t init_val[3]; int64_t ins_pos; int64_t ins_val; }
    cases[] = {
        { { 2, 3, 4 }, 1, 1 },
        { { 2, 3, 4 }, -4, 1 },
        { { 1, 3, 4 }, 2, 2 },
        { { 1, 3, 4 }, -3, 2 },
        { { 1, 2, 4 }, 3, 3 },
        { { 1, 2, 4 }, -2, 3 },
        { { 1, 2, 3 }, 4, 4 },
        { { 1, 2, 3 }, -1, 4 },
        { { 1, 2, 3 }, 0, 0 },
        { { 1, 2, 3 }, 5, 0 },
        { { 1, 2, 3 }, -5, 0 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int status;
        const struct case_ *c = &cases[i];
        zis_load_nil(z, 1, 1);
        zis_make_values(
            z, 1, "[iii]ii",
            c->init_val[0], c->init_val[1], c->init_val[2],
            c->ins_pos, c->ins_val
        );
        status = zis_insert_element(z, 1, 2, 3);
        if (!c->ins_val) {
            zis_test_assert_eq(status, ZIS_E_ARG);
            continue;
        }
        zis_test_assert_eq(status, ZIS_OK);
        int64_t v[4] = { 0, 0, 0, 0 };
        size_t n = 0;
        status = zis_read_values(z, 1, "[*iiii]", &n, &v[0], &v[1], &v[2], &v[3]);
        zis_test_assert_eq(status, 5);
        zis_test_assert_eq(n, 4);
        for (int j = 0; j < 4; j++)
            zis_test_assert_eq(v[j], j + 1);
    }
}

zis_test_define(test_insert_element, z) {
    do_test_insert_element__array(z);
}

static void do_test_remove_element__array(zis_t z) {
    const struct case_ { int64_t init_val[3]; int64_t rm_pos; int status; }
    cases[] = {
        { { 5, 1, 2 }, 1, ZIS_OK },
        { { 5, 1, 2 }, -3, ZIS_OK },
        { { 1, 5, 2 }, 2, ZIS_OK },
        { { 1, 5, 2 }, -2, ZIS_OK },
        { { 1, 2, 5 }, 3, ZIS_OK },
        { { 1, 2, 5 }, -1, ZIS_OK },
        { { 1, 2, 3 }, 0, ZIS_E_ARG },
        { { 1, 2, 3 }, 4, ZIS_E_ARG },
        { { 1, 2, 3 }, -4, ZIS_E_ARG },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int status;
        const struct case_ *c = &cases[i];
        zis_load_nil(z, 1, 1);
        zis_make_values(
            z, 1, "[iii]ii",
            c->init_val[0], c->init_val[1], c->init_val[2],
            c->rm_pos
        );
        status = zis_remove_element(z, 1, 2);
        zis_test_assert_eq(status, c->status);
        if (c->status == ZIS_OK) {
            int64_t v[2] = { 0, 0 };
            size_t n = 0;
            status = zis_read_values(z, 1, "[*ii]", &n, &v[0], &v[1]);
            zis_test_assert_eq(status, 3);
            zis_test_assert_eq(n, 2);
            for (int j = 0; j < 2; j++)
                zis_test_assert_eq(v[j], j + 1);
        }
    }
}

static void do_test_remove_element__map(zis_t z) {
    const int N = 200;
    int status;

    status = zis_make_values(z, 1, "{}");
    zis_test_assert_eq(status, 1);

    for (int i = 0; i < N; i++) {
        status = zis_make_int(z, 2, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_make_int(z, 3, -i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_store_element(z, 1, 2, 3);
        zis_test_assert_eq(status, ZIS_OK);
    }

    for (int i = 0; i < N; i += 2) {
        status = zis_make_int(z, 2, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_remove_element(z, 1, 2);
        zis_test_assert_eq(status, ZIS_OK);
    }

    for (int i = 0; i < N; i++) {
        status = zis_make_int(z, 2, i);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_load_element(z, 1, 2, 0);
        if (i & 1) {
            int64_t v;
            zis_test_assert_eq(status, ZIS_OK);
            status = zis_read_int(z, 0, &v);
            zis_test_assert_eq(status, ZIS_OK);
            zis_test_assert_eq(v, -i);
        } else {
            zis_test_assert_eq(status, ZIS_E_ARG);
        }
    }
}

zis_test_define(test_remove_element, z) {
    do_test_remove_element__array(z);
    do_test_remove_element__map(z);
}

// main

zis_test_list(
    REG_MAX,
    // zis-api-context //
    test_at_panic,
    // zis-api-native //
    test_native_block,
    // zis-api-values //
    test_nil,
    test_bool,
    test_int,
    test_float,
    test_string,
    test_symbol,
    test_exception,
    test_make_values,
    test_read_values,
    // zis-api-code //
    test_function,
    test_module,
    // zis-api-variables //
    test_load_element,
    test_store_element,
    test_insert_element,
    test_remove_element,
)
