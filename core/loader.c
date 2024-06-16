#include "loader.h"

#include <assert.h>
#include <stddef.h>

#include "assembly.h"
#include "attributes.h"
#include "compat.h"
#include "compile.h"
#include "context.h"
#include "debug.h"
#include "fsutil.h"
#include "globals.h"
#include "locals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "platform.h" // ZIS_WORDSIZE

#include "arrayobj.h"
#include "exceptobj.h"
#include "funcobj.h"
#include "mapobj.h"
#include "moduleobj.h"
#include "pathobj.h"
#include "streamobj.h"
#include "symbolobj.h"

#include "zis_config.h"
#include "zis_modules.h"

/* ----- embedded modules --------------------------------------------------- */

#if !ZIS_EMBEDDED_MODULE_LIST_EMPTY

#define E(NAME)  extern const struct zis_native_module_def ZIS_NATIVE_MODULE_VARNAME(NAME);
ZIS_EMBEDDED_MODULE_LIST
#undef E

struct zis_native_module_def__named_ref {
    const char *name;
    const struct zis_native_module_def *def;
};

static const struct zis_native_module_def__named_ref embedded_module_list[] = {
#define E(NAME) { #NAME, & ZIS_NATIVE_MODULE_VARNAME(NAME) } ,
    ZIS_EMBEDDED_MODULE_LIST
#undef E
};

#endif // !ZIS_EMBEDDED_MODULE_LIST_EMPTY

/// Search for an embedded module by name. Returns NULL if not exists.
static const struct zis_native_module_def *find_embedded_module(const char *name) {
#if ZIS_EMBEDDED_MODULE_LIST_EMPTY

    zis_unused_var(name);
    return NULL;

#else // !ZIS_EMBEDDED_MODULE_LIST_EMPTY

    const size_t embedded_module_count =
        sizeof embedded_module_list / sizeof embedded_module_list[0];
#if ZIS_EMBEDDED_MODULE_LIST_SORTED

    // List sorted. Use the binary search algorithm.

    typedef intptr_t _ssize_t;
    static_assert(sizeof(size_t) == sizeof(_ssize_t), "");

    assert(embedded_module_count && embedded_module_count < SIZE_MAX / 2);
    size_t index_l = 0, index_r = embedded_module_count - 1;

    do {
        const size_t index_m = index_l + (index_r - index_l) / 2;
        const struct zis_native_module_def__named_ref *const def = &embedded_module_list[index_m];
        const int diff = strcmp(def->name, name);
        if (diff == 0)
            return def->def;
        if (diff < 0)
            index_l = index_m + 1;
        else
            index_r = index_m - 1;
    } while ((_ssize_t)index_l <= (_ssize_t)index_r);

#else // !ZIS_EMBEDDED_MODULE_LIST_SORTED

    for (size_t i = 0; i < embedded_module_count; i++) {
        const struct zis_native_module_def *const def = embedded_module_list[i];
        if (strcmp(name, def->name) == 0)
            return def;
    }

#endif // ZIS_EMBEDDED_MODULE_LIST_SORTED

    return NULL;

#endif // ZIS_EMBEDDED_MODULE_LIST_EMPTY
}

/* ----- internal data structures ------------------------------------------- */

struct module_loader_data {
    struct zis_array_obj *search_path; // { dir (Path) }
    struct zis_map_obj   *loaded_modules; // { name (Symbol) -> mod (Module) / tree ( Map{ name (Symbol) -> mod (Module) } ) }
};

static void module_loader_data_as_obj_vec(
    struct module_loader_data *d,
    struct zis_object **begin_and_end[ZIS_PARAMARRAY_STATIC 2]
) {
    begin_and_end[0] = (struct zis_object **)d;
    begin_and_end[1] = (struct zis_object **)((char *)d + sizeof(*d));
    assert(begin_and_end[1] - begin_and_end[0] == 2);
}

/// GC objects visitor. See `zis_objmem_object_visitor_t`.
static void module_loader_data_gc_visitor(void *_d, enum zis_objmem_obj_visit_op op) {
    struct module_loader_data *const d = _d;
    struct zis_object **range[2];
    module_loader_data_as_obj_vec(d, range);
    zis_objmem_visit_object_vec(range[0], range[1], op);
}

