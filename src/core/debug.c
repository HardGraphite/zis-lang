#include "debug.h"

#include <assert.h>
#include <stdlib.h>

#include "platform.h"
#include "strutil.h"

#include "zis_config.h" // ZIS_ENVIRON_NAME_DEBUG_LOG

#if ZIS_DEBUG

#include <time.h>

void zis_debug_time(struct timespec *tp) {
#if ZIS_SYSTEM_POSIX || (defined(__MINGW32__) && !defined(_UCRT))
    clock_gettime(CLOCK_MONOTONIC, tp);
#else
    timespec_get(tp, TIME_UTC);
#endif
}

#endif // ZIS_DEBUG

#if ZIS_DEBUG_LOGGING

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *logging_stream = NULL;
static char logging_group[32] = { 0 };
static enum zis_debug_log_level logging_level = ZIS_DEBUG_LOG_WARN;
static bool logging_colorful = false;

static const char *const logging_level_name[] = {
    [ZIS_DEBUG_LOG_FATAL] = "Fatal",
    [ZIS_DEBUG_LOG_ERROR] = "Error",
    [ZIS_DEBUG_LOG_WARN ] = "Warn" ,
    [ZIS_DEBUG_LOG_INFO ] = "Info" ,
    [ZIS_DEBUG_LOG_TRACE] = "Trace",
    [ZIS_DEBUG_LOG_DUMP ] = "Dump" ,
};

static const char *const logging_color_by_level[] = {
    [ZIS_DEBUG_LOG_FATAL] = "\033[1;31m", // red
    [ZIS_DEBUG_LOG_ERROR] = "\033[1;31m", // red
    [ZIS_DEBUG_LOG_WARN ] = "\033[1;33m", // yellow
    [ZIS_DEBUG_LOG_INFO ] = "\033[1;34m", // blue
    [ZIS_DEBUG_LOG_TRACE] = "\033[1;36m", // cyan
    [ZIS_DEBUG_LOG_DUMP ] = "\033[1;32m", // green
};

#define logging_level_count (sizeof logging_level_name / sizeof logging_level_name[0])

zis_unused_fn static void logging_parse_config(const char *conf_str) {
    // "[LEVEL]:[GROUP]:[FILE]"
    if (!*conf_str)
        return;
    char level_name[8], group_name[32], file_ch;
    const int n = sscanf(conf_str, "%7[^:]:%31[^:]:%c", level_name, group_name, &file_ch);
    if (n < 1)
        return;
    for (size_t i = 0; i < logging_level_count; i++) {
        if (zis_str_icmp(level_name, logging_level_name[i]) == 0) {
            logging_level = (enum zis_debug_log_level)i;
            break;
        }
    }
    if (n < 2)
        return;
    strcpy(logging_group, group_name);
    if (n < 3)
        return;
    const char *const file_name = strchr(strchr(conf_str, ':') + 1, ':') + 1;
    zis_unused_var(file_ch);
    assert(file_ch == file_name[0]);
    logging_stream = fopen(file_name, "w");
    logging_colorful = false;
}

static void logging_fini(void) {
    if (logging_stream) {
        if (logging_stream != stdout && logging_stream != stderr)
            fclose(logging_stream);
        logging_stream = NULL;
    }
}

static void logging_init(void) {
    if (logging_stream) // FIXME: use atomic operation or mutex.
        return;

    logging_stream = stderr;
    logging_colorful = true;

#ifdef ZIS_ENVIRON_NAME_DEBUG_LOG
    const char *config_string = getenv(ZIS_ENVIRON_NAME_DEBUG_LOG);
    if (config_string)
        logging_parse_config(config_string);
#endif // ZIS_ENVIRON_NAME_DEBUG_LOG

    atexit(logging_fini);
#if !(defined(__MINGW32__) && !defined(_UCRT))
    at_quick_exit(logging_fini);
#endif // !(MinGW && !UCRT)

    char time_str[24];
    const time_t time_num = time(NULL);
    strftime(time_str, sizeof time_str, "%F %T", localtime(&time_num));
    zis_debug_log(
        INFO, "Debug", "logging_init@|%s|: level=%s, group=%s",
        time_str,
        logging_level_name[logging_level],
        *logging_group ? logging_group : "<any>"
    );
}

static bool logging_check(enum zis_debug_log_level level, const char *group) {
    if ((size_t)level >= logging_level_count || (int)level > (int)logging_level)
        return false;
    if (*logging_group && zis_str_icmp(group, logging_group) != 0)
        return false;
    return true;
}

static uintmax_t logging_timestamp(void) {
    const time_t t = clock();
    return t / (CLOCKS_PER_SEC / 1000);
}

zis_noinline static void logging_print(
    uintmax_t timestamp, enum zis_debug_log_level level,
    const char *group, const char *message, int msg_len
) {
    fprintf(
        logging_stream, "%s[T%.03ju.%.03u|" ZIS_DISPLAY_NAME "|%-5s|%-6s]%s %.*s%s\n",
        logging_colorful && (size_t)level < logging_level_count ?
            logging_color_by_level[(size_t)level] : "",
        timestamp / 1000, (unsigned int)(timestamp % 1000),
        logging_level_name[(size_t)level], group,
        logging_colorful ? "\033[0m\033[1m" : "",
        msg_len, message,
        logging_colorful ? "\033[0m" : ""
    );
}

