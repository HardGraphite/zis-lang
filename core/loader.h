/// Program loader.

#pragma once

#include "compat.h"
#include "fsutil.h" // zis_path_char_t, ZIS_PATH_MAX

struct zis_array_obj;
struct zis_context;
struct zis_module_obj;
struct zis_path_obj;
struct zis_stream_obj;
struct zis_symbol_obj;

/// Module loader. This is a GC root.
struct zis_module_loader;

/// Create a module loader.
struct zis_module_loader *zis_module_loader_create(struct zis_context *z);

/// Delete a module loader.
void zis_module_loader_destroy(struct zis_module_loader *ml, struct zis_context *z);

/// Add a search path to the end of the path list. Ignore if duplicate.
void zis_module_loader_add_path(struct zis_context *z, struct zis_path_obj *path);

/// Search for a module file.
bool zis_module_loader_search(
    struct zis_context *z,
    zis_path_char_t path_buffer[ZIS_PARAMARRAY_STATIC ZIS_PATH_MAX],
    struct zis_symbol_obj *module_name
);

/// Save a module as loaded. The `sub_module_name` is optional.
void zis_module_loader_add_loaded(
    struct zis_context *z,
    struct zis_symbol_obj *module_name,
    struct zis_symbol_obj *sub_module_name /* = NULL */,
    struct zis_module_obj *module
);

/// Find a loaded module by its name. Returns NULL if not exist.
struct zis_module_obj *zis_module_loader_get_loaded(
    struct zis_context *z,
    struct zis_symbol_obj *module_name
);

/// Find the name of a loaded module.
/// The result is stored to `name`: `name[0]` = module_name, `name[1]` = submod_name/NULL.
bool zis_module_loader_find_loaded_name(
    struct zis_context *z,
    struct zis_symbol_obj *name[ZIS_PARAMARRAY_STATIC 2],
    struct zis_module_obj *module
);

#define ZIS_MOD_LDR_SEARCH_LOADED   0x01  ///< Search in loaded modules.
#define ZIS_MOD_LDR_UPDATE_LOADED   0x02  ///< Add to loaded modules.

/// Import (load and initialize) a module by its name.
/// Parameters `module` and `sub_module_name` are optional.
/// When `module` is given, data is loaded into it and the flag `ZIS_MOD_LDR_SEARCH_LOADED` is ignored.
/// On failure, puts an exception in REG-0 and returns NULL.
struct zis_module_obj *zis_module_loader_import(
    struct zis_context *z, struct zis_module_obj *module /* = NULL */,
    struct zis_symbol_obj *module_name, struct zis_symbol_obj *sub_module_name /* = NULL */,
    int flags
);

/// Import (load and initialize) a module from the file.
/// when it is given, data is loaded into it and the flags are ignored.
/// On failure, puts an exception in REG-0 and returns NULL.
struct zis_module_obj *zis_module_loader_import_file(
    struct zis_context *z, struct zis_module_obj *module /* = NULL */,
    struct zis_path_obj *file
);

/// Import (compile and initialize) a module from source code from the stream.
/// On failure, puts an exception in REG-0 and returns NULL.
struct zis_module_obj *zis_module_loader_import_source(
    struct zis_context *z, struct zis_module_obj *module /* = NULL */,
    struct zis_stream_obj *input
);