struct zis_module_loader {
    struct module_loader_data data;
};

/* ----- module search and loading ------------------------------------------ */

/// Type of a module file.
enum module_loader_module_file_type {
    MOD_FILE_NOT_FOUND,
    MOD_FILE_SRC,
    MOD_FILE_NDL,
    MOD_FILE_ASM,
    MOD_FILE_DIR,
};

struct _module_loader_search_state {
    struct zis_context *z;
    struct module_loader_data *d;
    zis_path_char_t *buffer;
};

static int _module_loader_search_fn(const zis_path_char_t *mod_name, void *_arg) {
    struct _module_loader_search_state *const state = _arg;
    struct zis_context *const z = state->z;
    struct module_loader_data *const d = state->d;
    zis_path_char_t *buffer = state->buffer;

    for (size_t i = 0; ; i++) {
        struct zis_object *entry = zis_array_obj_get_checked(d->search_path, i);
        if (!entry)
            break;
        if (!zis_object_type_is(entry, z->globals->type_Path))
            continue;
        struct zis_path_obj *const path_obj = zis_object_cast(entry, struct zis_path_obj);
        const zis_path_char_t *const dir = zis_path_obj_data(path_obj);
        zis_debug_log(
            TRACE, "Loader", "search for %" ZIS_PATH_STR_PRI " in %" ZIS_PATH_STR_PRI,
            mod_name, dir
        );

        const size_t path_len_nex = zis_path_join(buffer, dir, mod_name);
        if (path_len_nex >= ZIS_PATH_MAX - 4)
            continue;

#define FILL_BUF_EXT(EXT_NAME) \
    static_assert(sizeof(ZIS_FILENAME_EXTENSION_##EXT_NAME) - 1 <= 4, ""); \
    zis_path_copy_n(buffer + path_len_nex, ZIS_PATH_STR(ZIS_FILENAME_EXTENSION_##EXT_NAME), sizeof(ZIS_FILENAME_EXTENSION_##EXT_NAME)); \
// ^^^ FILL_BUF_EXT() ^^^

        enum zis_fs_filetype file_type;

#if ZIS_FEATURE_SRC
        FILL_BUF_EXT(SRC)
        file_type = zis_fs_filetype(buffer);
        if (file_type == ZIS_FS_FT_REG)
            return (int)MOD_FILE_SRC;
        else if (file_type == ZIS_FS_FT_DIR)
            return (int)MOD_FILE_DIR;
#endif // ZIS_FEATURE_SRC

        FILL_BUF_EXT(NDL)
        file_type = zis_fs_filetype(buffer);
        if (file_type == ZIS_FS_FT_REG)
            return (int)MOD_FILE_NDL;

#if ZIS_FEATURE_ASM
        FILL_BUF_EXT(ASM)
        file_type = zis_fs_filetype(buffer);
        if (file_type == ZIS_FS_FT_REG)
            return (int)MOD_FILE_ASM;
#endif // ZIS_FEATURE_ASM

#undef FILL_BUF_EXT
    }

    return (int)MOD_FILE_NOT_FOUND;
}

/// Search for the module by name. Returned path needs to be freed.
zis_nodiscard static enum module_loader_module_file_type module_loader_search(
    struct zis_context *z, struct module_loader_data *d,
    zis_path_char_t *path_buf,
    struct zis_symbol_obj *name_sym
) {
    char name_str[64];
    const size_t name_sz = zis_symbol_obj_data_size(name_sym);
    if (name_sz >= sizeof name_str)
        return MOD_FILE_NOT_FOUND; // TODO: handles names longer than 64.
    memcpy(name_str, zis_symbol_obj_data(name_sym), name_sz);
    name_str[name_sz] = 0;

    struct _module_loader_search_state state = { z, d, path_buf };
    const int ret = zis_path_with_temp_path_from_str(name_str, _module_loader_search_fn, &state);
    const enum module_loader_module_file_type ft = (enum module_loader_module_file_type)ret;

    if (ft == MOD_FILE_NOT_FOUND) {
        zis_debug_log(
            WARN, "Loader", "cannot find module file for %s",
            name_str
        );
    } else {
        zis_debug_log(
            INFO, "Loader", "found module file: %" ZIS_PATH_STR_PRI,
            path_buf
        );
    }

    return ft;
}

static enum module_loader_module_file_type
module_loader_load_guess_file_type(const zis_path_char_t *path) {
    zis_path_char_t ext_buf[8];
    const size_t ext_len = zis_path_extension(NULL, path);
    if (ext_len >= 8)
        return MOD_FILE_NOT_FOUND;
    zis_path_extension(ext_buf, path);

#if ZIS_FEATURE_SRC
    if (zis_path_compare(ext_buf, ZIS_PATH_STR(ZIS_FILENAME_EXTENSION_SRC)) == 0) {
        const enum zis_fs_filetype file_type = zis_fs_filetype(path);
        if (file_type == ZIS_FS_FT_REG)
            return MOD_FILE_SRC;
        else if (file_type == ZIS_FS_FT_DIR)
            return MOD_FILE_DIR;
        return MOD_FILE_NOT_FOUND;
    }
#endif // ZIS_FEATURE_SRC

    if (zis_path_compare(ext_buf, ZIS_PATH_STR(ZIS_FILENAME_EXTENSION_NDL)) == 0) {
        const enum zis_fs_filetype file_type = zis_fs_filetype(path);
        if (file_type == ZIS_FS_FT_REG)
            return MOD_FILE_NDL;
        return MOD_FILE_NOT_FOUND;
    }

#if ZIS_FEATURE_ASM
    if (zis_path_compare(ext_buf, ZIS_PATH_STR(ZIS_FILENAME_EXTENSION_ASM)) == 0) {
        const enum zis_fs_filetype file_type = zis_fs_filetype(path);
        if (file_type == ZIS_FS_FT_REG)
            return MOD_FILE_ASM;
        return MOD_FILE_NOT_FOUND;
    }
#endif // ZIS_FEATURE_ASM

    return MOD_FILE_NOT_FOUND;
}

static int _module_loader_path_to_mod_name_fn(const char *name, void *_buf) {
    char *buf = _buf;
    const size_t n = strlen(name);
    if (n >= 64) {
        memcpy(buf, name, 64);
        buf[63] = 0;
    } else {
        memcpy(buf, name, n + 1);
    }
    return 0;
}

static void module_loader_path_to_mod_name(
    char buf[ZIS_PARAMARRAY_STATIC 64], const zis_path_char_t *file
) {
    zis_path_char_t file_stem[64];
    if (zis_path_stem(NULL, file) >= 64) {
        buf[0] = 0;
        return;
    }
    zis_path_stem(file_stem, file);
    zis_path_with_temp_str_from_path(file_stem, _module_loader_path_to_mod_name_fn, buf);
}

/// Load module file. On failure, do thrown (REG-0) and returns false.
static bool module_loader_load_from_file(
    struct zis_context *z,
    const zis_path_char_t *file, enum module_loader_module_file_type file_type,
    struct zis_module_obj *_module
) {
    char mod_name[64]; // TODO: handles names longer than 64.
    module_loader_path_to_mod_name(mod_name, file);

    zis_debug_log(
        INFO, "Loader", "loading module %s from file %" ZIS_PATH_STR_PRI,
        mod_name, file
    );

    zis_locals_decl(
        z, var,
        struct zis_module_obj *module;
        struct zis_func_obj *init_func;
    );
    zis_locals_zero(var);
    var.module = _module;

    int status = ZIS_OK;

    switch (file_type) {
        // In each case, `init_func` shall be assigned, and the module of
        // `init_func` shall be set.

#if ZIS_FEATURE_SRC
    case MOD_FILE_SRC: {
        struct zis_compilation_bundle comp_bundle;
        zis_compilation_bundle_init(&comp_bundle, z);
        const int ff = ZIS_STREAM_OBJ_MODE_IN | ZIS_STREAM_OBJ_TEXT | ZIS_STREAM_OBJ_UTF8;
        struct zis_stream_obj *f = zis_stream_obj_new_file(z, file, ff);
        var.init_func = zis_compile_source(&comp_bundle, f, var.module);
        zis_stream_obj_close(f);
        zis_compilation_bundle_fini(&comp_bundle);
        if (!var.init_func) {
            status = ZIS_THR;
            break;
        }
        break;
    }
#endif // ZIS_FEATURE_SRC

    case MOD_FILE_NDL: {
        zis_dl_handle_t lib = zis_dl_open(file);
        if (!lib) {
            zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
                z, "sys", NULL,
                "not a dynamic library: %" ZIS_PATH_STR_PRI, file
            )));
            status = ZIS_THR;
            break;
        }
        char mod_def_var_name[80] = ZIS_NATIVE_MODULE_VARNAME_PREFIX_STR;
        static_assert(sizeof ZIS_NATIVE_MODULE_VARNAME_PREFIX_STR + sizeof mod_name <= sizeof mod_def_var_name, "");
        strcat(mod_def_var_name, mod_name);
        struct zis_native_module_def *mod_def = zis_dl_get(lib, mod_def_var_name);
        if (!mod_def) {
            zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
                z, "sys", NULL,
                "not a module file: %" ZIS_PATH_STR_PRI, file
            )));
            status = ZIS_THR;
            break;
        }
        // zis_dl_close(lib); // CAN NOT close library here!!
        var.init_func = zis_module_obj_load_native_def(z, var.module, mod_def);
        break;
    }

