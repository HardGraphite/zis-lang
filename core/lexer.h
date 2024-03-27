/// The lexical analyzer.

#pragma once

#include <stdbool.h>

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_context;
struct zis_lexer;
struct zis_map_obj;
struct zis_object;
struct zis_stream_obj;
struct zis_token;

#if ZIS_FEATURE_SRC

/// Lexer error handling function. Should be "noreturn".
typedef void (*zis_lexer_error_handler_t)(struct zis_lexer *, const char *restrict);

/// Lexical analyzer.
/// The owner of this lexer must call `_zis_lexer_gc_visit()` in a GC root visitor.
struct zis_lexer {
    unsigned int line, column;
    unsigned int ignore_eol; ///< Whether to ignore end-of-line tokens. Being 0 after started.
    bool input_eof;
    struct zis_context *z;
    struct zis_stream_obj *input; // Stream objects will not be moved by the GC system.
    struct zis_map_obj *keywords;
    struct zis_object *temp_var;
    zis_lexer_error_handler_t error_handler;
};

/// Initialize the lexer.
void zis_lexer_init(struct zis_lexer *restrict l, struct zis_context *z);

/// Start a new input.
void zis_lexer_start(
    struct zis_lexer *restrict l,
    struct zis_stream_obj *input_stream, zis_lexer_error_handler_t error_handler
);

/// Finish an input.
void zis_lexer_finish(struct zis_lexer *restrict l);

/// Scan for the next token.
/// The error handler may be called, in which case this function does not return.
void zis_lexer_next(struct zis_lexer *restrict l, struct zis_token *restrict tok);

/// Start of a ignore-end-of-line region.
void zis_lexer_ignore_eol_begin(struct zis_lexer *restrict l);

/// End of a ignore-end-of-line region.
void zis_lexer_ignore_eol_end(struct zis_lexer *restrict l);

void _zis_lexer_gc_visit(struct zis_lexer *restrict l, int op);

#endif // ZIS_FEATURE_SRC
