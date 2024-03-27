#include "arrayobj.h"

#include "context.h"
#include "globals.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"

/* ----- array slots -------------------------------------------------------- */

/// Allocate but do not initialize slots.
static struct zis_array_slots_obj *
array_slots_obj_alloc(struct zis_context *z, size_t n) {
    struct zis_array_slots_obj *const self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Array_Slots, 1 + n, 0
        ),
        struct zis_array_slots_obj
    );
    assert(zis_array_slots_obj_length(self) == n);
    return self;
}

struct zis_array_slots_obj *zis_array_slots_obj_new(
    struct zis_context *z, struct zis_object *v[], size_t n
) {
    if (zis_unlikely(!n))
        return z->globals->val_empty_array_slots;

    struct zis_array_slots_obj *const self = array_slots_obj_alloc(z, n);

    if (v) {
        zis_object_vec_copy(self->_data, v, n);
        zis_object_write_barrier_n(self, v, n);
    } else {
        zis_object_vec_zero(self->_data, n);
    }

    return self;
}

struct zis_array_slots_obj *zis_array_slots_obj_new2(
    struct zis_context *z, size_t len,
    struct zis_array_slots_obj *_other_slots
) {
    if (zis_unlikely(!len))
        return z->globals->val_empty_array_slots;

    zis_locals_decl_1(z, var, struct zis_array_slots_obj *other_slots);
    var.other_slots = _other_slots;

    struct zis_array_slots_obj *const self = array_slots_obj_alloc(z, len);
    assert(zis_array_slots_obj_length(self) == len);

    struct zis_object **const v = var.other_slots->_data;
    size_t n = zis_array_slots_obj_length(var.other_slots);
    if (n > len)
        n = len;
    zis_object_vec_copy(self->_data, v, n);
    zis_object_write_barrier_n(self, v, n);
    zis_object_vec_zero(self->_data + n, len - n);

    zis_locals_drop(z, var);
    return self;
}

struct zis_array_slots_obj *_zis_array_slots_obj_new_empty(struct zis_context *z) {
    return array_slots_obj_alloc(z, 0);
}

ZIS_NATIVE_TYPE_DEF_XS_NB(
    Array_Slots,
    struct zis_array_slots_obj,
    NULL, NULL, NULL
);

/* ----- array -------------------------------------------------------------- */

struct zis_array_obj *zis_array_obj_new(
    struct zis_context *z,
    struct zis_object *v[], size_t n
) {
    struct zis_array_obj *self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Array),
        struct zis_array_obj
    );
    self->_data = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier(self);
    self->length = n;
    if (n) {
        zis_locals_decl_1(z, var, struct zis_array_obj *self);
        var.self = self;
        struct zis_array_slots_obj *const data = zis_array_slots_obj_new(z, v, n);
        self = var.self;
        zis_locals_drop(z, var);
        self->_data = data;
        zis_object_assert_no_write_barrier(self);
    }
    return self;
}

struct zis_array_obj *zis_array_obj_new2(
    struct zis_context *z,
    size_t reserve, struct zis_object *v[], size_t n
) {
    struct zis_array_obj *self = zis_object_cast(
        zis_objmem_alloc(z, z->globals->type_Array),
        struct zis_array_obj
    );
    self->_data = z->globals->val_empty_array_slots;
    zis_object_assert_no_write_barrier(self);
    self->length = n;
    if (reserve < n)
        reserve = n;
    if (reserve) {
        zis_locals_decl_1(z, var, struct zis_array_obj *self);
        var.self = self;
        struct zis_array_slots_obj *data;
        if (v) {
            data = array_slots_obj_alloc(z, reserve);
            zis_object_vec_copy(data->_data, v, n);
            zis_object_vec_zero(data->_data + n, reserve - n);
        } else {
            data = zis_array_slots_obj_new(z, NULL, n);
        }
        self = var.self;
        zis_locals_drop(z, var);
        self->_data = data;
        zis_object_assert_no_write_barrier(self);
    }
    return self;
}

void zis_array_obj_clear(struct zis_array_obj *self) {
    zis_object_vec_zero(self->_data->_data, self->length);
    self->length = 0;
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

        zis_locals_decl(z, var, struct zis_array_obj *self; struct zis_object *v;);
        var.self = self, var.v = v;
        self_data = zis_array_slots_obj_new2(z, new_cap, self_data);
        self = var.self, v = var.v;
        zis_locals_drop(z, var);

        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
    }

    zis_array_slots_obj_set(self_data, old_len, v);
    self->length = old_len + 1;
}

