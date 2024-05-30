#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "globals.h"
#include "loader.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "symbolobj.h"
#include "zis.h" // ZIS_PANIC_*

#include "funcobj.h"
#include "mapobj.h"
#include "moduleobj.h"
#include "pathobj.h"
#include "stringobj.h"
#include "typeobj.h"

#include "zis_config.h"

/* ----- init: load essential builtin modules ------------------------------- */

extern const struct zis_native_module_def ZIS_NATIVE_MODULE_VARNAME(prelude);

zis_cold_fn static void context_load_builtin_modules(struct zis_context *z) {
    const int flags = ZIS_MOD_LDR_UPDATE_LOADED;
    struct zis_module_obj *x;
    x = zis_module_loader_import(
        z, z->globals->val_mod_prelude,
        zis_symbol_registry_get(z, "prelude", (size_t)-1), NULL, flags
    );
    assert(x == z->globals->val_mod_prelude), zis_unused_var(x);
}

/* ----- init: read environment variables ----------------------------------- */

zis_cold_fn static void context_read_environ_path(struct zis_context *z) {
#ifdef ZIS_ENVIRON_NAME_PATH

#if ZIS_SYSTEM_WINDOWS
#    define char       wchar_t
#    define strchr     wcschr
#    define getenv(x)  _wgetenv(ZIS_PATH_STR(x))
#endif // ZIS_SYSTEM_WINDOWS

    const char *const var_path = getenv(ZIS_ENVIRON_NAME_PATH);
    if (!var_path)
        return;

    // syntax="PATH1;PATH2;..."
    for (const char *begin = var_path; ; ) {
        const char *const end = strchr(begin, ';');
        struct zis_path_obj *const path_obj =
            zis_path_obj_new(z, begin, end ? (size_t)(end - begin) : (size_t)-1);
        zis_module_loader_add_path(z, path_obj);
        if (!end)
            break;
        begin = end + 1;
    }

#if ZIS_SYSTEM_WINDOWS
#    undef char
#    undef strchr
#    undef getenv
#endif // ZIS_SYSTEM_WINDOWS

#else // !ZIS_ENVIRON_NAME_PATH

    zis_unused_var(z);

#endif // ZIS_ENVIRON_NAME_PATH
}

zis_cold_fn static void context_read_environ_mems(
    size_t *restrict stack_size,
    struct zis_objmem_options *restrict objmem_opts
) {
    *stack_size = 0;
    memset(objmem_opts, 0, sizeof *objmem_opts);

#ifdef ZIS_ENVIRON_NAME_MEMS

#if ZIS_SYSTEM_WINDOWS
#    define char  wchar_t
#    define getenv(x)  _wgetenv(ZIS_PATH_STR(x))
#    define sscanf(x, y, ...)  swscanf(x, ZIS_PATH_STR(y), __VA_ARGS__)
#endif // ZIS_SYSTEM_WINDOWS

    const char *const var = getenv(ZIS_ENVIRON_NAME_MEMS);
    if (!var)
        return;

    // syntax="STACK_SZ;<heap_opts>", heap_opts="NEW_SPC,OLD_SPC_NEW:OLD_SPC_MAX,BIG_SPC_NEW:BIG_SPC_MAX"
    sscanf(
        var, "%zu;%zu,%zu:%zu,%zu:%zu",
        stack_size, &objmem_opts->new_space_size,
        &objmem_opts->old_space_size_new, &objmem_opts->old_space_size_max,
        &objmem_opts->big_space_size_new, &objmem_opts->big_space_size_max
    );

#if ZIS_SYSTEM_WINDOWS
#    undef char
#    undef getenv
#    undef sscanf
#endif // ZIS_SYSTEM_WINDOWS

#endif // ZIS_ENVIRON_NAME_MEMS
}

/* ----- public functions --------------------------------------------------- */

zis_nodiscard struct zis_context *zis_context_create(void) {
    zis_debug_try_init();

    struct zis_context *const z = zis_mem_alloc(sizeof(struct zis_context));
    memset(z, 0, sizeof *z);

    size_t stack_size;
    struct zis_objmem_options objmem_options;
    context_read_environ_mems(&stack_size, &objmem_options);
    z->objmem_context = zis_objmem_context_create(&objmem_options);
    z->callstack = zis_callstack_create(z, stack_size);
    z->symbol_registry = zis_symbol_registry_create(z);
    zis_locals_root_init(&z->locals_root, z);

    z->globals = zis_context_globals_create(z);
    z->module_loader = zis_module_loader_create(z);

    context_load_builtin_modules(z);
    context_read_environ_path(z);

    assert(!z->panic_handler);

    zis_debug_log(INFO, "Context", "new context @%p", (void *)z);
    return z;
}

