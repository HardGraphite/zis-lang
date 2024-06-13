#include "typeobj.h"

#include "context.h"
#include "debug.h"
#include "globals.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"

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
    struct zis_context *z, const struct zis_native_type_def *restrict def
) {
    // See `zis_type_obj_new()` and `zis_type_obj_load_native_def()`.

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

zis_cold_fn void _zis_type_obj_bootstrap_init(
    struct zis_context *z, struct zis_type_obj *self
) {
    // See `zis_type_obj_new()`.

    self->_methods = z->globals->val_empty_array_slots;
    zis_object_write_barrier(self, self->_methods);

    zis_locals_decl_1(z, var, struct zis_type_obj *self);
    var.self = self;

    var.self->_name_map = zis_map_obj_new(z, 0.0f, 0);
    var.self->_statics = zis_map_obj_new(z, 0.0f, 0);

    self = var.self;
    zis_locals_drop(z, var);
}

struct zis_type_obj *zis_type_obj_new(struct zis_context *z) {
    struct zis_type_obj *self = type_obj_alloc(z);

    self->_methods   = z->globals->val_empty_array_slots;
    zis_object_write_barrier(self, self->_methods);
    self->_name_map  = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_map_obj);
    self->_statics   = zis_object_cast(zis_smallint_to_ptr(ZIS_SMALLINT_MIN), struct zis_map_obj);
    self->_slots_num = 0;
    self->_bytes_len = 0;
    self->_obj_size  = 0;

    zis_locals_decl_1(z, var, struct zis_type_obj *self);
    var.self = self;

    var.self->_name_map = zis_map_obj_new(z, 0.0f, 0);
    var.self->_statics = zis_map_obj_new(z, 0.0f, 0);

    self = var.self;
    zis_locals_drop(z, var);

    return self;
}

zis_noinline static size_t _named_func_def_arr_len(const struct zis_native_func_def__named_ref *restrict arr) {
    if (!arr)
        return 0;
    const struct zis_native_func_def__named_ref *end_p;
    for (end_p = arr; end_p->def; end_p++);
    return (size_t)(end_p - arr);
}

zis_noinline static size_t _named_var_def_arr_len(const struct zis_native_value_def__named *restrict arr) {
    if (!arr)
        return 0;
    const struct zis_native_value_def__named *end_p;
    for (end_p = arr; end_p->name; end_p++);
    return (size_t)(end_p - arr);
}

void zis_type_obj_load_native_def(
    struct zis_context *z,
    struct zis_type_obj *_self, const struct zis_native_type_def *restrict def
) {
    _self->_slots_num = def->slots_num;
    _self->_bytes_len = def->bytes_size;
    const bool extendable = _self->_slots_num == (size_t)-1 || _self->_bytes_len == (size_t)-1;
    _self->_obj_size = extendable ? 0 : ZIS_OBJECT_HEAD_SIZE + _self->_slots_num * sizeof(void *) + _self->_bytes_len;

    assert(_self->_methods == z->globals->val_empty_array_slots);

    const size_t field_count = def->fields ? def->slots_num : 0;
    const size_t method_count = _named_func_def_arr_len(def->methods);
    const size_t static_count = _named_var_def_arr_len(def->statics);
    const size_t name_map_reserve = field_count + method_count;

    zis_locals_decl(
        z, var,
        struct zis_type_obj *self;
        struct zis_map_obj *name_map;
        struct zis_array_slots_obj *method_table;
        struct zis_map_obj *statics_map;
    );
    zis_locals_zero(var);
    var.self = _self;

    if (name_map_reserve)
        zis_map_obj_reserve(z, var.self->_name_map, name_map_reserve);

    if (field_count) {
        const char *const *const fields = def->fields;
        assert(fields);

        var.name_map = var.self->_name_map;

        for (size_t i = 0; i < field_count; i++) {
            const char *const field_name = fields[i];
            if (!field_name)
                continue;
            assert(i <= ZIS_SMALLINT_MAX);
            struct zis_object *idx = zis_smallint_to_ptr((zis_smallint_t)i);
            struct zis_symbol_obj *sym = zis_symbol_registry_get(z, field_name, (size_t)-1);
            zis_map_obj_sym_set(z, var.name_map, sym, idx);
        }
    }

    if (method_count) {
        const struct zis_native_func_def__named_ref *const methods = def->methods;
        assert(methods);

        var.name_map = var.self->_name_map;
        var.method_table = zis_array_slots_obj_new(z, NULL, method_count);
        var.self->_methods = var.method_table;
        zis_object_write_barrier(var.self, var.method_table);

        for (size_t i = 0; i < method_count; i++) {
            const char *const func_name = methods[i].name;
            const struct zis_native_func_def *const func_def = methods[i].def;
            struct zis_func_obj_meta func_obj_meta;
            if (zis_unlikely(!zis_func_obj_meta_conv(&func_obj_meta, func_def->meta))) {
                zis_debug_log(
                    ERROR, "Loader",
                    "(struct zis_native_func_meta){ .na=%u, .no=%u, .nl=%u }: illegal",
                    func_def->meta.na, func_def->meta.no, func_def->meta.nl
                );
                continue;
            }
            zis_array_slots_obj_set(
                var.method_table, i,
                zis_object_from(zis_func_obj_new_native(z, func_obj_meta, func_def->code))
            );
            if (func_name) {
                assert(i <= ZIS_SMALLINT_MAX);
                struct zis_object *idx = zis_smallint_to_ptr(-1 - (zis_smallint_t)i);
                struct zis_symbol_obj *sym = zis_symbol_registry_get(z, func_name, (size_t)-1);
                zis_map_obj_sym_set(z, var.name_map, sym, idx);
            }
        }
    }

    if (static_count) {
        const struct zis_native_value_def__named *const statics = def->statics;
        assert(statics);

        var.statics_map = var.self->_statics;

        for (size_t i = 0; i < static_count; i++) {
            const struct zis_native_value_def__named *const var_def = &statics[i];
            if (zis_unlikely(zis_make_value(z, 0, &var_def->value) != ZIS_OK))
                continue;
            struct zis_symbol_obj *sym = zis_symbol_registry_get(z, var_def->name, (size_t)-1);
            struct zis_object *value = zis_context_get_reg0(z);
            zis_map_obj_sym_set(z, var.statics_map, sym, value);
        }
    }

    zis_locals_drop(z, var);
}

size_t zis_type_obj_find_field(
    const struct zis_type_obj *self,
    struct zis_symbol_obj *name
) {
    struct zis_object *const x = zis_map_obj_sym_get(self->_name_map, name);
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
    struct zis_object *const x = zis_map_obj_sym_get(self->_name_map, name);
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

    struct zis_object *const x = zis_map_obj_sym_get(self->_name_map, name);
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

ZIS_NATIVE_TYPE_DEF(
    Type,
    struct zis_type_obj, _slots_num,
    NULL, NULL, NULL
);
