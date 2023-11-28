#include "context.h"

#include "debug.h"
#include "memory.h"
#include "objmem.h"

zis_nodiscard struct zis_context *zis_context_create(void) {
    zis_debug_try_init();
    struct zis_context *const z = zis_mem_alloc(sizeof(struct zis_context));
    z->objmem_context = zis_objmem_context_create();
    zis_debug_log(INFO, "Context", "new context @%p", (void *)z);
    return z;
}

void zis_context_destroy(struct zis_context *z) {
    zis_debug_log(INFO, "Context", "deleting context @%p", (void *)z);
    zis_objmem_context_destroy(z->objmem_context);
    zis_mem_free(z);
}
