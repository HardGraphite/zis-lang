#include <zis.h>

#include "context.h"

ZIS_API zis_t zis_create(void) {
    return zis_context_create();
}

ZIS_API void zis_destroy(zis_t z) {
    zis_context_destroy(z);
}
