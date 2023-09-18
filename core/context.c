#include "context.h"

#include "memory.h"

static void zis_context_init(struct zis_context *z) {
    zis_unused_var(z);
}

static void zis_context_fini(struct zis_context *z) {
    zis_unused_var(z);
}

zis_nodiscard struct zis_context *zis_context_create(void) {
    struct zis_context *const z = zis_mem_alloc(sizeof(struct zis_context));
    zis_context_init(z);
    return z;
}

void zis_context_destroy(struct zis_context *z) {
    zis_context_fini(z);
    zis_mem_free(z);
}
