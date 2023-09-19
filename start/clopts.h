/// Command-line option utilities

#pragma once

#include <stdio.h>

struct clopts_option;
struct clopts_context;

/// Type of an option handler function.
typedef void (*clopts_option_handler_t) \
    (struct clopts_context *ctx, const char *arg, void *data);

/// Type of the rest-args handler function.
typedef void (*clopts_restargs_handler_t) \
    (struct clopts_context *ctx, const char *args[], int argc, void *data);

/// Report an error in a handler. The function does not return.
_Noreturn void clopts_handler_error(struct clopts_context *ctx, const char *fmt, ...);

/// Terminate parsing without errors in a handler. The function does not return.
_Noreturn void clopts_handler_break(struct clopts_context *ctx);

/// Definition of an option.
struct clopts_option {
    char name; ///< Option name.
    const char *arg_name; ///< Argument name. '\0' means do not accept arguments.
    clopts_option_handler_t handler; ///< Option handler.
    const char *help; ///< Help message. Optional.
};

/// Definition of the program command line info.
struct clopts_program {
    const char *usage_args;
    const struct clopts_option *options; // The last element of the array must be zeros.
    clopts_restargs_handler_t rest_args;
};

/// Parse command line arguments.
/// The parameter `data` will be passed to the handlers. The parameter `err_stream`
/// is optional. The parameter `argc` must be positive.
/// On success, returns 0; when `clopts_handler_error()` is called, prints the
/// error message (if `err_stream` is available) and returns -1; when
/// `clopts_handler_break()` is called, returns 1.
int clopts_parse(
    const struct clopts_program *def,
    void *data, FILE *err_stream, int argc, char *argv[]
);

/// Print program help message.
void clopts_help(const struct clopts_program *def, FILE *stream, struct clopts_context *ctx);