#if ZIS_FEATURE_ASM
    case MOD_FILE_ASM: {
        const int ff = ZIS_STREAM_OBJ_MODE_IN | ZIS_STREAM_OBJ_TEXT | ZIS_STREAM_OBJ_UTF8;
        struct zis_stream_obj *f = zis_stream_obj_new_file(z, file, ff);
        var.init_func = zis_assemble_func_from_text(z, f, var.module);
        zis_stream_obj_close(f);
        if (!var.init_func) {
            status = ZIS_THR;
            break;
        }
        break;
    }
#endif // ZIS_FEATURE_ASM

    default:
        zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL);
    }

    if (status == ZIS_OK)
        status = zis_module_obj_do_init(z, var.init_func);

    zis_locals_drop(z, var);
    assert(status == ZIS_OK || status == ZIS_THR);
    return status == ZIS_OK;
}

static bool module_loader_load_from_source(
    struct zis_context *z,
    struct zis_stream_obj *input,
    struct zis_module_obj *_module
) {
#if ZIS_FEATURE_SRC

    struct zis_compilation_bundle comp_bundle;
    zis_locals_decl(
        z, var,
        struct zis_module_obj *module;
        struct zis_func_obj *init_func;
    );
    zis_locals_zero(var);
    var.module = _module;
    zis_compilation_bundle_init(&comp_bundle, z);
    var.init_func = zis_compile_source(&comp_bundle, input, var.module);
    zis_compilation_bundle_fini(&comp_bundle);
    int status = ZIS_OK;
    if (!var.init_func)
        status = ZIS_THR;
    else
        status = zis_module_obj_do_init(z, var.init_func);
    zis_locals_drop(z, var);
    assert(status == ZIS_OK || status == ZIS_THR);
    return status == ZIS_OK;

#else // ZIS_FEATURE_SRC

    zis_unused_var(input), zis_unused_var(_module);
    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, "sys", NULL, ""
    )));
    return false;

