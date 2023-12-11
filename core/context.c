#include "context.h"

#include <assert.h>
#include <stdlib.h>

#include "debug.h"
#include "globals.h"
#include "memory.h"
#include "objmem.h"
#include "stack.h"
#include "zis.h" // ZIS_PANIC_*

zis_nodiscard struct zis_context *zis_context_create(void) {
    zis_debug_try_init();
    struct zis_context *const z = zis_mem_alloc(sizeof(struct zis_context));
    z->objmem_context = zis_objmem_context_create();
    z->callstack = zis_callstack_create(z);
    z->globals = zis_context_globals_create(z);
    z->panic_handler = NULL;
    zis_debug_log(INFO, "Context", "new context @%p", (void *)z);
    return z;
}

void zis_context_destroy(struct zis_context *z) {
    zis_debug_log(INFO, "Context", "deleting context @%p", (void *)z);
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
