#include "typeobj.h"

#include "context.h"
#include "debug.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "arrayobj.h"
#include "funcobj.h"
#include "mapobj.h"
#include "symbolobj.h"

/// Allocate but do not initialize.
static struct zis_type_obj *type_obj_alloc(struct zis_context *z) {
    struct zis_object *const obj =
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Type, 0, 0);
    return zis_object_cast(obj, struct zis_type_obj);
}

zis_cold_fn struct zis_type_obj *_zis_type_obj_bootstrap_alloc(
    struct zis_context *z, const struct zis_native_type_def *volatile def
) {
    // See `zis_type_obj_new_r()` and `zis_type_obj_load_native_def()`.

    struct zis_type_obj *self = type_obj_alloc(z);

    self->_methods  = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_array_slots_obj);
    self->_name_map = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_map_obj);
    self->_statics  = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_map_obj);

    self->_slots_num = def->slots_num;
    self->_bytes_len = def->bytes_size;
    const bool extendable = self->_slots_num == (size_t)-1 || self->_bytes_len == (size_t)-1;
    self->_obj_size  = extendable ? 0 : ZIS_OBJECT_HEAD_SIZE + self->_slots_num * sizeof(void *) + self->_bytes_len;

    return self;
}

zis_cold_fn void _zis_type_obj_bootstrap_init_r(
    struct zis_context *z, struct zis_type_obj *self,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]
) {
    // See `zis_type_obj_new_r()`.

    self->_methods = z->globals->val_empty_array_slots;
    zis_object_write_barrier(self, self->_methods);

    regs[0] = zis_object_from(self);
    struct zis_map_obj *map_obj;
    map_obj = zis_map_obj_new_r(z, regs + 1, 0.0f, 0);
    assert(zis_object_type(regs[0]) == z->globals->type_Type);
    self = zis_object_cast(regs[0], struct zis_type_obj);
    self->_name_map = map_obj;
    map_obj = zis_map_obj_new_r(z, regs + 1, 0.0f, 0);
    assert(zis_object_type(regs[0]) == z->globals->type_Type);
    self = zis_object_cast(regs[0], struct zis_type_obj);
    self->_statics = map_obj;
}

struct zis_type_obj *zis_type_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]
) {
    struct zis_type_obj *self = type_obj_alloc(z);

    self->_methods   = z->globals->val_empty_array_slots;
    zis_object_write_barrier(self, self->_methods);
    self->_name_map  = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_map_obj);
    self->_statics   = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_map_obj);
    self->_slots_num = 0;
    self->_bytes_len = 0;
    self->_obj_size  = 0;

    regs[0] = zis_object_from(self);
    struct zis_map_obj *map_obj;
    map_obj = zis_map_obj_new_r(z, regs + 1, 0.0f, 0);
    assert(zis_object_type(regs[0]) == z->globals->type_Type);
    self = zis_object_cast(regs[0], struct zis_type_obj);
    self->_name_map = map_obj;
    map_obj = zis_map_obj_new_r(z, regs + 1, 0.0f, 0);
    assert(zis_object_type(regs[0]) == z->globals->type_Type);
    self = zis_object_cast(regs[0], struct zis_type_obj);
    self->_statics = map_obj;

    return self;
}

zis_noinline static size_t _func_def_arr_len(const struct zis_native_func_def *arr) {
    if (!arr)
        return 0;
    size_t n = 0;
    for (const struct zis_native_func_def *p = arr; p->code; p++, n++);
    return n;
}

