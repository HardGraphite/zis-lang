#include "loader.h"

#include <assert.h>
#include <stddef.h>

#include "assembly.h"
#include "attributes.h"
#include "compat.h"
#include "context.h"
#include "debug.h"
#include "fsutil.h"
#include "globals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "platform.h" // ZIS_WORDSIZE
#include "stack.h"

#include "arrayobj.h"
#include "exceptobj.h"
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

static const struct zis_native_module_def *const embedded_module_list[] = {
#define E(NAME)  & ZIS_NATIVE_MODULE_VARNAME(NAME) ,
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

    typedef
#if ZIS_WORDSIZE == 64
    int64_t
#elif ZIS_WORDSIZE == 32
    int32_t
#else
    int
#endif
    _ssize_t;
    static_assert(sizeof(size_t) == sizeof(_ssize_t), "");

    assert(embedded_module_count && embedded_module_count < SIZE_MAX / 2);
    size_t index_l = 0, index_r = embedded_module_count - 1;

    do {
        const size_t index_m = index_l + (index_r - index_l) / 2;
        const struct zis_native_module_def *const def = embedded_module_list[index_m];
        const int diff = strcmp(def->name, name);
        if (diff == 0)
            return def;
        if (diff < 0)
            index_l = index_m + 1;
        else
            index_r = index_m - 1;
    } while ((_ssize_t)index_l <= (_ssize_t)index_r);

    return NULL;

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
    struct zis_object    *temp_var;
};

static void module_loader_data_as_obj_vec(
    struct module_loader_data *d,
    struct zis_object **begin_and_end[ZIS_PARAMARRAY_STATIC 2]
) {
    begin_and_end[0] = (struct zis_object **)d;
    begin_and_end[1] = (struct zis_object **)((char *)d + sizeof(*d));
    assert(begin_and_end[1] - begin_and_end[0] == 3);
}

