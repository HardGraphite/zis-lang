#include "tupleobj.h"

#include <string.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

/// Allocate.
struct zis_tuple_obj *tuple_obj_alloc(
    struct zis_context *z, struct zis_object **ret, size_t n
) {
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Tuple, 1 + n, 0
    );
    *ret = obj;
    struct zis_tuple_obj *const self = zis_object_cast(obj, struct zis_tuple_obj);
    assert(zis_tuple_obj_length(self) == n);
    return self;
}

void zis_tuple_obj_new(
    struct zis_context *z, struct zis_object **ret,
    struct zis_object *v[], size_t n
) {
    struct zis_tuple_obj *const self = tuple_obj_alloc(z, ret, n);
    if (zis_likely(v)) {
        memcpy(self->_data, v, n * sizeof(struct zis_object *));
        zis_object_assert_no_write_barrier(self);
    } else {
        memset(self->_data, 0xff, n * sizeof(void *));
    }
}

struct zis_tuple_obj *_zis_tuple_obj_new_empty(struct zis_context *z) {
    struct zis_object *o;
    return tuple_obj_alloc(z, &o, 0);
}

struct zis_object *zis_tuple_obj_Mx_get_element(
    struct zis_context *z, const struct zis_tuple_obj *self,
    struct zis_object *index_obj
) {
    if (zis_object_is_smallint(index_obj)) {
        zis_smallint_t idx = zis_smallint_from_ptr(index_obj);
        assert(zis_object_is_smallint(self->_slots_num));
        const zis_smallint_t len = zis_smallint_from_ptr(self->_slots_num) - 1;
        assert(len >= 0);
        if (idx > 0) {
            idx--;
            if (zis_unlikely(idx >= len))
                return NULL;
        } else {
            if (zis_unlikely(idx == 0))
                return NULL;
            idx = len + idx;
            if (zis_unlikely((size_t)idx >= (size_t)len))
                return NULL;
        }
        assert(idx >= 0 && idx < len);
        return self->_data[(size_t)idx];
    }
    zis_unused_var(z);
    return NULL;
}

ZIS_NATIVE_TYPE_DEF_XS_NB(
    Tuple, struct zis_tuple_obj,
    NULL, NULL, NULL
);