struct zis_object *zis_array_obj_pop(struct zis_array_obj *self) {
    struct zis_array_slots_obj *self_data = self->_data;
    const size_t old_len = self->length;
    assert(old_len <= zis_array_slots_obj_length(self_data));

    if (zis_unlikely(!old_len))
        return NULL; // empty

    const size_t new_len = old_len - 1;
    self->length = new_len;
    struct zis_object *const elem = zis_array_slots_obj_get(self_data, new_len);
    self_data->_data[new_len] = (void *)(uintptr_t)-1;
    assert(zis_object_is_smallint(self_data->_data[new_len]));

    return elem;
}

bool zis_array_obj_insert(
    struct zis_context *z,
    struct zis_array_obj *self, size_t pos, struct zis_object *v
) {
    const size_t old_len = self->length;
    if (zis_unlikely(pos >= old_len)) {
        if (zis_unlikely(pos > old_len))
            return false;
        zis_array_obj_append(z, self, v);
        return true;
    }

    struct zis_array_slots_obj *self_data = self->_data;
    struct zis_object **old_data = self_data->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);

    assert(old_len <= old_cap);
    if (zis_unlikely(old_len == old_cap)) {
        const size_t new_cap = old_cap >= 2 ? old_cap * 2 : 4;

        zis_locals_decl(z, var, struct zis_array_obj *self; struct zis_object *v;);
        var.self = self, var.v = v;
        self_data = array_slots_obj_alloc(z, new_cap);
        self = var.self, v = var.v;
        zis_locals_drop(z, var);
        old_data = self->_data->_data;

        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
        zis_object_vec_copy(self_data->_data, old_data, pos);
        zis_object_write_barrier_n(self_data, old_data, pos);
    }
    zis_object_vec_move(self_data->_data + pos + 1, old_data + pos, old_len - pos);
    zis_array_slots_obj_set(self_data, pos, v);
    self->length = old_len + 1;
    return true;
}

bool zis_array_obj_remove(
    struct zis_context *z,
    struct zis_array_obj *self, size_t pos
) {
    const size_t old_len = self->length;
    if (zis_unlikely(!old_len || pos >= old_len))
        return false;
    if (zis_unlikely(pos == old_len - 1)) {
        zis_array_obj_pop(self);
        return true;
    }

    struct zis_array_slots_obj *self_data = self->_data;
    struct zis_object **old_data = self_data->_data;
    const size_t old_cap = zis_array_slots_obj_length(self_data);

    if (zis_unlikely(old_len <= old_cap / 2 && old_len >= 16)) {
        const size_t new_cap = old_len;

        zis_locals_decl_1(z, var, struct zis_array_obj *self);
        var.self = self;
        self_data = array_slots_obj_alloc(z, new_cap);
        self = var.self;
        zis_locals_drop(z, var);
        old_data = self->_data->_data;

        self->_data = self_data;
        zis_object_write_barrier(self, self_data);
        zis_object_vec_copy(self_data->_data, old_data, pos);
        zis_object_write_barrier_n(self_data, old_data, pos);
    }
    const size_t new_len = old_len - 1;
    zis_object_vec_move(self_data->_data + pos, old_data + pos + 1, new_len - pos);
    self->length = new_len;
    self_data->_data[new_len] = (void *)(uintptr_t)-1;
    assert(zis_object_is_smallint(self_data->_data[new_len]));
    return true;
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

bool zis_array_obj_Mx_insert_element(
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
            if (zis_unlikely(idx > len_smi))
                return false;
        } else {
            if (zis_unlikely(idx == 0))
                return false;
            idx = len_smi + 1 + idx;
            if (zis_unlikely((size_t)idx > len))
                return false;
        }
        assert(idx >= 0 && idx <= len_smi);
        return zis_array_obj_insert(z, self, (size_t)idx, value);
    }
    zis_unused_var(z);
    return false;
}

bool zis_array_obj_Mx_remove_element(
    struct zis_context *z, struct zis_array_obj *self,
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
                return false;
        } else {
            if (zis_unlikely(idx == 0))
                return false;
            if (idx == -1)
                return zis_array_obj_pop(self) ? true : false;
            idx = len_smi + idx;
            if (zis_unlikely((size_t)idx >= len))
                return false;
        }
        assert(idx >= 0 && idx < len_smi);
        return zis_array_obj_remove(z, self, (size_t)idx);
    }
    zis_unused_var(z);
    return false;
}

ZIS_NATIVE_TYPE_DEF(
    Array,
    struct zis_array_obj, length,
    NULL, NULL, NULL
);