void zis_type_obj_load_native_def(
    struct zis_context *z,
    struct zis_type_obj *self, const struct zis_native_type_def *volatile def
) {
    self->_slots_num = def->slots_num;
    self->_bytes_len = def->bytes_size;
    const bool extendable = self->_slots_num == (size_t)-1 || self->_bytes_len == (size_t)-1;
    self->_obj_size = extendable ? 0 : ZIS_OBJECT_HEAD_SIZE + self->_slots_num * sizeof(void *) + self->_bytes_len;

    assert(self->_methods == z->globals->val_empty_array_slots);

    const size_t field_count = def->fields ? def->slots_num : 0;
    const size_t method_count = _func_def_arr_len(def->methods);
    const size_t static_count = _func_def_arr_len(def->statics);
    const size_t name_map_reserve = field_count + method_count;

    const size_t tmp_regs_n = 5;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, tmp_regs_n);

    // ~~ tmp_regs[0] = self, tmp_regs[1..4] = tmp ~~

    tmp_regs[0] = zis_object_from(self);

    if (name_map_reserve) {
        tmp_regs[1] = zis_object_from(self->_name_map);
        zis_map_obj_reserve_r(z, tmp_regs + 1, name_map_reserve);
        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Type);
        self = zis_object_cast(tmp_regs[0], struct zis_type_obj);
    }

    // ~~ tmp_regs[0] = self, tmp_regs[1] = name_map / statics_map, tmp_regs[2] = method_table ~~

    if (field_count) {
        const char *const *const fields = def->fields;
        assert(fields);

        tmp_regs[1] = zis_object_from(self->_name_map);
        for (size_t i = 0; i < field_count; i++) {
            assert(i <= ZIS_SMALLINT_MAX);
            struct zis_object *idx = zis_smallint_to_ptr((zis_smallint_t)i);
            struct zis_symbol_obj *sym = zis_symbol_registry_get(z, fields[i], (size_t)-1);
            assert(zis_object_type(tmp_regs[1]) == z->globals->type_Map);
            zis_map_obj_sym_set(z, zis_object_cast(tmp_regs[1], struct zis_map_obj), sym, idx);
        }

        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Type);
        self = zis_object_cast(tmp_regs[0], struct zis_type_obj);
    }

    if (method_count) {
        const struct zis_native_func_def *const methods = def->methods;
        assert(methods);

        struct zis_array_slots_obj *method_table =
            zis_array_slots_obj_new(z, NULL, method_count);
        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Type);
        self = zis_object_cast(tmp_regs[0], struct zis_type_obj);
        self->_methods = method_table;
        zis_object_write_barrier(self, method_table);

        tmp_regs[2] = zis_object_from(method_table);
        tmp_regs[1] = zis_object_from(self->_name_map);
        for (size_t i = 0; i < method_count; i++) {
            const struct zis_native_func_def *const func_def = &methods[i];
            struct zis_func_obj_meta func_obj_meta;
            if (zis_unlikely(!zis_func_obj_meta_conv(&func_obj_meta, func_def->meta))) {
                zis_debug_log(
                    ERROR, "Loader",
                    "(struct zis_native_func_meta){ .na=%u, .no=%u, .nl=%u }: `.nl' is too big",
                    func_def->meta.na, func_def->meta.no, func_def->meta.nl
                );
                continue;
            }
            struct zis_func_obj *func_obj =
                zis_func_obj_new_native(z, func_obj_meta, func_def->code);
            assert(zis_object_type(tmp_regs[2]) == z->globals->type_Array_Slots);
            zis_array_slots_obj_set(
                zis_object_cast(tmp_regs[2], struct zis_array_slots_obj),
                i, zis_object_from(func_obj)
            );
            if (func_def->name) {
                assert(i <= ZIS_SMALLINT_MAX);
                struct zis_object *idx = zis_smallint_to_ptr(-1 - (zis_smallint_t)i);
                struct zis_symbol_obj *sym = zis_symbol_registry_get(z, func_def->name, (size_t)-1);
                assert(zis_object_type(tmp_regs[1]) == z->globals->type_Map);
                zis_map_obj_sym_set(z, zis_object_cast(tmp_regs[1], struct zis_map_obj), sym, idx);
            }
        }

        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Type);
        self = zis_object_cast(tmp_regs[0], struct zis_type_obj);
    }

    if (static_count) {
        const struct zis_native_func_def *const statics = def->statics;
        assert(statics);

        tmp_regs[1] = zis_object_from(self->_statics);
        for (size_t i = 0; i < static_count; i++) {
            const struct zis_native_func_def *const func_def = &statics[i];
            if (!func_def->name)
                continue;
            struct zis_func_obj_meta func_obj_meta;
            if (zis_unlikely(!zis_func_obj_meta_conv(&func_obj_meta, func_def->meta))) {
                zis_debug_log(
                    ERROR, "Loader",
                    "(struct zis_native_func_meta){ .na=%u, .no=%u, .nl=%u }: `.nl' is too big",
                    func_def->meta.na, func_def->meta.no, func_def->meta.nl
                );
                continue;
            }
            tmp_regs[2] = zis_object_from(zis_func_obj_new_native(z, func_obj_meta, func_def->code));
            struct zis_symbol_obj *sym = zis_symbol_registry_get(z, func_def->name, (size_t)-1);
            assert(zis_object_type(tmp_regs[1]) == z->globals->type_Map);
            zis_map_obj_sym_set(z, zis_object_cast(tmp_regs[1], struct zis_map_obj), sym, tmp_regs[2]);
        }

        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Type);
        self = zis_object_cast(tmp_regs[0], struct zis_type_obj);
    }

    zis_callstack_frame_free_temp(z, tmp_regs_n);
}

