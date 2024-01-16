#include "debug.h"

#include <assert.h>

#include "platform.h"
#include "strutil.h"

#include "zis_config.h" // ZIS_ENVIRON_NAME_DEBUG_LOG

#if ZIS_DEBUG

#include <time.h>

void zis_debug_time(struct timespec *tp) {
#if ZIS_SYSTEM_POSIX
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

static const char *const logging_level_name[] = {
    [ZIS_DEBUG_LOG_FATAL] = "Fatal",
    [ZIS_DEBUG_LOG_ERROR] = "Error",
    [ZIS_DEBUG_LOG_WARN ] = "Warn" ,
    [ZIS_DEBUG_LOG_INFO ] = "Info" ,
    [ZIS_DEBUG_LOG_TRACE] = "Trace",
    [ZIS_DEBUG_LOG_DUMP ] = "Dump" ,
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

#ifdef ZIS_ENVIRON_NAME_DEBUG_LOG
    const char *config_string = getenv(ZIS_ENVIRON_NAME_DEBUG_LOG);
    if (config_string)
        logging_parse_config(config_string);
#endif // ZIS_ENVIRON_NAME_DEBUG_LOG

    at_quick_exit(logging_fini);

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
        logging_stream, "[T%.03ju.%.03u|" ZIS_DISPLAY_NAME "|%-5s|%-6s] %.*s\n",
        timestamp / 1000, (unsigned int)(timestamp % 1000),
        logging_level_name[(size_t)level], group, msg_len, message
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

void zis_debug_try_init(void) {
#if ZIS_DEBUG_LOGGING
    logging_init();
#endif // ZIS_DEBUG_LOGGING
}
