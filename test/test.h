// Testing utilities.

#pragma once

#include <stddef.h>

#include "../include/zis.h"
#include "../core/attributes.h"

/* ----- test-case definitions ---------------------------------------------- */

/// Test function identifier.
#define zis_test_func_name(__name) \
    __test_case_##__name##_func

/// Define a test function.
#define zis_test_define(__name, __zis_context_var) \
    static void zis_test_func_name(__name)(struct zis_context * __zis_context_var)
// ^^^ zis_test_define() ^^^

#define zis_test_case(__name) \
    { #__name, zis_test_func_name(__name) }

/// Enumerate test cases. See `zis_test_case()`.
#define zis_test_list(__name, __reg_max, ...) \
    static const struct __zis_test_entry      \
    __test_##__name##_entries[] = { __VA_ARGS__ {NULL, NULL} }; \
    int __name(int argc, char *argv[]) {      \
        struct __zis_test_fn_state state = { argc, argv, __test_##__name##_entries }; \
        zis_t z = zis_create();               \
        int exit_status = zis_native_block(z, (__reg_max), __zis_test_fn, &state); \
        zis_destroy(z);                       \
        return exit_status;                   \
    }                                         \
// ^^^ zis_test_list() ^^^

struct __zis_test_entry {
    const char *name;
    void (*func)(struct zis_context *);
};

struct __zis_test_fn_state {
    int argc;
    char **argv;
    const struct __zis_test_entry *entries;
};

int __zis_test_fn(zis_t, void *);

/// Test function identifier.
#define zis_test0_func_name(__name) \
    __test0_case_##__name##_func

/// Define a test function.
#define zis_test0_define(__name) \
    static void zis_test0_func_name(__name)(void)
// ^^^ zis_test_define() ^^^

#define zis_test0_case(__name) \
    { #__name, zis_test0_func_name(__name) }

/// Enumerate test cases. See `zis_test0_case()`.
#define zis_test0_list(__name, ...)       \
    static const struct __zis_test0_entry \
    __test_##__name##_entries[] = { __VA_ARGS__ {NULL, NULL} }; \
    int __name(int argc, char *argv[]) {  \
        return __zis_test0_run(__test_##__name##_entries, argc, argv); \
    }                                     \
// ^^^ zis_test_list() ^^^

struct __zis_test0_entry {
    const char *name;
    void (*func)(void);
};

int __zis_test0_run(const struct __zis_test0_entry *, int, char *[]);

/* ----- logging ------------------------------------------------------------ */

/// Log levels.
enum zis_test_log_level {
    ZIS_TEST_LOG_ERROR,
    ZIS_TEST_LOG_STATUS,
    ZIS_TEST_LOG_TRACE,
};

/// Print log with location and function name.
#define zis_test_log(__level, ...) \
    __zis_test_log((__level), __FILE__, __LINE__, __func__, __VA_ARGS__)

zis_printf_fn_attrs(5, 6) void __zis_test_log(
    int level, const char *file, unsigned int line, const char *func,
    const char *restrict zis_printf_fn_arg_fmtstr(fmt), ...
);

/* ----- assertions --------------------------------------------------------- */

/// Always fail.
#define zis_test_fail(...) \
do { \
    zis_test_log(ZIS_TEST_LOG_ERROR, __VA_ARGS__); \
    __zis_test_post_failure(); \
} while (0)

/// Assertion.
#define zis_test_assert(__expr) \
do { \
    if (!(__expr)) \
        __zis_test_assert_fail(__FILE__, __LINE__, __func__, #__expr); \
} while (0)

#if (!__STDC__ || __STDC_VERSION__ < 201112L)

/// Assertion: equal.
#define zis_test_assert_eq(__lhs, __rhs) \
    zis_test_assert((__lhs) == (__rhs))

#else // C11

/// Assertion: equal.
#define zis_test_assert_eq(__lhs, __rhs) \
do { \
    if ((__lhs) == (__rhs)) \
        break; \
    _Generic( \
        (__lhs), \
        bool              : __zis_test_assert_eq_fail_b, \
        char              : __zis_test_assert_eq_fail_i, \
        /* signed char  : __zis_test_assert_eq_fail_i, */\
        /* unsigned char: __zis_test_assert_eq_fail_u, */\
        signed short      : __zis_test_assert_eq_fail_i, \
        unsigned short    : __zis_test_assert_eq_fail_u, \
        signed int        : __zis_test_assert_eq_fail_i, \
        unsigned int      : __zis_test_assert_eq_fail_u, \
        signed long       : __zis_test_assert_eq_fail_i, \
        unsigned long     : __zis_test_assert_eq_fail_u, \
        signed long long  : __zis_test_assert_eq_fail_i, \
        unsigned long long: __zis_test_assert_eq_fail_u, \
        float             : __zis_test_assert_eq_fail_f, \
        double            : __zis_test_assert_eq_fail_f, \
        void *            : __zis_test_assert_eq_fail_p, \
        default           : __zis_test_assert_eq_fail_p  \
    )(__FILE__, __LINE__, __func__, #__lhs, #__rhs, (__lhs), (__rhs)); \
} while (0)

#endif // C11

zis_noreturn void __zis_test_post_failure(void);
zis_noreturn void __zis_test_assert_fail(const char *, unsigned int, const char *, const char *restrict);
zis_noreturn void __zis_test_assert_eq_fail_b(const char *, unsigned int, const char *, const char *, const char *, bool, bool);
zis_noreturn void __zis_test_assert_eq_fail_i(const char *, unsigned int, const char *, const char *, const char *, intmax_t, intmax_t);
zis_noreturn void __zis_test_assert_eq_fail_u(const char *, unsigned int, const char *, const char *, const char *, uintmax_t, uintmax_t);
zis_noreturn void __zis_test_assert_eq_fail_f(const char *, unsigned int, const char *, const char *, const char *, double, double);
zis_noreturn void __zis_test_assert_eq_fail_p(const char *, unsigned int, const char *, const char *, const char *, const void *, const void *);