void zis_context_destroy(struct zis_context *z) {
    zis_debug_log(INFO, "Context", "deleting context @%p", (void *)z);
    zis_locals_root_fini(&z->locals_root, z);
    zis_module_loader_destroy(z->module_loader, z);
    zis_context_globals_destroy(z->globals, z);
    zis_symbol_registry_destroy(z->symbol_registry, z);
    zis_callstack_destroy(z->callstack, z);
    zis_objmem_context_destroy(z->objmem_context);
    zis_mem_free(z);
}

void zis_context_set_reg0(struct zis_context *z, struct zis_object *v) {
    z->callstack->frame[0] = v;
}

struct zis_object *zis_context_get_reg0(struct zis_context *z) {
    return z->callstack->frame[0];
}

zis_noreturn zis_cold_fn void
zis_context_panic(struct zis_context *z, enum zis_context_panic_reason r) {
    static_assert(ZIS_PANIC_OOM == ZIS_CONTEXT_PANIC_OOM, "");
    static_assert(ZIS_PANIC_SOV == ZIS_CONTEXT_PANIC_SOV, "");
    static_assert(ZIS_PANIC_ILL == ZIS_CONTEXT_PANIC_ILL, "");

    zis_debug_log(
        WARN, "Context", "context@%p: panic(%i:%s)", (void *)z, (int)r,
        r == ZIS_CONTEXT_PANIC_ABORT ? "abort" :
        r == ZIS_CONTEXT_PANIC_OOM ? "out-of-memory" :
        r == ZIS_CONTEXT_PANIC_SOV ? "stack-overflow" :
        r == ZIS_CONTEXT_PANIC_ILL ? "illegal-bytecode" :
        r == ZIS_CONTEXT_PANIC_IMPL ? "not-implemented" :
        "??"
    );

    if (r != ZIS_CONTEXT_PANIC_ABORT && z) {
        zis_context_panic_handler_t handler = z->panic_handler;
        if (handler)
            handler(z, (int)r);
    }

    fprintf(stderr, ZIS_DISPLAY_NAME ": panic(%i)\n", (int)r);
    abort();
}

struct _guess_name_state {
    struct zis_context *z;
    char buffer[80];
    char *external_buffer;
    size_t external_buffer_capacity;
    size_t size;
};

static void _guess_name_init(struct _guess_name_state *restrict state, struct zis_context *z) {
    memset(state, 0, sizeof *state);
    state->z = z;
}

static void _guess_name_fini(struct _guess_name_state *restrict state) {
    if (state->external_buffer)
        zis_mem_free(state->external_buffer);
}

static struct zis_string_obj *_guess_name_gen_str(struct _guess_name_state *restrict state) {
    const char *buffer = state->external_buffer ? state->external_buffer : state->buffer;
    return zis_string_obj_new(state->z, buffer, state->size);
}

static void _guess_name_clear(struct _guess_name_state *restrict state) {
    state->size = 0;
}

static void _guess_name_append(struct _guess_name_state *restrict state, const char *s, size_t n) {
    if (!state->external_buffer) {
        if (state->size + n <= sizeof state->buffer) {
            memcpy(state->buffer + state->size, s, n);
            state->size += n;
            return;
        }
        state->external_buffer_capacity = sizeof state->buffer * 2;
        state->external_buffer = zis_mem_alloc(state->external_buffer_capacity);
    }
    if (state->size + n > state->external_buffer_capacity) {
        size_t new_capacity = state->external_buffer_capacity * 2;
        if (new_capacity < state->size + n)
            new_capacity = state->size + n;
        state->external_buffer_capacity = new_capacity;
        state->external_buffer = zis_mem_realloc(state->external_buffer, new_capacity);
    }
    memcpy(state->external_buffer + state->size, s, n);
    state->size += n;
}

static void _guess_name_append_sym(struct _guess_name_state *restrict state, struct zis_symbol_obj *sym) {
    _guess_name_append(state, zis_symbol_obj_data(sym), zis_symbol_obj_data_size(sym));
}

static void _guess_name_append_char(struct _guess_name_state *restrict state, char c) {
    _guess_name_append(state, &c, 1);
}

static bool _guess_name_of_var_in_mod(
    struct _guess_name_state *restrict state,
    struct zis_module_obj *mod, struct zis_object *var
) {
    for (size_t i = 0, n = zis_array_slots_obj_length(mod->_variables); i < n; i++) {
        if (mod->_variables->_data[i] == var) {
            struct zis_object *name =
                zis_map_obj_reverse_lookup(state->z, mod->_name_map, zis_smallint_to_ptr((zis_smallint_t)i));
            if (!name)
                return false;
            assert(zis_object_type_is(name, state->z->globals->type_Symbol));
            _guess_name_append_sym(state, zis_object_cast(name, struct zis_symbol_obj));
            return true;
        }
    }
    return false;
}

