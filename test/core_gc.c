#include "test.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define REG_MAX 200
#define TMP_REG_MAX 4

static void clear_stack(zis_t z) {
    zis_load_nil(z, TMP_REG_MAX + 1, (size_t)-1);
}

static void clear_stack_tmp(zis_t z) {
    zis_load_nil(z, 1, TMP_REG_MAX);
}

static void make_random_data(zis_t z, int64_t seed) {
    const int N = 200;
    int status;

    zis_make_values(z, 1, "[*]", (size_t)N);
    for (int i = 0; i < N; i++) {
        char buffer[64];
        snprintf(buffer, sizeof buffer, "<<<<<<<< No. %" PRIi64 "-%i >>>>>>>>", seed, i);
        status = zis_make_values(
            z, 2, "(nxifs)",
            (bool)(i & 1), seed + i, (double)(seed + i), buffer, (size_t)-1
        );
        zis_test_assert_eq(status, 6);
        status = zis_make_int(z, 0, -1);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_insert_element(z, 1, 0, 2);
        zis_test_assert_eq(status, ZIS_OK);
    }

    zis_move_local(z, 0, 1);
    clear_stack_tmp(z);
}

static void check_random_data(zis_t z, int64_t seed) {
    const int N = 200;

    zis_move_local(z, 1, 0);
    {
        size_t n = 0;
        int status =  zis_read_values(z, 1, "[*]", &n);
        zis_test_assert_eq(status, 1);
        zis_test_assert_eq(n, (size_t)N);
    }

    for (int i = 0; i < N; i++) {
        zis_make_int(z, 0, i + 1);
        int status = zis_load_element(z, 1, 0, 2);
        zis_test_assert_eq(status, ZIS_OK);

        size_t v_size = 0;
        bool v_bool = false;
        int64_t v_i64 = 0;
        double v_double = 0.0;
        char v_strbuf[64] = { 0 };
        size_t v_strlen = sizeof v_strbuf;
        status = zis_read_values(
            z, 2, "(*nxifs)",
            &v_size, &v_bool, &v_i64, &v_double, &v_strbuf, &v_strlen
        );
        zis_test_assert_eq(status, 6);

        char buffer[64];
        snprintf(buffer, sizeof buffer, "<<<<<<<< No. %" PRIi64 "-%i >>>>>>>>", seed, i);
        zis_test_assert_eq(v_size, 5);
        zis_test_assert_eq(v_bool, (bool)(i & 1));
        zis_test_assert_eq(v_i64, seed + i);
        zis_test_assert_eq(v_double, (double)(seed + i));
        zis_test_assert_eq(v_strlen, strlen(buffer));
        zis_test_assert_eq(memcmp(v_strbuf, buffer, v_strlen), 0);
    }

    clear_stack_tmp(z);
}

static char long_str_buf[64 * 1024]; // Is it big enough to be allocated in big space?
static char long_str_buf2[sizeof long_str_buf];

static void make_random_large_object(zis_t z, int64_t seed) {
    if (!long_str_buf[0])
        memset(long_str_buf, '~', sizeof long_str_buf);
    char buffer[64];
    snprintf(buffer, 64, "%063ji", seed);
    memcpy(long_str_buf, buffer, 63);
    int status = zis_make_string(z, 0, long_str_buf, sizeof long_str_buf);
    zis_test_assert_eq(status, ZIS_OK);
}

static void check_random_large_object(zis_t z, int64_t seed) {
    assert(long_str_buf[0]);
    char buffer[64];
    snprintf(buffer, 64, "%063ji", seed);
    memcpy(long_str_buf, buffer, 63);

    size_t size = sizeof long_str_buf2;
    int status = zis_read_string(z, 0, long_str_buf2, &size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(size, sizeof long_str_buf2);
    zis_test_assert_eq(memcmp(long_str_buf2, long_str_buf, size), 0);
}

zis_test_define(test_self_check, z) {
    make_random_data(z, 0);
    check_random_data(z, 0);
    make_random_large_object(z, 0);
    check_random_large_object(z, 0);
    clear_stack_tmp(z);
    clear_stack(z);
}

zis_test_define(test_all_garbage, z) {
    const unsigned long N = 100000;

    for (unsigned long i = 0; i < N; i++) {
        int status = zis_make_float(z, 0, (double)i);
        zis_test_assert_eq(status, ZIS_OK);
    }

    clear_stack_tmp(z);
}

zis_test_define(test_massive_garbage, z) {
    const int64_t N = 1000;

    make_random_data(z, N);
    zis_move_local(z, TMP_REG_MAX + 1, 0);
    check_random_data(z, N);

    for (int64_t i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%" PRIi64, i);
        make_random_data(z, i);
        check_random_data(z, i);
    }

    zis_move_local(z, 0, TMP_REG_MAX + 1);
    check_random_data(z, N);

    clear_stack_tmp(z);
    clear_stack(z);
}

zis_test_define(test_massive_survivors, z) {
    const int64_t N = 1000, M = REG_MAX - TMP_REG_MAX - 1;

    for (int64_t j = 0; j < M; j++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "j=%" PRIi64, j);
        make_random_data(z, j);
        zis_move_local(z, TMP_REG_MAX + 1 + j, 0);
        check_random_data(z, j);
    }

    for (int64_t i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%" PRIi64, i);
        make_random_data(z, i);
        check_random_data(z, i);
    }

    for (int64_t j = 0; j < M; j++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "j=%" PRIi64, j);
        zis_move_local(z, 0, TMP_REG_MAX + 1 + j);
        check_random_data(z, j);
    }

    clear_stack_tmp(z);
    clear_stack(z);
}

zis_test_define(test_large_object, z) {
    const int64_t N = 200;

    make_random_data(z, N);
    zis_move_local(z, TMP_REG_MAX + 1, 0);
    check_random_data(z, N);
    make_random_large_object(z, N);
    zis_move_local(z, TMP_REG_MAX + 2, 0);
    check_random_large_object(z, N);

    for (int i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%i", i);
        make_random_data(z, i);
        check_random_data(z, i);
        make_random_large_object(z, i);
        check_random_large_object(z, i);
    }

    zis_move_local(z, 0, TMP_REG_MAX + 2);
    check_random_large_object(z, N);
    zis_move_local(z, 0, TMP_REG_MAX + 1);
    check_random_data(z, N);

    clear_stack_tmp(z);
    clear_stack(z);
}

zis_test_define(test_complex_references, z) {
    const int64_t N = 100;
    const unsigned reg = TMP_REG_MAX + 1;

    zis_make_values(z, reg, "[]");

    for (int64_t i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%" PRIi64, i);
        make_random_data(z, i);
        check_random_data(z, i);
    }

    zis_make_int(z, 1, -1);
    for (int64_t i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%" PRIi64, i);
        zis_make_float(z, 0, (double)i);
        zis_insert_element(z, reg, 1, 0);
    }
    clear_stack_tmp(z);

    for (int64_t i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%" PRIi64, i);
        make_random_data(z, i);
        check_random_data(z, i);
    }

    for (int64_t i = 0; i < N; i++) {
        zis_test_log(ZIS_TEST_LOG_TRACE, "i=%" PRIi64, i);
        int status;
        double v = 0.0;
        zis_make_int(z, 1, i + 1);
        status = zis_load_element(z, reg, 1, 0);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_float(z, 0, &v);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v, (double)i);
    }
    clear_stack_tmp(z);

    clear_stack(z);
}

zis_test_list(
    REG_MAX,
    test_self_check,
    test_all_garbage,
    test_massive_garbage,
    test_massive_survivors,
    test_large_object,
    test_complex_references,
)
