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
#define zis_test_list(...) \
    int main(void) {       \
        zis_test_func_t l[] = { __VA_ARGS__ NULL }; \
        struct zis_context *const z = zis_create(); \
        for (zis_test_func_t *p = l; *p; p++)       \
            (*p)(z);       \
        zis_destroy(z);    \
    }                      \
// ^^^ zis_test_list() ^^^

/// Type of a test0 function.
typedef void(*zis_test0_func_t)(void);

/// Define a test0 function.
#define zis_test0_define(__name) \
    static void __name (void)

/// Enumerate test0 functions defined by `zis_test0_define()`.
#define zis_test0_list(...) \
    int main(void) {       \
        zis_test0_func_t l[] = { __VA_ARGS__ NULL }; \
        for (zis_test0_func_t *p = l; *p; p++)       \
            (*p)();        \
    }                      \
// ^^^ zis_test0_list() ^^^

/// Print log.
#define zis_test_log(__level, ...) \
    __zis_test_log((__level), __FILE__, __LINE__, __VA_ARGS__)

/// Assertion.
#define zis_test_assert(__expr) \
    do {                        \
        if (!(__expr))          \
            __zis_test_assertion_fail(__FILE__, __LINE__, #__expr); \
    } while (0)                 \
// ^^^ zis_test_assert() ^^^

/// Assertion: equal.
#define zis_test_assert_eq(__lhs, __rhs) \
    zis_test_assert((__lhs) == (__rhs))

/// Assertion: not equal.
#define zis_test_assert_ne(__lhs, __rhs) \
    zis_test_assert((__lhs) != (__rhs))

/* -------------------------------------------------------------------------- */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

zis_printf_fn_attrs(4, 5) zis_static_inline void __zis_test_log(
    int level, const char *file, unsigned int line,
    const char *restrict zis_printf_fn_arg_fmtstr(fmt), ...
) {
    zis_unused_var(level);

    va_list ap;
    va_start(ap, fmt);
    char buffer[256];
    if (vsnprintf(buffer, sizeof buffer, fmt, ap) < 0)
        buffer[0] = 0;
    va_end(ap);

    fprintf(stderr, "[ZIS-TEST] ** %s:%u: %s\n", file, line, buffer);
}

zis_noreturn zis_static_inline void __zis_test_assertion_fail(
    const char *file, unsigned int line, const char *expr
) {
    fprintf(stderr, "[ZIS-TEST] !! %s:%u: assertion failed: %s\n", file, line, expr);
    quick_exit(EXIT_FAILURE);
}
