/// C source code debugging tools.

#pragma once

#include <zis_config.h> // ZIS_DEBUG_*

/// Initialize global debugging environment. It's safe to be called more than once.
void zis_debug_try_init(void);

#if ZIS_DEBUG

struct timespec; // "time.h"

/// Get current time from a high-resolution clock.
void zis_debug_time(struct timespec *tp);

#endif // ZIS_DEBUG

#if ZIS_DEBUG_LOGGING

#include <stdio.h>

#include "attributes.h"

/// Logging levels.
enum zis_debug_log_level {
    ZIS_DEBUG_LOG_FATAL,
    ZIS_DEBUG_LOG_ERROR,
    ZIS_DEBUG_LOG_WARN,
    ZIS_DEBUG_LOG_INFO,
    ZIS_DEBUG_LOG_TRACE,
    ZIS_DEBUG_LOG_DUMP,
};

/// Print a logging message.
#define zis_debug_log(level, group, ...) \
    (_zis_debug_log(ZIS_DEBUG_LOG_##level, group, __VA_ARGS__))

/// Print logging messages with a block of code.
#define zis_debug_log_1(level, group, prompt, stream_var, stmt) \
do {                                                            \
    FILE *const __fp =                                          \
        zis_debug_log_stream(ZIS_DEBUG_LOG_##level, group);     \
    if (!__fp)                                                  \
        break;                                                  \
    zis_debug_log(level, group, "%s vvv", prompt);              \
    { FILE * stream_var = __fp; stmt }                          \
    zis_debug_log(level, group, "%s ^^^", prompt);              \
} while (0)

/// Get the logging stream. Returns NULL if not available.
FILE *zis_debug_log_stream(enum zis_debug_log_level level, const char *group);

void _zis_debug_log(
    enum zis_debug_log_level, const char *,
    zis_printf_fn_arg_fmtstr const char *, ...
) zis_printf_fn_attrs(3, 4);

#else // !ZIS_DEBUG_LOGGING

#define zis_debug_log(level, group, ...) ((void)0)
#define zis_debug_log_1(level, group, prompt, var, stmt) ((void)0)

#endif // ZIS_DEBUG_LOGGING
