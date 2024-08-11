#include "test.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/platform.h"

#if ZIS_SYSTEM_POSIX
#    include <signal.h>
#elif ZIS_SYSTEM_WINDOWS
#    include <Windows.h>
#endif

/* ----- global state ------------------------------------------------------- */

struct test_state {
    const char *test_list_name;
    const char *test_name;
    jmp_buf test_jmpbuf;
};

static struct test_state test_state;

#define test_state_setjmp() \
    (setjmp(test_state.test_jmpbuf))

#define test_state_longjmp() \
    (longjmp(test_state.test_jmpbuf, 1))

/* ----- logging ------------------------------------------------------------ */

static enum zis_test_log_level logging_level = ZIS_TEST_LOG_STATUS;
static FILE *logging_file = NULL;

#define LOGGING_LEVEL_ENV "ZIS_TEST_LOG"

static const char *const logging_level_name[] = {
    [ZIS_TEST_LOG_ERROR ] = "Error" ,
    [ZIS_TEST_LOG_STATUS] = "Status",
    [ZIS_TEST_LOG_TRACE ] = "Trace" ,
};

static void logging_init(void) {
    if (logging_file)
        return;
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

static void test_message(const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buffer[256];
    if (vsnprintf(buffer, sizeof buffer, fmt, ap) < 0)
        buffer[0] = 0;
    va_end(ap);

    fprintf(
        logging_file, "[ZIS-TEST] (%s::%s) %s\n",
        test_state.test_list_name, test_state.test_name ? test_state.test_name : "*",
        buffer
    );
}

void __zis_test_log(
    int level, const char *file, unsigned int line, const char *func,
    const char *restrict zis_printf_fn_arg_fmtstr(fmt), ...
) {
    if (level > (int)logging_level)
        return;

    va_list ap;
    va_start(ap, fmt);
    char buffer[256];
    if (vsnprintf(buffer, sizeof buffer, fmt, ap) < 0)
        buffer[0] = 0;
    va_end(ap);

    fprintf(
        logging_file, "[ZIS-TEST] [%s] (%s::%s) %s:%u: %s: %s\n",
        logging_level_to_name(level),
        test_state.test_list_name, test_state.test_name,
        file, line, func, buffer
    );
}

/* ----- assertions --------------------------------------------------------- */

/// A debugger breakpoint. Skip if not debugging.
static void breakpoint(void) {
#if ZIS_SYSTEM_POSIX
    void(*old_handler)(int) = signal(SIGTRAP, SIG_IGN);
    raise(SIGTRAP);
    signal(SIGTRAP, old_handler == SIG_ERR ? SIG_DFL : old_handler);
#elif ZIS_SYSTEM_WINDOWS
    if (IsDebuggerPresent())
        DebugBreak();
#else
    fputs("[breakpoint]\n", stderr);
#endif
}

zis_noreturn void __zis_test_assertion_fail(
    const char *file, unsigned int line, const char *func, const char *expr
) {
    __zis_test_log(ZIS_TEST_LOG_ERROR, file, line, func, "assertion ``%s'' failed", expr);
    breakpoint();
    test_state_longjmp();
}

/* ----- test-case definitions ---------------------------------------------- */

union test_entry_ptr {
    const struct __zis_test0_entry *test0;
    const struct __zis_test_entry *test;
};

static int test_run_common(
    union test_entry_ptr entries,
    zis_t z, int argc, char * argv[]
) {
    logging_init();
    zis_unused_var(argc);
    test_state.test_list_name = argv[0] ? argv[0] : "??";

    unsigned int failure_count = 0;
    test_state.test_name = NULL;

    if (test_state_setjmp()) {
        test_message("failed");
        failure_count++;
        if (z) {
            // FIXME: unwind ZiS runtime callstack.
        }
    }

    while (entries.test->name) {
        union test_entry_ptr this_entry = entries;
        entries.test++;
        test_state.test_name = this_entry.test->name;
        test_message("start");
        if (!z)
            this_entry.test0->func();
        else
            this_entry.test->func(z);
        test_message("passed");
    }

    test_state.test_name = NULL;
    test_message("%u failed", failure_count);
    return failure_count ? EXIT_FAILURE : EXIT_SUCCESS;
}

int __zis_test_fn(zis_t z, void *_state) {
    struct __zis_test_fn_state *const s = _state;
    return test_run_common((union test_entry_ptr){.test = s->entries}, z, s->argc, s->argv);
}

int __zis_test0_run(
    const struct __zis_test0_entry *entries,
    int argc, char * argv[]
) {
    return test_run_common((union test_entry_ptr){.test0 = entries}, NULL, argc, argv);
}
