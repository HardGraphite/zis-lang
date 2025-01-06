#include "test.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "core/fsutil.c"

void *zis_mem_alloc(size_t sz) {
    return malloc(sz);
}

void zis_mem_free(void *p) {
    free(p);
}

static bool path_eq(const zis_path_char_t *a, const zis_path_char_t *b) {
    const size_t na = zis_path_len(a), nb = zis_path_len(b);
    if (na != nb)
        return false;
    return memcmp(a, b, na * sizeof *a) == 0;
}

static void do_test_path_func_1(
    const char *func_name, size_t (*func)(zis_path_char_t *, const zis_path_char_t *),
    const zis_path_char_t *path,
    const zis_path_char_t *expected_buf
) {
    zis_path_char_t buffer[64];
    memset(buffer, 0xff, sizeof buffer);
    const size_t func_ret = func(buffer, path);
    const bool ret_ok = func_ret == zis_path_len(buffer);
    const bool buf_ok = path_eq(buffer, expected_buf);
    zis_test_log(
        ZIS_TEST_LOG_TRACE,
        "%s(`%" ZIS_PATH_STR_PRI "`) -> %zu, `%" ZIS_PATH_STR_PRI "`",
        func_name, path, func_ret, buffer
    );
    zis_test_assert(ret_ok);
    zis_test_assert(buf_ok);
}

static void do_test_path_func_2(
    const char *func_name,
    size_t (*func)(zis_path_char_t *, const zis_path_char_t *, const zis_path_char_t *),
    const zis_path_char_t *path1, const zis_path_char_t *path2,
    const zis_path_char_t *expected_buf
) {
    zis_path_char_t buffer[64];
    memset(buffer, 0xff, sizeof buffer);
    const size_t func_ret = func(buffer, path1, path2);
    const bool ret_ok = func_ret == zis_path_len(buffer);
    const bool buf_ok = path_eq(buffer, expected_buf);
    zis_test_log(
        ZIS_TEST_LOG_TRACE,
        "%s(`%" ZIS_PATH_STR_PRI "`, `%" ZIS_PATH_STR_PRI "`) -> %zu, `%" ZIS_PATH_STR_PRI "`",
        func_name, path1, path2, func_ret, buffer
    );
    zis_test_assert(ret_ok);
    zis_test_assert(buf_ok);
}

static void do_test_path_len(const zis_path_char_t *path, size_t len) {
    const size_t n = zis_path_len(path);
    zis_test_log(ZIS_TEST_LOG_TRACE, "zis_path_len(`%" ZIS_PATH_STR_PRI "`) -> %zu", path, n);
    zis_test_assert_eq(n, len);
}

zis_test0_define(path_len) {
    do_test_path_len(ZIS_PATH_STR(""), 0);
    do_test_path_len(ZIS_PATH_STR("foo"), 3);
    do_test_path_len(ZIS_PATH_STR("foo/bar"), 7);
}

zis_test0_define(path_dup) {
    const zis_path_char_t *a = ZIS_PATH_STR("foo/bar");
    zis_path_char_t *a1 = zis_path_dup(a), *a2 = zis_path_dup_n(a, 7);
    zis_test_assert(path_eq(a, a1));
    zis_test_assert(path_eq(a, a2));
    zis_mem_free(a1), zis_mem_free(a2);
}

static int do_test_path_str_conv_1(const zis_path_char_t *a, void *b) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "%s -> %" ZIS_PATH_STR_PRI, (char *)b, a);
    zis_test_assert(path_eq(a, b));
    return 0;
}

static int do_test_path_str_conv_2(const char *a, void *b) {
    zis_test_log(ZIS_TEST_LOG_TRACE, "%" ZIS_PATH_STR_PRI " -> %s", (zis_path_char_t *)b, a);
    zis_test_assert(strcmp(a, b) == 0);
    return 0;
}

zis_test0_define(path_str_conv) {
    const zis_path_char_t *p = ZIS_PATH_STR("foo/bar");
    const char *s = "foo/bar";
    zis_path_with_temp_path_from_str(s, do_test_path_str_conv_1, (void *)p);
    zis_path_with_temp_str_from_path(p, do_test_path_str_conv_2, (void *)s);
}

zis_test0_define(path_copy) {
    zis_path_char_t buffer[32];
    const zis_path_char_t *a = ZIS_PATH_STR("foo/bar");
    memset(buffer, 0xff, sizeof buffer);
    zis_test_assert_eq(zis_path_copy(buffer, a), 7);
    zis_test_assert(path_eq(a, buffer));
    memset(buffer, 0xff, sizeof buffer);
    zis_path_copy_n(buffer, a, 8);
    zis_test_assert(path_eq(a, buffer));
}