#endif // ZIS_FEATURE_SRC
}

/// Try to Load an embedded module.
/// Returns whether found and loaded.
static bool module_loader_try_load_from_embedded(
    struct zis_context *z,
    const struct zis_symbol_obj *name_sym,
    struct zis_module_obj *module
) {
    char name_str[64];
    const size_t name_sz = zis_symbol_obj_data_size(name_sym);
    if (name_sz >= sizeof name_str)
        return false; // TODO: handles names longer than 64.
    memcpy(name_str, zis_symbol_obj_data(name_sym), name_sz);
    name_str[name_sz] = 0;

    const struct zis_native_module_def *mod_def = find_embedded_module(name_str);
    if (!mod_def)
        return false;

    int init_status = zis_module_obj_do_init(z, zis_module_obj_load_native_def(z, module, mod_def));
    assert(init_status == ZIS_OK), zis_unused_var(init_status);
    return true;
}

/* ----- public functions --------------------------------------------------- */

struct zis_module_loader *zis_module_loader_create(struct zis_context *z) {
    struct zis_module_loader*const ml = zis_mem_alloc(sizeof(struct zis_module_loader));

    {
        struct zis_object **range[2];
        module_loader_data_as_obj_vec(&ml->data, range);
        zis_object_vec_zero(range[0], range[1] - range[0]);
    }
    zis_objmem_add_gc_root(z, &ml->data, module_loader_data_gc_visitor);

