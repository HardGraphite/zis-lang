#include "arrayobj.h"

#include <string.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

/* ----- array slots -------------------------------------------------------- */

struct zis_array_slots_obj *zis_array_slots_obj_new(
    struct zis_context *z, struct zis_object *v[], size_t n
) {
    if (zis_unlikely(!n))
        return z->globals->val_empty_array_slots;

    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Array_Slots, 1 + n, 0
    );
    struct zis_array_slots_obj *const self =
        zis_object_cast(obj, struct zis_array_slots_obj);
    assert(zis_array_slots_obj_length(self) == n);

    if (v) {
        memcpy(self->_data, v, n * sizeof(struct zis_object *));
        zis_object_assert_no_write_barrier(self);
    } else {
        memset(self->_data, 0xff, n * sizeof(struct zis_object *));
    }

    return self;
}

struct zis_array_slots_obj *zis_array_slots_obj_new2(
    struct zis_context *z, size_t len,
    struct zis_object *v[], size_t n
) {
    if (zis_unlikely(!len))
        return z->globals->val_empty_array_slots;
    if (zis_unlikely(n > len))
        n = len;

    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Array_Slots, 1 + len, 0
    );
    struct zis_array_slots_obj *const self =
        zis_object_cast(obj, struct zis_array_slots_obj);
    assert(zis_array_slots_obj_length(self) == len);

    assert(v);
    memcpy(self->_data, v, n * sizeof(struct zis_object *));
    zis_object_assert_no_write_barrier(self);
    memset(self->_data + n, 0xff, (len - n) * sizeof(struct zis_object *));

    return self;
}

struct zis_array_slots_obj *_zis_array_slots_obj_new_empty(struct zis_context *z) {
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Array_Slots, 1, 0
    );
    return zis_object_cast(obj, struct zis_array_slots_obj);
}

ZIS_NATIVE_TYPE_DEF_XS_NB(
    Array_Slots,
    struct zis_array_slots_obj,
    NULL, NULL, NULL
);

/* ----- array -------------------------------------------------------------- */

void zis_array_obj_new(
    struct zis_context *z, struct zis_object **ret,
    struct zis_object *v[], size_t n
) {
    struct zis_object *const obj = zis_objmem_alloc(z, z->globals->type_Array);
    *ret = obj;
    struct zis_array_obj *const self = zis_object_cast(obj, struct zis_array_obj);
    self->_data = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier(self);
    self->length = n;
    if (n) {
        self->_data = zis_array_slots_obj_new(z, v, n);
        zis_object_assert_no_write_barrier(self);
    }
}

void zis_array_obj_new2(
    struct zis_context *z, struct zis_object **ret,
    size_t reserve, struct zis_object *v[], size_t n
) {
    struct zis_object *const obj = zis_objmem_alloc(z, z->globals->type_Array);
    *ret = obj;
    struct zis_array_obj *const self = zis_object_cast(obj, struct zis_array_obj);
    self->_data = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier(self);
    self->length = n;
    if (reserve < n)
        reserve = n;
    if (reserve) {
        self->_data = v ?
            zis_array_slots_obj_new2(z, reserve, v, n) :
            zis_array_slots_obj_new(z, NULL, n);
        zis_object_assert_no_write_barrier(self);
    }
}

void zis_array_obj_append(
    struct zis_context *z, struct zis_array_obj *self, struct zis_object *v
) {
    struct zis_array_slots_obj *self_data = self->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);
    const size_t old_len = self->length;

    assert(old_len <= old_cap);
    if (zis_unlikely(old_len == old_cap)) {
        const size_t new_cap = old_cap >= 2 ? old_cap * 2 : 4;
        self_data = zis_array_slots_obj_new2(z, new_cap, self_data->_data, old_len);
        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
    }

    zis_array_slots_obj_set(self_data, old_len, v);
    self->length = old_len + 1;
}

struct zis_object *zis_array_obj_pop(
    struct zis_context *z,
    struct zis_array_obj *self
) {
    struct zis_array_slots_obj *self_data = self->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);
    const size_t old_len = self->length;

    if (zis_unlikely(!old_len))
        return NULL; // empty

    if (zis_unlikely(old_len <= old_cap / 2 && old_len >= 16)) {
        const size_t new_cap = old_len;
        self_data = zis_array_slots_obj_new2(z, new_cap, self_data->_data, old_len);
        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
    }

    const size_t new_len = old_len - 1;
    self->length = new_len;
    struct zis_object *const elem = zis_array_slots_obj_get(self_data, new_len);
    self_data->_data[new_len] = (void *)(uintptr_t)-1;
    assert(zis_object_is_smallint(self_data->_data[new_len]));

    return elem;
}

struct zis_object *zis_array_obj_Mx_get_element(
    struct zis_context *z, const struct zis_array_obj *self,
    struct zis_object *index_obj
) {
    if (zis_object_is_smallint(index_obj)) {
        zis_smallint_t idx = zis_smallint_from_ptr(index_obj);
        const size_t len = self->length;
        assert(len <= ZIS_SMALLINT_MAX);
        const zis_smallint_t len_smi = (zis_smallint_t)len;
        if (idx > 0) {
            idx--;
            if (zis_unlikely(idx >= len_smi))
                return NULL;
        } else {
            if (zis_unlikely(idx == 0))
                return NULL;
            idx = len_smi + idx;
            if (zis_unlikely((size_t)idx >= len))
                return NULL;
        }
        assert(idx >= 0 && idx < len_smi);
        return zis_array_slots_obj_get(self->_data, (size_t)idx);
    }
    zis_unused_var(z);
    return NULL;
}

bool zis_array_obj_Mx_set_element(
    struct zis_context *z, struct zis_array_obj *self,
    struct zis_object *index_obj, struct zis_object *value
) {
    if (zis_object_is_smallint(index_obj)) {
        zis_smallint_t idx = zis_smallint_from_ptr(index_obj);
        const size_t len = self->length;
        assert(len <= ZIS_SMALLINT_MAX);
        const zis_smallint_t len_smi = (zis_smallint_t)len;
        if (idx > 0) {
            idx--;
            if (zis_unlikely(idx >= len_smi))
                return false;
        } else {
            if (zis_unlikely(idx == 0))
                return false;
            idx = len_smi + idx;
            if (zis_unlikely((size_t)idx >= len))
                return false;
        }
        assert(idx >= 0 && idx < len_smi);
        zis_array_slots_obj_set(self->_data, (size_t)idx, value);
        return true;
    }
    zis_unused_var(z);
    return false;
}

ZIS_NATIVE_TYPE_DEF(
    Array,
    struct zis_array_obj, length,
    NULL, NULL, NULL
);
