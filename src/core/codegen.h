/// Bytecode code-generator.

#pragma once

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_assembler;
struct zis_ast_node_obj;
struct zis_context;
struct zis_func_obj;
struct zis_module_obj;

#if ZIS_FEATURE_SRC

/// Bytecode code-generator.
struct zis_codegen;

/// Create a code-generator.
struct zis_codegen *zis_codegen_create(struct zis_context *z);

/// Delete a code-generator.
void zis_codegen_destroy(struct zis_codegen *cg, struct zis_context *z);

/// Generate a bytecode function from the AST.
/// On failure, formats an exception (REG-0) and returns NULL.
struct zis_func_obj *zis_codegen_generate(
    struct zis_codegen *cg,
    struct zis_ast_node_obj *ast,
    struct zis_module_obj *module /* = NULL */
);

#endif // ZIS_FEATURE_SRC
