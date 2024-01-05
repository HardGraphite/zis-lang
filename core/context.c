#include "context.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "fsutil.h"
#include "globals.h"
#include "loader.h"
#include "memory.h"
#include "objmem.h"
#include "stack.h"
#include "symbolobj.h"
#include "zis.h" // ZIS_PANIC_*

#include "pathobj.h"

#include "zis_config.h"

static void context_read_environ_path(struct zis_context *z) {
#ifdef ZIS_ENVIRON_NAME_PATH

#if ZIS_SYSTEM_WINDOWS
#    define char     wchar_t
#    define strchr   wcschr
#    define getenv   _wgetenv
#endif // ZIS_SYSTEM_WINDOWS

    const char *const var_path = getenv(ZIS_ENVIRON_NAME_PATH);
    if (!var_path)
        return;

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

zis_nodiscard struct zis_context *zis_context_create(void) {
    zis_debug_try_init();

    struct zis_context *const z = zis_mem_alloc(sizeof(struct zis_context));
    memset(z, 0, sizeof *z);

    z->objmem_context = zis_objmem_context_create();
    z->callstack = zis_callstack_create(z);

    // Allow use of `zis_callstack_frame_alloc_temp()` and `zis_callstack_frame_free_temp()`.
    // See `zis_native_block()`.
    zis_callstack_enter(z->callstack, 1, NULL);

    z->symbol_registry = zis_symbol_registry_create(z);
    z->globals = zis_context_globals_create(z);
    z->module_loader = zis_module_loader_create(z);

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
    zis_symbol_registry_destroy(z->symbol_registry, z);
    zis_context_globals_destroy(z->globals, z);
    zis_callstack_destroy(z->callstack, z);
    zis_objmem_context_destroy(z->objmem_context);
    zis_mem_free(z);
}

zis_noreturn void zis_context_panic(struct zis_context *z, enum zis_context_panic_reason r) {
    static_assert(ZIS_PANIC_OOM == (int)ZIS_CONTEXT_PANIC_OOM, "");
    static_assert(ZIS_PANIC_SOV == (int)ZIS_CONTEXT_PANIC_SOV, "");

    zis_debug_log(WARN, "Context", "context@%p: panic(%i)", (void *)z, (int)r);

    if (r != ZIS_CONTEXT_PANIC_ABORT) {
        zis_context_panic_handler_t handler = z->panic_handler;
        if (handler)
            handler(z, (int)r);
    }

    abort();
}