    ml->data.search_path = zis_array_obj_new(z, NULL, 0);
    ml->data.loaded_modules = zis_map_obj_new(z, 0.0f, 8);

    zis_debug_log(TRACE, "Loader", "new module loader %p", (void *)ml);
    return ml;
}

void zis_module_loader_destroy(struct zis_module_loader *ml, struct zis_context *z) {
    zis_debug_log(TRACE, "Loader", "deleting loader %p", (void *)ml);
    zis_objmem_remove_gc_root(z, &ml->data);
    zis_mem_free(ml);
}

void zis_module_loader_add_path(struct zis_context *z, struct zis_path_obj *path) {
    struct module_loader_data *const d = &z->module_loader->data;

    for (size_t i = 0; ; i++) {
        struct zis_object *e = zis_array_obj_get_checked(d->search_path, i);
        if (!e)
            break;
        if (!zis_object_type_is(e, z->globals->type_Path))
            continue;
        struct zis_path_obj *const e_path = zis_object_cast(e, struct zis_path_obj);
        if (zis_path_obj_same(path, e_path))
            return;
    }

    if (!(zis_fs_filetype(zis_path_obj_data(path)) & ZIS_FS_FT_DIR)) {
        zis_debug_log(
            WARN, "Loader", "add_path: not a dir: %" ZIS_PATH_STR_PRI,
            zis_path_obj_data(path)
        );
        return;
    }

    zis_debug_log(
        INFO, "Loader", "add path: %" ZIS_PATH_STR_PRI,
        zis_path_obj_data(path)
    );
    zis_array_obj_append(z, d->search_path, zis_object_from(path));
}

bool zis_module_loader_search(
    struct zis_context *z,
    zis_path_char_t path_buffer[ZIS_PARAMARRAY_STATIC ZIS_PATH_MAX],
    struct zis_symbol_obj *module_name
) {
    const enum module_loader_module_file_type ft =
        module_loader_search(z, &z->module_loader->data, path_buffer, module_name);
    return ft != MOD_FILE_NOT_FOUND;
}

void zis_module_loader_add_loaded(
    struct zis_context *z,
    struct zis_symbol_obj *module_name,
    struct zis_symbol_obj *sub_module_name /* = NULL */,
    struct zis_module_obj *module
) {
    struct module_loader_data *const d = &z->module_loader->data;
    struct zis_context_globals *const g = z->globals;

    struct zis_object *entry = zis_map_obj_sym_get(d->loaded_modules, module_name);
    struct zis_type_obj *entry_type = entry ? zis_object_type_1(entry) : NULL;

    if (sub_module_name) {
        if (entry_type == g->type_Map) {
            zis_map_obj_sym_set(
                z, zis_object_cast(entry, struct zis_map_obj),
                sub_module_name, zis_object_from(module)
            );
        } else {
            zis_locals_decl(
                z, var,
                struct zis_symbol_obj *module_name, *sub_module_name;
                struct zis_module_obj *module;
                struct zis_map_obj *map;
                struct zis_object *old_entry;
            );
            var.module_name = module_name;
            var.sub_module_name = sub_module_name;
            var.module = module;
            var.old_entry = zis_smallint_to_ptr(0);

            bool has_init = false;
            if (entry_type == g->type_Module) {
                var.old_entry = entry;
                has_init = true;
            }

            var.map = zis_map_obj_new(z, 0.0f, 2);
            if (has_init)
                zis_map_obj_set(z, var.map, zis_object_from(g->sym_init), var.old_entry);
            zis_map_obj_set(z, var.map, zis_object_from(var.sub_module_name), zis_object_from(var.module));
            zis_map_obj_set(z, d->loaded_modules, zis_object_from(var.module_name), zis_object_from(var.map));

            zis_locals_drop(z, var);
        }
    } else {
        if (entry_type == g->type_Map) {
            zis_map_obj_sym_set(
                z, zis_object_cast(entry, struct zis_map_obj),
                g->sym_init, zis_object_from(module)
            );
        } else {
            zis_map_obj_sym_set(
                z, d->loaded_modules,
                module_name, zis_object_from(module)
            );
        }
    }
}

