/// The source code parser.

#pragma once

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_context;
struct zis_object;
struct zis_stream_obj;

#if ZIS_FEATURE_SRC

/// Source code parser.
struct zis_parser;

/// Create a parser.
struct zis_parser *zis_parser_create(struct zis_context *z);

/// Delete a parser.
void zis_parser_destroy(struct zis_parser *p, struct zis_context *z);

enum zis_parser_what {
    ZIS_PARSER_MOD,  ///< module (as a function)
    ZIS_PARSER_EXPR, ///< next expression
};

/// Do parsing and generate an AST.
/// On failure, formats an exception (REG-0) and returns NULL.
struct zis_ast_node_obj *zis_parser_parse(
    struct zis_parser *p,
    struct zis_stream_obj *input,
    enum zis_parser_what what
);

#endif // ZIS_FEATURE_SRC
