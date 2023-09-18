#include <zis.h>

#include "context.h"

ZIS_API struct zis_context *zis_create(void) {
    return zis_context_create();
}

ZIS_API void zis_destroy(struct zis_context *z) {
    zis_context_destroy(z);
}
