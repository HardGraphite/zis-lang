#include "bytesobj.h"

#include <string.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

#define BYTES_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_bytes_obj, _bytes_size)

struct zis_bytes_obj *_zis_bytes_obj_new_empty(struct zis_context *z) {
    struct zis_bytes_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Bytes,
            0, BYTES_OBJ_BYTES_FIXED_SIZE
        ),
        struct zis_bytes_obj
    );
    self->_size = 0;
    return self;
}

/// Create a `Bytes` object.
struct zis_bytes_obj *zis_bytes_obj_new(
    struct zis_context *z,
    const void *restrict data, size_t size
) {
    struct zis_bytes_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Bytes,
            0, BYTES_OBJ_BYTES_FIXED_SIZE + size
        ),
        struct zis_bytes_obj
    );
    self->_size = size;
    if (data)
        memcpy(self->_data, data, size);
    return self;
}

ZIS_NATIVE_TYPE_DEF_XB(
    Bytes,
    struct zis_bytes_obj, _bytes_size,
    NULL, NULL, NULL
);
