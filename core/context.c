#include "context.h"

#include "debug.h"
#include "memory.h"
#include "objmem.h"
#include "stack.h"

zis_nodiscard struct zis_context *zis_context_create(void) {
    zis_debug_try_init();
    struct zis_context *const z = zis_mem_alloc(sizeof(struct zis_context));
    z->objmem_context = zis_objmem_context_create();
    z->callstack = zis_callstack_create(z);
    zis_debug_log(INFO, "Context", "new context @%p", (void *)z);
    return z;
}

void zis_context_destroy(struct zis_context *z) {
    zis_debug_log(INFO, "Context", "deleting context @%p", (void *)z);
    zis_callstack_destroy(z->callstack, z);
    zis_objmem_context_destroy(z->objmem_context);
    zis_mem_free(z);
}
