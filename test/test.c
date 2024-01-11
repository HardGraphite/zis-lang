#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/platform.h"

/* ----- debugging and breakpoint ------------------------------------------- */

#if ZIS_SYSTEM_POSIX
#    include <signal.h>
#elif ZIS_SYSTEM_WINDOWS
#    include <Windows.h>
#endif

#if ZIS_SYSTEM_POSIX

static void sigtrap_handler(int sig) {
    zis_unused_var(sig);
    fputs("(breakpoint)\n", stderr);
}

#endif // ZIS_SYSTEM_POSIX

zis_noreturn static void breakpoint_or_abort(void) {
#if ZIS_SYSTEM_POSIX
    signal(SIGTRAP, sigtrap_handler);
    raise(SIGTRAP); // Should trigger `sigtrap_handler()` if not debugging.
#elif ZIS_SYSTEM_WINDOWS
    if (IsDebuggerPresent())
        DebugBreak();
#endif

    quick_exit(EXIT_FAILURE);
}

/* ----- logging ------------------------------------------------------------ */

static enum zis_test_log_level logging_level = ZIS_TEST_LOG_STATUS;
static FILE *logging_file = NULL;

#define LOGGING_LEVEL_ENV "ZIS_TEST_LOG"

static const char *const logging_level_name[] = {
    [ZIS_TEST_LOG_ERROR ] = "Error" ,
    [ZIS_TEST_LOG_STATUS] = "Status",
    [ZIS_TEST_LOG_TRACE ] = "Trace" ,
};

static void init_logging_level(void) {
    logging_file = stderr;
    const char *conf = getenv(LOGGING_LEVEL_ENV);
    if (conf) {
        const size_t n = sizeof logging_level_name / sizeof logging_level_name[0];
        for (size_t i = 0; i < n; i++) {
            if (strcmp(conf, logging_level_name[i]) == 0) {
                logging_level = (enum zis_test_log_level)i;
                break;
            }
        }
    }
}

static const char *logging_level_to_name(enum zis_test_log_level level) {
    const size_t n = sizeof logging_level_name / sizeof logging_level_name[0];
    if ((size_t)level >= n)
        return "?";
    return logging_level_name[(size_t)level];
}

static bool check_logging_level(enum zis_test_log_level level) {
    return (int)level <= (int)logging_level;
}

/* ----- exported functions ------------------------------------------------- */

void __zis_test_init(void) {
    init_logging_level();
}

int __zis_test_list_block(zis_t z, void *_arg) {
    for (zis_test_func_t *f = _arg; *f; f++)
        (*f)(z);
    return 0;
}

void __zis_test_log(
    int level, const char *file, unsigned int line, const char *func,
    const char *restrict zis_printf_fn_arg_fmtstr(fmt), ...
) {
    if (!check_logging_level(level))
        return;

    va_list ap;
    va_start(ap, fmt);
    char buffer[256];
    if (vsnprintf(buffer, sizeof buffer, fmt, ap) < 0)
        buffer[0] = 0;
    va_end(ap);

    fprintf(
        logging_file, "[ZIS-TEST] (%s) %s:%u: %s: %s\n",
        logging_level_to_name(level), file, line, func, buffer
    );
}

zis_noreturn void __zis_test_assertion_fail(
    const char *file, unsigned int line, const char *func, const char *expr
) {
    __zis_test_log(ZIS_TEST_LOG_ERROR, file, line, func, "assertion ``%s'' failed", expr);
    breakpoint_or_abort();
}