static bool _guess_name_of_type_member_in_mod(
    struct _guess_name_state *restrict state,
    struct zis_module_obj *mod, struct zis_object *var, bool check_methods
) {
    struct zis_type_obj *type_var = NULL;
    struct zis_object *type_member_name = NULL;
    bool var_is_method = false;

    for (size_t i = 0, n = zis_array_slots_obj_length(mod->_variables); i < n; i++) {
        struct zis_object *x = mod->_variables->_data[i];
        if (!zis_object_type_is(x, state->z->globals->type_Type))
            continue;
        type_var = zis_object_cast(x, struct zis_type_obj);
        {
            type_member_name = zis_map_obj_reverse_lookup(state->z, type_var->_statics, var);
            if (type_member_name)
                break;
        }
        if (!check_methods)
            continue;
        for (size_t j = 0, m = zis_array_slots_obj_length(type_var->_methods); j < m; j++) {
            if (type_var->_methods->_data[i] == var) {
                type_member_name =
                    zis_map_obj_reverse_lookup(state->z, type_var->_name_map, zis_smallint_to_ptr(-1 - (zis_smallint_t)j));
                if (type_member_name) {
                    var_is_method = true;
                    i = n;
                    break;
                }
            }
        }
    }
    if (!(type_var && type_member_name))
        return false;
    _guess_name_of_var_in_mod(state, mod, zis_object_from(type_var));
    _guess_name_append_char(state, var_is_method ? ':' : '.');
    assert(zis_object_type_is(type_member_name, state->z->globals->type_Symbol));
    _guess_name_append_sym(state, zis_object_cast(type_member_name, struct zis_symbol_obj));
    return true;
}

static bool _guess_name_of_mod(struct _guess_name_state *restrict state, struct zis_module_obj *var) {
    struct zis_symbol_obj *name[2];
    if (!zis_module_loader_find_loaded_name(state->z, name, var))
        return false;
    _guess_name_append_sym(state, name[0]);
    if (name[1]) {
        _guess_name_append_char(state, '.');
        _guess_name_append_sym(state, name[1]);
    }
    return true;
}

static bool _guess_name_of_type(struct _guess_name_state *restrict state, struct zis_type_obj *var) {
    struct zis_module_obj *module = NULL;
    for (size_t i = 0, n = zis_array_slots_obj_length(var->_methods); i < n; i++) {
        struct zis_object *x = zis_array_slots_obj_get(var->_methods, i);
        if (zis_object_type_is(x, state->z->globals->type_Function)) {
            module = zis_func_obj_module(zis_object_cast(x, struct zis_func_obj));
            break;
        }
    }
    if (module) {
        if (!_guess_name_of_mod(state, module))
            _guess_name_append(state, "??", 2);
        _guess_name_append_char(state, '.');
        if (_guess_name_of_var_in_mod(state, module, zis_object_from(var)))
            return true;
        if (_guess_name_of_type_member_in_mod(state, module, zis_object_from(var), false))
            return true;
    }
    module = state->z->globals->val_mod_prelude;
    _guess_name_clear(state);
    return _guess_name_of_var_in_mod(state, module, zis_object_from(var));
}

static bool _guess_name_of_func(struct _guess_name_state *restrict state, struct zis_func_obj *var) {
    struct zis_module_obj *module = zis_func_obj_module(var);
    if (!_guess_name_of_mod(state, module))
        _guess_name_append(state, "??", 2);
    _guess_name_append_char(state, '.');
    if (_guess_name_of_var_in_mod(state, module, zis_object_from(var)))
        return true;
    if (_guess_name_of_type_member_in_mod(state, module, zis_object_from(var), true))
        return true;
    module = state->z->globals->val_mod_prelude;
    _guess_name_clear(state);
    return _guess_name_of_var_in_mod(state, module, zis_object_from(var));
}

struct zis_string_obj *
zis_context_guess_variable_name(struct zis_context *z, struct zis_object *var) {
    struct zis_context_globals *const g = z->globals;
    struct zis_type_obj *const var_type = zis_object_type_1(var);
    struct _guess_name_state state;
    bool success;
    _guess_name_init(&state, z);
    if (var_type == g->type_Function)
        success = _guess_name_of_func(&state, zis_object_cast(var, struct zis_func_obj));
    else if (var_type == g->type_Type)
        success = _guess_name_of_type(&state, zis_object_cast(var, struct zis_type_obj));
    else if (var_type == g->type_Module)
        success = _guess_name_of_mod(&state, zis_object_cast(var, struct zis_module_obj));
    else
        success = false;
    struct zis_string_obj *str = success ? _guess_name_gen_str(&state) : NULL;
    _guess_name_fini(&state);
    return str;
}