void _zis_debug_log(
    enum zis_debug_log_level level, const char *group,
    zis_printf_fn_arg_fmtstr const char *fmt, ...
) {
    if (!logging_check(level, group))
        return;
    const uintmax_t time = logging_timestamp();
    va_list ap;
    va_start(ap, fmt);
    char buffer[256];
    const int n = vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    logging_print(time, level, group, n > 0 ? buffer : "", n);
}

FILE *zis_debug_log_stream(enum zis_debug_log_level level, const char *group) {
    return logging_check(level, group) ? logging_stream : NULL;
}

#endif // ZIS_DEBUG_LOGGING

#if ZIS_DEBUG_DUMPBT

#include <stdio.h>
#include <signal.h>

#include "attributes.h"

#if ZIS_SYSTEM_WINDOWS && !defined(__MINGW32__)

#include <wchar.h>
#include <Windows.h>
#include <DbgHelp.h>
#include <processthreadsapi.h>
#define BT_WINDOWS 1
#pragma comment(lib, "DbgHelp")

#elif ZIS_SYSTEM_POSIX && defined(__has_include) && __has_include(<execinfo.h>)

#include <execinfo.h>
#include <unistd.h>
#define BT_EXECINFO 1

#else

#pragma message "zis_debug_dump_backtrace() is not available"

#endif

zis_cold_fn void zis_debug_dump_backtrace(FILE *stream) {
#if BT_WINDOWS

    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, NULL, TRUE);

    void *bt_buf[62];
    const unsigned int n_bt =
        CaptureStackBackTrace(1, sizeof bt_buf / sizeof bt_buf[0], bt_buf, NULL);

    struct { SYMBOL_INFOW info; wchar_t _name_buf[80]; } sym_info;
    sym_info.info.SizeOfStruct = sizeof sym_info.info;
    sym_info.info.MaxNameLen = sizeof sym_info._name_buf / sizeof(wchar_t);
    IMAGEHLP_LINEW64 line_info;
    line_info.SizeOfStruct = sizeof line_info;
    for (unsigned int i = 0; i < n_bt; i++) {
        DWORD64 sym_addr_off;
        const wchar_t *sym_name;
        int sym_name_len;
        void *sym_addr;
        if (SymFromAddrW(proc, (DWORD64)(bt_buf[i]), &sym_addr_off, &sym_info.info)) {
            sym_name = sym_info.info.Name;
            sym_name_len = sym_info.info.NameLen;
            sym_addr = (void *)sym_info.info.Address;
        } else {
            sym_name = L"???";
            sym_name_len = 3;
            sym_addr = NULL;
            sym_addr_off = 0U;
        }
        const wchar_t *file_name;
        unsigned long line_num;
        if (sym_addr && SymGetLineFromAddrW64(proc, sym_info.info.Address, &(DWORD){0}, &line_info)) {
            file_name = line_info.FileName;
            line_num = line_info.LineNumber;
        } else {
            file_name = L"???";
            line_num = 0;
        }
        fwprintf(
            stream, L"%2u: %ls(%u): %.*ls+%#x [%p]\n",
            i, file_name, line_num, sym_name_len,
            sym_name, (unsigned int)sym_addr_off, sym_addr
        );
    }

#elif BT_EXECINFO

    void *bt_buf[64];
    const int n_bt = backtrace(bt_buf, (int)(sizeof bt_buf / sizeof bt_buf[0]));
    if (n_bt)
        backtrace_symbols_fd(bt_buf + 1, n_bt - 1, fileno(stream));

#else

    zis_unused_var(stream);

#endif
}

zis_cold_fn static void dump_bt_sig_handler(int sig) {
    char msg_buf[32];
    FILE *stream = stderr;
#if ZIS_DEBUG_LOGGING
    if (logging_stream)
        stream = logging_stream;
#endif // ZIS_DEBUG_LOGGING
    const int msg_len =
        snprintf(msg_buf, sizeof msg_buf, "!! Signal %i raised, backtrace:\n", sig);
    fwrite(msg_buf, 1, (size_t)msg_len, stream);
    zis_debug_dump_backtrace(stream);
    signal(sig, SIG_DFL);
    raise(sig);
    _Exit(EXIT_FAILURE);
    // FIXME: some of the used functions are not safe to call in a signal handler.
}

static void dump_bt_init(void) {
    const int sig_list[] = {
        SIGSEGV,
        SIGINT,
        SIGILL,
        SIGABRT,
        SIGFPE,
    };
    for (unsigned int i = 0; i < sizeof sig_list / sizeof sig_list[0]; i++) {
        void (*old_fn)(int) = signal(sig_list[i], dump_bt_sig_handler);
        if (old_fn != SIG_DFL)
            signal(sig_list[i], old_fn);
        else
            zis_debug_log(INFO, "Debug", "signal(%i, %s)", sig_list[i], "dump_bt_sig_handler");
    }
}

#endif // ZIS_DEBUG_DUMPBT

void zis_debug_try_init(void) {
#if ZIS_DEBUG_LOGGING
    logging_init();
#endif // ZIS_DEBUG_LOGGING
#if ZIS_DEBUG_DUMPBT
    dump_bt_init();
#endif // ZIS_DEBUG_DUMPBT
}
