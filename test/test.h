// Testing utilities.

#pragma once

#include <stddef.h>

#include "../include/zis.h"
#include "../core/attributes.h"

/// Type of a test function.
typedef void(*zis_test_func_t)(struct zis_context *);

/// Define a test function.
#define zis_test_define(__name, __zis_context_var) \
    static void __name ( struct zis_context * __zis_context_var )

/// Enumerate test functions defined by `zis_test_define()`.
#define zis_test_list(__reg_max, ...) \
    int main(void) {                  \
        zis_test_func_t f[] = { __VA_ARGS__ NULL }; \
        __zis_test_init();            \
        struct zis_context *const z = zis_create(); \
        zis_native_block(z, __reg_max, __zis_test_list_block, f); \
        zis_destroy(z);               \
    }                                 \
// ^^^ zis_test_list() ^^^

/// Type of a test0 function.
typedef void(*zis_test0_func_t)(void);

/// Define a test0 function.
#define zis_test0_define(__name) \
    static void __name (void)

/// Enumerate test0 functions defined by `zis_test0_define()`.
#define zis_test0_list(...) \
    int main(void) {        \
        zis_test0_func_t l[] = { __VA_ARGS__ NULL }; \
        __zis_test_init();  \
        for (zis_test0_func_t *p = l; *p; p++)       \
            (*p)();         \
    }                       \
// ^^^ zis_test0_list() ^^^

/// Log levels.
enum zis_test_log_level {
    ZIS_TEST_LOG_ERROR,
    ZIS_TEST_LOG_STATUS,
    ZIS_TEST_LOG_TRACE,
};

/// Print log with location and function name.
#define zis_test_log(__level, ...) \
    __zis_test_log((__level), __FILE__, __LINE__, __func__, __VA_ARGS__)

/// Assertion.
#define zis_test_assert(__expr) \
    do {                        \
        if (!(__expr))          \
            __zis_test_assertion_fail(__FILE__, __LINE__, __func__, #__expr); \
    } while (0)                 \
// ^^^ zis_test_assert() ^^^

/// Assertion: equal.
#define zis_test_assert_eq(__lhs, __rhs) \
    zis_test_assert((__lhs) == (__rhs))

/// Assertion: not equal.
#define zis_test_assert_ne(__lhs, __rhs) \
    zis_test_assert((__lhs) != (__rhs))

/* -------------------------------------------------------------------------- */

void __zis_test_init(void);

int __zis_test_list_block(zis_t z, void *_arg);

zis_printf_fn_attrs(5, 6) void __zis_test_log(
    int level, const char *file, unsigned int line, const char *func,
    const char *restrict zis_printf_fn_arg_fmtstr(fmt), ...
);

zis_noreturn void __zis_test_assertion_fail(
    const char *file, unsigned int line, const char *func, const char *expr
);
