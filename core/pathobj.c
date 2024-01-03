#include "pathobj.h"

#include <assert.h>
#include <stddef.h>

#include "context.h"
#include "fsutil.h"
#include "globals.h"
#include "ndefutil.h"
#include "object.h"
#include "objmem.h"

struct zis_path_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    size_t _bytes_size;
    size_t _path_len;
    zis_path_char_t _data[];
};

#define PATH_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_path_obj, _bytes_size)

struct zis_path_obj *zis_path_obj_new(
    struct zis_context *z,
    const zis_path_char_t *path, size_t path_len
) {
    if (path_len == (size_t)-1) {
        assert(path);
        path_len = zis_path_len(path);
    }
    struct zis_path_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Path,
            0, PATH_OBJ_BYTES_FIXED_SIZE + (path_len + 1) * sizeof(zis_path_char_t)
        ),
        struct zis_path_obj
    );
    self->_path_len = path_len;
    zis_path_copy_n(self->_data, path, path_len);
    self->_data[path_len] = 0;
    return self;
}

size_t zis_path_obj_path_len(const struct zis_path_obj *self) {
    return self->_path_len;
}

const zis_path_char_t *zis_path_obj_data(const struct zis_path_obj *self) {
    return self->_data;
}

ZIS_NATIVE_TYPE_DEF_XB(
    Path,
    struct zis_path_obj, _bytes_size,
    NULL, NULL, NULL
);