zis_test0_define(path_concat) {
    zis_path_char_t buffer[32];
    const zis_path_char_t *a = ZIS_PATH_STR("foo");
    const zis_path_char_t *b = ZIS_PATH_STR("bar");
    const zis_path_char_t *ab = ZIS_PATH_STR("foo") ZIS_PATH_STR("bar");
    memset(buffer, 0xff, sizeof buffer);
    zis_test_assert_eq(zis_path_concat(buffer, a, b), 6);
    zis_test_assert(path_eq(ab, buffer));
    memset(buffer, 0xff, sizeof buffer);
    zis_test_assert_eq(zis_path_concat_n(buffer, a, 3, b, 3), 6);
    zis_test_assert(path_eq(ab, buffer));
}

zis_test0_define(path_join) {
    zis_path_char_t buffer[32];
    const zis_path_char_t *a = ZIS_PATH_STR("foo");
    const zis_path_char_t *b = ZIS_PATH_STR("bar");
    const zis_path_char_t *ab =
        ZIS_PATH_STR("foo") ZIS_PATH_PREFERRED_DIR_SEP_STR ZIS_PATH_STR("bar");
    memset(buffer, 0xff, sizeof buffer);
    zis_test_assert_eq(zis_path_join(buffer, a, b), 7);
    zis_test_assert(path_eq(ab, buffer));
    memset(buffer, 0xff, sizeof buffer);
    zis_test_assert_eq(zis_path_join_n(buffer, a, 3, b, 3), 7);
    zis_test_assert(path_eq(ab, buffer));
}

zis_test0_define(path_filename) {
#define DO_TEST(X, Y) \
    do_test_path_func_1("zis_path_filename", zis_path_filename, ZIS_PATH_STR( X ), ZIS_PATH_STR( Y ))

    DO_TEST("/foo/bar.txt", "bar.txt");
    DO_TEST("/foo/.bar", ".bar");
    DO_TEST("/foo/bar/", "");
    DO_TEST("/foo/.", ".");
    DO_TEST("/foo/..", "..");
    DO_TEST(".", ".");
    DO_TEST("..", "..");
    DO_TEST("/", "");
    DO_TEST("//host", "host");

#undef DO_TEST
}

zis_test0_define(path_stem) {
#define DO_TEST(X, Y) \
    do_test_path_func_1("zis_path_stem", zis_path_stem, ZIS_PATH_STR( X ), ZIS_PATH_STR( Y ))

    DO_TEST("/foo/bar.txt", "bar");
    DO_TEST("/foo/.bar", ".bar");
    DO_TEST("foo.bar.baz.tar", "foo.bar.baz");

#undef DO_TEST
}

zis_test0_define(path_extension) {
#define DO_TEST(X, Y) \
    do_test_path_func_1("zis_path_extension", zis_path_extension, ZIS_PATH_STR( X ), ZIS_PATH_STR( Y ))

    DO_TEST("/foo/bar.txt", ".txt");
    DO_TEST("/foo/bar.", ".");
    DO_TEST("/foo/bar", "");
    DO_TEST("/foo/bar.txt/bar.cc", ".cc");
    DO_TEST("/foo/bar.txt/bar.", ".");
    DO_TEST("/foo/bar.txt/bar", "");
    DO_TEST("/foo/.", "");
    DO_TEST("/foo/..", "");
    DO_TEST("/foo/.hidden", "");
    DO_TEST("/foo/..bar", ".bar");

#undef DO_TEST
}

zis_test0_define(path_parent) {
#define DO_TEST(X, Y) \
    do_test_path_func_1("zis_path_parent", zis_path_parent, ZIS_PATH_STR( X ), ZIS_PATH_STR( Y ))

    DO_TEST("/var/tmp/example.txt", "/var/tmp");
    DO_TEST("/var/tmp/.", "/var/tmp");
#ifndef _WIN32
    DO_TEST("/", "/");
#endif // _WIN32

#undef DO_TEST
}

zis_test0_define(path_with_extension) {
#define DO_TEST1(X, Z) \
    do_test_path_func_2("zis_path_with_extension", zis_path_with_extension, \
        ZIS_PATH_STR( X ), NULL, ZIS_PATH_STR( Z ))
#define DO_TEST2(X, Y, Z) \
    do_test_path_func_2("zis_path_with_extension", zis_path_with_extension, \
        ZIS_PATH_STR( X ), ZIS_PATH_STR( Y ), ZIS_PATH_STR( Z ))

    DO_TEST2("foo.txt", ".tar", "foo.tar");
    DO_TEST1("foo.txt", "foo");
    DO_TEST2("foo", ".txt", "foo.txt");
    DO_TEST1("foo", "foo");

#undef DO_TEST1
#undef DO_TEST2
}

zis_test0_list(
    core_fsutil,
    zis_test0_case(path_len),
    zis_test0_case(path_dup),
    zis_test0_case(path_str_conv),
    zis_test0_case(path_copy),
    zis_test0_case(path_concat),
    zis_test0_case(path_join),
    zis_test0_case(path_filename),
    zis_test0_case(path_stem),
    zis_test0_case(path_extension),
    zis_test0_case(path_parent),
    zis_test0_case(path_with_extension),
)
