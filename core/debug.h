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

#include "attributes.h"

/// Logging levels.
enum zis_debug_log_level {
    ZIS_DEBUG_LOG_FATAL,
    ZIS_DEBUG_LOG_ERROR,
    ZIS_DEBUG_LOG_WARN,
    ZIS_DEBUG_LOG_INFO,
    ZIS_DEBUG_LOG_TRACE,
};

typedef void (*zis_debug_log_with_func_t)(void *arg, void *file);

/// Print a logging message.
#define zis_debug_log(level, group, ...) \
    (_zis_debug_log(ZIS_DEBUG_LOG_##level, group, __VA_ARGS__))

/// Print logging messages with a function.
/// The function `func` takes `func_arg` and a FILE pointer as the arguments.
#define zis_debug_log_with(level, group, prompt, func, func_arg) \
    (_zis_debug_log_with(ZIS_DEBUG_LOG_##level, group, prompt, func, func_arg))

void _zis_debug_log(
    enum zis_debug_log_level, const char *,
    zis_printf_fn_arg_fmtstr const char *, ...
) zis_printf_fn_attrs(3, 4);
void _zis_debug_log_with(
    enum zis_debug_log_level, const char *,
    const char *, zis_debug_log_with_func_t, void *
);

#else // !ZIS_DEBUG_LOGGING

#define zis_debug_log(level, group, ...) ((void)0)
#define zis_debug_log_with(level, group, prompt, func, func_arg) ((void)0)

#endif // ZIS_DEBUG_LOGGING
