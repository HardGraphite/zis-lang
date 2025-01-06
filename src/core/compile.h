/// Bytecode compiler.

#pragma once

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_codegen;
struct zis_context;
struct zis_func_obj;
struct zis_module_obj;
struct zis_parser;
struct zis_stream_obj;

#if ZIS_FEATURE_SRC

/// Stuff needed for compilation.
struct zis_compilation_bundle {
    struct zis_parser  *parser;
    struct zis_codegen *codegen;
    struct zis_context *context;
};

/// Initialize the `compilation_bundle`.
void zis_compilation_bundle_init(struct zis_compilation_bundle *restrict cb, struct zis_context *z);

/// Finalize the `compilation_bundle`. Free resources.
void zis_compilation_bundle_fini(struct zis_compilation_bundle *restrict cb);

/// Compile source code from `input` stream to a function.
/// On failure, formats an exception (REG-0) and returns NULL.
/// The parameters `module` is optional.
struct zis_func_obj *zis_compile_source(
    const struct zis_compilation_bundle *restrict comp_bundle,
    struct zis_stream_obj *input, struct zis_module_obj *module /* = NULL */
);

#endif // ZIS_FEATURE_SRC