struct zis_module_obj *zis_module_loader_get_loaded(
    struct zis_context *z,
    struct zis_symbol_obj *module_name
) {
    struct module_loader_data *const d = &z->module_loader->data;
    struct zis_context_globals *const g = z->globals;

    struct zis_object *entry = zis_map_obj_sym_get(d->loaded_modules, module_name);
    if (!entry)
        return NULL;
    struct zis_type_obj *entry_type = zis_object_type_1(entry);
    if (entry_type == g->type_Module)
        return zis_object_cast(entry, struct zis_module_obj);
    if (entry_type != g->type_Map)
        return NULL;
    entry = zis_map_obj_sym_get(zis_object_cast(entry, struct zis_map_obj), g->sym_init);
    if (!entry)
        return NULL;
    entry_type = zis_object_type_1(entry);
    if (entry_type == g->type_Module)
        return zis_object_cast(entry, struct zis_module_obj);
    return NULL;
}

struct _find_loaded_name_state {
    struct zis_context *z;
    struct zis_type_obj *type_Map;
    struct zis_symbol_obj **name;
    struct zis_module_obj *module;
};

static int _find_loaded_name_fn(struct zis_object *_key, struct zis_object *_val, void *_arg) {
    struct _find_loaded_name_state *const state = _arg;
    if (_val == zis_object_from(state->module)) {
        assert(zis_object_type_is(_key, state->z->globals->type_Symbol));
        state->name[0] = zis_object_cast(_key, struct zis_symbol_obj);
        state->name[1] = NULL;
        return 1;
    } else if (zis_object_type_is(_val, state->type_Map)) {
        struct zis_object *sub_name = zis_map_obj_reverse_lookup(
            state->z, zis_object_cast(_val, struct zis_map_obj),
            zis_object_from(state->module)
        );
        assert(zis_object_type_is(_key, state->z->globals->type_Symbol));
        assert(zis_object_type_is(sub_name, state->z->globals->type_Symbol));
        state->name[0] = zis_object_cast(_key, struct zis_symbol_obj);
        state->name[1] = zis_object_cast(sub_name, struct zis_symbol_obj);
    }
    return 0;
}

bool zis_module_loader_find_loaded_name(
    struct zis_context *z,
    struct zis_symbol_obj *name[ZIS_PARAMARRAY_STATIC 2],
    struct zis_module_obj *module
) {
    name[0] = NULL, name[1] = NULL;
    struct _find_loaded_name_state state = { z, z->globals->type_Map, name, module };
    return zis_map_obj_foreach(z, z->module_loader->data.loaded_modules, _find_loaded_name_fn, &state);
}

static bool _module_loader_load_top(
    struct zis_context *z,
    struct zis_module_obj *_module,
    struct zis_symbol_obj *_module_name
) {
    struct module_loader_data *const d = &z->module_loader->data;

    zis_locals_decl(
        z, var,
        struct zis_module_obj *module;
        struct zis_symbol_obj *module_name;
    );
    var.module = _module;
    var.module_name = _module_name;

    // Maybe it is an embedded module.
    if (module_loader_try_load_from_embedded(z, var.module_name, var.module)) {
        zis_locals_drop(z, var);
        return var.module;
    }

    // Search for the module file.
    zis_path_char_t *path_buffer = zis_path_alloc(ZIS_PATH_MAX);
    const enum module_loader_module_file_type file_type =
        module_loader_search(z, d, path_buffer, var.module_name);
    if (file_type == MOD_FILE_NOT_FOUND) {
        zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
            z, "sys", NULL, "no module named `%.*s'",
            (int)zis_symbol_obj_data_size(var.module_name),
            zis_symbol_obj_data(var.module_name)
        )));
        zis_locals_drop(z, var);
        return false;
    }

    // Load module from the found file.
    const bool ok = module_loader_load_from_file(z, path_buffer, file_type, var.module);

    // Clean up.
    zis_mem_free(path_buffer);
    zis_locals_drop(z, var);

    return ok;
}

