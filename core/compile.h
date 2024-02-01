/// Bytecode compiler.

#pragma once

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_context;
struct zis_func_obj;
struct zis_stream_obj;

#if ZIS_FEATURE_SRC

/// Compile source code from `input` stream to a function.
/// On failure, formats an exception (REG-0) and returns NULL.
struct zis_func_obj *zis_compile_source(
    struct zis_context *z, struct zis_stream_obj *input
);

#endif // ZIS_FEATURE_SRC