static void module_loader_data_clear_temp(struct module_loader_data *d) {
    d->temp_var = zis_smallint_to_ptr(0);
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
        struct zis_object *entry = zis_array_obj_get(d->search_path, i);
        if (!entry)
            break;
        if (zis_object_type(entry) != z->globals->type_Path)
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
zis_nodiscard enum module_loader_module_file_type module_loader_search(
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

/// Load module file. The Module object to use shall have been put in loader's `temp_var`.
/// On error, put an exception to the `temp_var` and returns false.
static bool module_loader_load_from_file(
    struct zis_context *z, struct module_loader_data *d,
    const zis_path_char_t *file, enum module_loader_module_file_type file_type
) {
    char mod_name[64]; // TODO: handles names longer than 64.
    module_loader_path_to_mod_name(mod_name, file);

    zis_debug_log(
        INFO, "Loader", "loading module %s from file %" ZIS_PATH_STR_PRI,
        mod_name, file
    );

    assert(zis_object_type(d->temp_var) == z->globals->type_Module);
    struct zis_module_obj *module = zis_object_cast(d->temp_var, struct zis_module_obj);

    switch (file_type) {
    case MOD_FILE_NDL: {
        zis_dl_handle_t lib = zis_dl_open(file);
        if (!lib) {
            d->temp_var = zis_object_from(zis_exception_obj_format(
                z, "sys", NULL,
                "not a dynamic library: %" ZIS_PATH_STR_PRI, file
            ));
            return false;
        }
        char mod_def_var_name[80] = ZIS_NATIVE_MODULE_VARNAME_PREFIX_STR;
        static_assert(sizeof ZIS_NATIVE_MODULE_VARNAME_PREFIX_STR + sizeof mod_name <= sizeof mod_def_var_name, "");
        strcat(mod_def_var_name, mod_name);
        struct zis_native_module_def *mod_def = zis_dl_get(lib, mod_def_var_name);
        if (!mod_def) {
            d->temp_var = zis_object_from(zis_exception_obj_format(
                z, "sys", NULL,
                "not a module file: %" ZIS_PATH_STR_PRI, file
            ));
            return false;
        }
        zis_module_obj_load_native_def(z, module, mod_def);
        // zis_dl_close(lib); // CAN NOT close library here!!
        return true;
    }

#if ZIS_FEATURE_ASM
    case MOD_FILE_ASM: {
        struct zis_assembler *const as = zis_assembler_create(z);
        const int ff = ZIS_STREAM_OBJ_MODE_IN | ZIS_STREAM_OBJ_TEXT | ZIS_STREAM_OBJ_UTF8;
        struct zis_stream_obj *f = zis_stream_obj_new_file(z, file, ff);
        struct zis_exception_obj *exc = zis_assembler_module_from_text(z, as, f, module);
        zis_assembler_destroy(as, z);
        if (exc) {
            d->temp_var = zis_object_from(exc);
            return false;
        }
        return true;
    }
#endif // ZIS_FEATURE_ASM

    default:
        zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
    }
}

/// Try to Load an embedded module.
/// Returns whether found and loaded.
static struct zis_module_obj *module_loader_try_load_from_embedded(
    struct zis_context *z,
    const struct zis_symbol_obj *name_sym
) {
    char name_str[64];
    const size_t name_sz = zis_symbol_obj_data_size(name_sym);
    if (name_sz >= sizeof name_str)
        return NULL; // TODO: handles names longer than 64.
    memcpy(name_str, zis_symbol_obj_data(name_sym), name_sz);
    name_str[name_sz] = 0;

    const struct zis_native_module_def *mod_def = find_embedded_module(name_str);
    if (!mod_def)
        return NULL;

    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
    struct zis_module_obj *module = zis_module_obj_new_r(z, tmp_regs);
    zis_module_obj_load_native_def(z, module, mod_def);
    assert(zis_object_type(tmp_regs[0]) == z->globals->type_Module);
    module = zis_object_cast(tmp_regs[0], struct zis_module_obj);
    zis_callstack_frame_free_temp(z, 2);
    return module;
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
    ml->data.loaded_modules =
        zis_map_obj_new_r(z, (struct zis_object **)&ml->data.loaded_modules, 0.0f, 8);

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
        struct zis_object *e = zis_array_obj_get(d->search_path, i);
        if (!e)
            break;
        if (zis_object_type(e) != z->globals->type_Path)
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
    struct zis_type_obj *entry_type = entry ? zis_object_type(entry) : NULL;

    if (sub_module_name) {
        if (entry_type == g->type_Map) {
            zis_map_obj_sym_set(
                z, zis_object_cast(entry, struct zis_map_obj),
                sub_module_name, zis_object_from(module)
            );
        } else {
            struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 8);
            // ~~ tmp_regs[0] = name, tmp_regs[1] = sub_name, tmp_regs[2] = mod, tmp_regs[3] = map, tmp_regs[4..7] = tmp ~~

            tmp_regs[0] = zis_object_from(module_name),
            tmp_regs[1] = zis_object_from(sub_module_name),
            tmp_regs[2] = zis_object_from(module);

            bool has_init = false;
            if (entry_type == g->type_Module) {
                d->temp_var = entry;
                has_init = true;
            }

            zis_map_obj_new_r(z, &tmp_regs[3], 0.0f, 2);
            if (has_init) {
                tmp_regs[4] = tmp_regs[3],
                tmp_regs[5] = zis_object_from(g->sym_init),
                tmp_regs[6] = d->temp_var;
                zis_map_obj_set_r(z, tmp_regs + 4);
                module_loader_data_clear_temp(d);
            }
            tmp_regs[4] = tmp_regs[3],
            tmp_regs[5] = tmp_regs[1],
            tmp_regs[6] = tmp_regs[2];
            zis_map_obj_set_r(z, tmp_regs + 4);

            tmp_regs[4] = zis_object_from(d->loaded_modules),
            tmp_regs[5] = tmp_regs[0],
            tmp_regs[6] = tmp_regs[3];
            zis_map_obj_set_r(z, tmp_regs + 4);

            zis_callstack_frame_free_temp(z, 4);
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
    struct zis_type_obj *entry_type = zis_object_type(entry);
    if (entry_type == g->type_Module)
        return zis_object_cast(entry, struct zis_module_obj);
    if (entry_type != g->type_Map)
        return NULL;
    entry = zis_map_obj_sym_get(zis_object_cast(entry, struct zis_map_obj), g->sym_init);
    if (!entry)
        return NULL;
    entry_type = zis_object_type(entry);
    if (entry_type == g->type_Module)
        return zis_object_cast(entry, struct zis_module_obj);
    return NULL;
}

static struct zis_module_obj *_module_loader_load_top(
    struct zis_context *z,
    struct zis_module_obj *top_module /* = NULL */,
    struct zis_symbol_obj *module_name
) {
    struct module_loader_data *const d = &z->module_loader->data;

    // Maybe it is an embedded module.
    {
        struct zis_module_obj *mod =
            module_loader_try_load_from_embedded(z, module_name);
        // FIXME: load to `top_module` if given.
        if (mod)
            return mod;
    }

    // Allocate a path buffer.
    zis_path_char_t *path_buffer = zis_path_alloc(ZIS_PATH_MAX);

    // Search for the module file.
    const enum module_loader_module_file_type file_type =
        module_loader_search(z, d, path_buffer, module_name);
    if (file_type == MOD_FILE_NOT_FOUND) {
        z->callstack->frame[0] = zis_object_from(zis_exception_obj_format(
            z, "sys", NULL, "no module named `%.*s'",
            (int)zis_symbol_obj_data_size(module_name),
            zis_symbol_obj_data(module_name)
        ));
        return NULL;
    }
    module_name = NULL; // DO NOT use it anymore!

    // Load module from the found file.
    if (!top_module) {
        struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
        top_module = zis_module_obj_new_r(z, tmp_regs);
        zis_callstack_frame_free_temp(z, 2);
    }
    d->temp_var = zis_object_from(top_module);
    const bool load_file_ok = module_loader_load_from_file(z, d, path_buffer, file_type);

    // Free the path buffer.
    zis_mem_free(path_buffer);

    if (!load_file_ok) {
        assert(zis_object_type(d->temp_var) == z->globals->type_Exception);
        z->callstack->frame[0] = d->temp_var;
        module_loader_data_clear_temp(d);
        return NULL;
    }

    assert(zis_object_type(d->temp_var) == z->globals->type_Module);
    top_module = zis_object_cast(d->temp_var, struct zis_module_obj);
    module_loader_data_clear_temp(d);
    return top_module;
}

static bool _module_loader_load_sub(
    struct zis_context *z, struct zis_module_obj *top_module,
    struct zis_symbol_obj *module_name, struct zis_symbol_obj *sub_module_name,
    int flags
) {
    zis_unused_var(z), zis_unused_var(top_module), zis_unused_var(module_name), zis_unused_var(sub_module_name), zis_unused_var(flags);
    zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
}

struct zis_module_obj *zis_module_loader_import(
    struct zis_context *z, struct zis_module_obj *module /* = NULL */,
    struct zis_symbol_obj *module_name, struct zis_symbol_obj *sub_module_name /* = NULL */,
    int flags
) {
    struct module_loader_data *const d = &z->module_loader->data;

    if (module)
        flags = 0;

    // Check whether the module has been loaded.
    bool found_in_loaded = false;
    if (flags & ZIS_MOD_LDR_SEARCH_LOADED) {
        assert(!module);
        module = zis_module_loader_get_loaded(z, module_name);
        if (module) {
            found_in_loaded = true;
            if (!sub_module_name)
                return module;
        }
    }

    // ~~ tmp_regs = { [0] = module, [1] = module_name, [2] = sub_module_name? } ~~
    const size_t tmp_regs_n = 3;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, tmp_regs_n);

    tmp_regs[1] = zis_object_from(module_name);
    if (sub_module_name)
        tmp_regs[2] = zis_object_from(sub_module_name);

#define PULL_REG_VARS() \
    do {                \
        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Module); \
        module = zis_object_cast(tmp_regs[0], struct zis_module_obj);    \
        assert(zis_object_type(tmp_regs[1]) == z->globals->type_Symbol); \
        module_name = zis_object_cast(tmp_regs[1], struct zis_symbol_obj); \
        if (sub_module_name) {                                           \
            assert(zis_object_type(tmp_regs[2]) == z->globals->type_Symbol); \
            sub_module_name = zis_object_cast(tmp_regs[2], struct zis_symbol_obj); \
        }               \
    } while (0)

    /// Do load.
    if (found_in_loaded) {
        tmp_regs[0] = zis_object_from(module);
    } else {
        module = _module_loader_load_top(z, module, module_name);
        if (!module)
            goto do_return;
        tmp_regs[0] = zis_object_from(module);
        PULL_REG_VARS();
    }

    // Save the loaded module.
    if (!found_in_loaded && (flags & ZIS_MOD_LDR_UPDATE_LOADED)) {
        zis_module_loader_add_loaded(z, module_name, NULL, module);
        PULL_REG_VARS();
    }

    // Initialize the module.
    if (!found_in_loaded) {
        if (zis_module_obj_do_init(z, module) == ZIS_THR)
            goto do_return;
        PULL_REG_VARS();
    }

    // Load sub-module.
    if (sub_module_name && !zis_module_obj_get(module, sub_module_name)) {
        if (!_module_loader_load_sub(z, module, module_name, sub_module_name, flags)) {
            assert(zis_object_type(d->temp_var) == z->globals->type_Exception);
            z->callstack->frame[0] = d->temp_var;
            module_loader_data_clear_temp(d);
            module = NULL;
            goto do_return;
        }
        PULL_REG_VARS();
    }

do_return:
    zis_callstack_frame_free_temp(z, tmp_regs_n);
    return module;
}

struct zis_module_obj *zis_module_loader_import_file(
    struct zis_context *z, struct zis_module_obj *module /* = NULL */,
    struct zis_path_obj *file
) {
    struct module_loader_data *const d = &z->module_loader->data;

    const zis_path_char_t *file_path = zis_path_obj_data(file);
    enum module_loader_module_file_type file_type =
        module_loader_load_guess_file_type(file_path);
    if (file_type == MOD_FILE_NOT_FOUND) {
        z->callstack->frame[0] = zis_object_from(zis_exception_obj_format(
            z, "sys", NULL, "not a module file: %" ZIS_PATH_STR_PRI, file_path
        ));
        return NULL;
    }

    if (!module) {
        d->temp_var = zis_object_from(file);
        struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
        module = zis_module_obj_new_r(z, tmp_regs);
        zis_callstack_frame_free_temp(z, 2);
        file = zis_object_cast(d->temp_var, struct zis_path_obj);
    }
    d->temp_var = zis_object_from(module);
    const bool load_file_ok = module_loader_load_from_file(z, d, file_path, file_type);
    if (!load_file_ok) {
        assert(zis_object_type(d->temp_var) == z->globals->type_Exception);
        z->callstack->frame[0] = d->temp_var;
        module_loader_data_clear_temp(d);
        return NULL;
    }

    assert(zis_object_type(d->temp_var) == z->globals->type_Module);
    module = zis_object_cast(d->temp_var, struct zis_module_obj);
    if (zis_module_obj_do_init(z, module) == ZIS_THR) {
        return NULL;
    }

    assert(zis_object_type(d->temp_var) == z->globals->type_Module);
    module = zis_object_cast(d->temp_var, struct zis_module_obj);
    module_loader_data_clear_temp(d);
    return module;
}