size_t zis_type_obj_find_field(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
) {
    struct zis_object *const x = zis_map_obj_sym_get(self->_statics, name);
    if (zis_unlikely(!x))
        return (size_t)-1;
    assert(zis_object_is_smallint(x));
    const zis_smallint_t i = zis_smallint_from_ptr(x);
    if (zis_unlikely(i < 0))
        return (size_t)-1;
    return (size_t)i;
}

size_t zis_type_obj_find_method(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
) {
    struct zis_object *const x = zis_map_obj_sym_get(self->_statics, name);
    if (zis_unlikely(!x))
        return (size_t)-1;
    assert(zis_object_is_smallint(x));
    const zis_smallint_t i = zis_smallint_from_ptr(x);
    if (zis_unlikely(i >= 0))
        return (size_t)-1;
    return (size_t)(-1 - i);
}

void zis_type_obj_set_method_i(
    const struct zis_type_obj *self,
    size_t index, struct zis_object *new_method
) {
    zis_array_slots_obj_set(self->_methods, index, new_method);
}

struct zis_object *zis_type_obj_get_method(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
) {
    // See `zis_type_obj_get_method_i()` and `zis_type_obj_find_method()`.

    struct zis_object *const x = zis_map_obj_sym_get(self->_statics, name);
    if (zis_unlikely(!x))
        return NULL;
    assert(zis_object_is_smallint(x));
    const zis_smallint_t i = zis_smallint_from_ptr(x);
    if (zis_unlikely(i >= 0))
        return NULL;
    return zis_array_slots_obj_get(self->_methods, (size_t)(-1 - i));
}

struct zis_object *zis_type_obj_get_static(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
) {
    return zis_map_obj_sym_get(self->_statics, name);
}

void zis_type_obj_set_static(
    struct zis_context *z, struct zis_type_obj *self,
    struct zis_symbol_obj *name, struct zis_object *value
) {
    zis_map_obj_sym_set(z, self->_statics, name, value);
}

ZIS_NATIVE_FUNC_LIST_DEF(
    type_methods,
);

ZIS_NATIVE_FUNC_LIST_DEF(
    type_statics,
);

ZIS_NATIVE_TYPE_DEF(
    Type,
    struct zis_type_obj, _slots_num,
    NULL,
    ZIS_NATIVE_FUNC_LIST_VAR(type_methods),
    ZIS_NATIVE_FUNC_LIST_VAR(type_statics)
);
