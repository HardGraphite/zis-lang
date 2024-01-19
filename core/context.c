#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "fsutil.h"
#include "globals.h"
#include "loader.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "symbolobj.h"
#include "zis.h" // ZIS_PANIC_*

#include "moduleobj.h"
#include "pathobj.h"

#include "zis_config.h"

/* ----- init: load essential builtin modules ------------------------------- */

extern const struct zis_native_module_def ZIS_NATIVE_MODULE_VARNAME(prelude);

zis_cold_fn static void context_load_builtin_modules(struct zis_context *z) {
    int status;

    zis_module_obj_load_native_def(z, z->globals->val_mod_prelude, &ZIS_NATIVE_MODULE_VARNAME(prelude));
    status = zis_module_obj_do_init(z, z->globals->val_mod_prelude);
    zis_unused_var(status), assert(status == ZIS_OK);
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

    // Allow use of `zis_callstack_frame_alloc_temp()` and `zis_callstack_frame_free_temp()`.
    // See `zis_native_block()`.
    zis_callstack_enter(z->callstack, 1, NULL);

    z->globals = zis_context_globals_create(z);
    z->module_loader = zis_module_loader_create(z);

    context_load_builtin_modules(z);
    context_read_environ_path(z);

    zis_callstack_leave(z->callstack);
    assert(zis_callstack_empty(z->callstack));

    assert(!z->panic_handler);

    zis_debug_log(INFO, "Context", "new context @%p", (void *)z);
    return z;
}

void zis_context_destroy(struct zis_context *z) {
    zis_debug_log(INFO, "Context", "deleting context @%p", (void *)z);
    zis_module_loader_destroy(z->module_loader, z);
    zis_context_globals_destroy(z->globals, z);
    zis_symbol_registry_destroy(z->symbol_registry, z);
    zis_callstack_destroy(z->callstack, z);
    zis_objmem_context_destroy(z->objmem_context);
    zis_mem_free(z);
}

zis_noreturn void zis_context_panic(struct zis_context *z, enum zis_context_panic_reason r) {
    static_assert(ZIS_PANIC_OOM == (int)ZIS_CONTEXT_PANIC_OOM, "");
    static_assert(ZIS_PANIC_SOV == (int)ZIS_CONTEXT_PANIC_SOV, "");
    static_assert(ZIS_PANIC_ILL == (int)ZIS_CONTEXT_PANIC_ILL, "");

    zis_debug_log(WARN, "Context", "context@%p: panic(%i)", (void *)z, (int)r);

    if (r != ZIS_CONTEXT_PANIC_ABORT) {
        zis_context_panic_handler_t handler = z->panic_handler;
        if (handler)
            handler(z, (int)r);
    }

    abort();
}