static bool _module_loader_load_sub(
    struct zis_context *z, struct zis_module_obj *top_module,
    struct zis_symbol_obj *module_name, struct zis_symbol_obj *sub_module_name,
    int flags
) {
    zis_unused_var(z), zis_unused_var(top_module), zis_unused_var(module_name), zis_unused_var(sub_module_name), zis_unused_var(flags);
    zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL);
}

struct zis_module_obj *zis_module_loader_import(
    struct zis_context *z, struct zis_module_obj *_module /* = NULL */,
    struct zis_symbol_obj *_module_name, struct zis_symbol_obj *_sub_module_name /* = NULL */,
    int flags
) {
    // Check whether the module has been loaded.
    bool found_in_loaded = false;
    if (flags & ZIS_MOD_LDR_SEARCH_LOADED && !_module) {
        _module = zis_module_loader_get_loaded(z, _module_name);
        if (_module) {
            found_in_loaded = true;
            if (!_sub_module_name)
                return _module;
        }
    }

    zis_locals_decl(
        z, var,
        struct zis_module_obj *module;
        struct zis_symbol_obj *module_name, *sub_module_name;
    );
    zis_locals_zero(var);
    var.module_name = _module_name;
    if (_sub_module_name)
        var.sub_module_name = _sub_module_name;
    var.module = _module ? _module : zis_module_obj_new(z, true);

    bool ok = found_in_loaded;

    // Load and save the module.
    if (!found_in_loaded) {
        ok = _module_loader_load_top(z, var.module, var.module_name);
        if (!ok)
            goto do_return;
        if (flags & ZIS_MOD_LDR_UPDATE_LOADED) {
            zis_module_loader_add_loaded(z, var.module_name, NULL, var.module);
        }
    }

    // Load sub-module.
    if (_sub_module_name && !zis_module_obj_get(var.module, var.sub_module_name)) {
        ok = _module_loader_load_sub(z, var.module, var.module_name, var.sub_module_name, flags);
        if (!ok)
            goto do_return;
    }

do_return:
    zis_locals_drop(z, var);
    return ok ? var.module : NULL;
}

struct zis_module_obj *zis_module_loader_import_file(
    struct zis_context *z, struct zis_module_obj *_module /* = NULL */,
    struct zis_path_obj *_file
) {
    enum module_loader_module_file_type file_type =
        module_loader_load_guess_file_type(zis_path_obj_data(_file));
    if (file_type == MOD_FILE_NOT_FOUND) {
        zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
            z, "sys", NULL, "not a module file: %" ZIS_PATH_STR_PRI, zis_path_obj_data(_file)
        )));
        return NULL;
    }

    zis_locals_decl(
        z, var,
        struct zis_path_obj *file;
        struct zis_module_obj *module;
    );
    var.file = _file;
    var.module = _module ? _module : zis_module_obj_new(z, true);

    const bool ok = module_loader_load_from_file(
        z, zis_path_obj_data(var.file), file_type, var.module
    );

    zis_locals_drop(z, var);
    return ok ? var.module : NULL;
}

struct zis_module_obj *zis_module_loader_import_source(
    struct zis_context *z, struct zis_module_obj *_module /* = NULL */,
    struct zis_stream_obj *input
) {
    zis_locals_decl(
        z, var,
        struct zis_module_obj *module;
    );
    var.module = _module ? _module : zis_module_obj_new(z, true);
    const bool ok = module_loader_load_from_source(z, input, var.module);
    zis_locals_drop(z, var);
    return ok ? var.module : NULL;
}
