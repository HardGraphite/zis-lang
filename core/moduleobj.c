#include "moduleobj.h"

#include "context.h"
#include "debug.h"
#include "globals.h"
#include "invoke.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "funcobj.h"
#include "mapobj.h"
#include "symbolobj.h"
#include "typeobj.h"

struct zis_module_obj *zis_module_obj_new(
    struct zis_context *z,
    bool parent_prelude
) {
    struct zis_module_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Module, 0, 0),
        struct zis_module_obj
    );

    zis_locals_decl_1(z, var, struct zis_module_obj *self);
    var.self = self;

    struct zis_array_slots_obj *const empty_array_slots = z->globals->val_empty_array_slots;
    self->_variables = empty_array_slots;
    zis_object_write_barrier(self, empty_array_slots);
    self->_parent = parent_prelude ? zis_object_from(z->globals->val_mod_prelude) : zis_smallint_to_ptr(0);

    self->_name_map = zis_object_cast(zis_smallint_to_ptr(0), struct zis_map_obj);
    struct zis_map_obj *name_map = zis_map_obj_new(z, 0.0f, 0);
    self = var.self;
    self->_name_map = name_map;
    zis_object_write_barrier(self, name_map);

    zis_locals_drop(z, var);
    return self;
}

zis_nodiscard struct zis_func_obj *zis_module_obj_load_native_def(
    struct zis_context *z,
    struct zis_module_obj *_self,
    const struct zis_native_module_def *def
) {
    zis_locals_decl(
        z, var,
        struct zis_module_obj *self;
        struct zis_func_obj *init_func; // or smallint
        union {
            struct zis_func_obj *temp_func;
            struct zis_type_obj *temp_type;
        };
    );
    var.self = _self;
    var.init_func = zis_object_cast(zis_smallint_to_ptr(0), struct zis_func_obj);
    assert(zis_object_is_smallint(zis_object_from(var.init_func)));
    var.temp_func = var.init_func;

    // Count entries.
    const size_t orig_var_cnt = zis_map_obj_length(_self->_name_map);
    assert(zis_array_slots_obj_length(var.self->_variables) >= orig_var_cnt);
    size_t def_func_cnt = 0, def_type_cnt = 0;
    if (def->functions) {
        for (const struct zis_native_func_def *p = def->functions; p->code; p++) {
            if (p->name)
                def_func_cnt++;
        }
    }
    if (def->types) {
        for (const struct zis_native_type_def *p = def->types; p->name || p->slots_num || p->bytes_size; p++) {
            if (p->name)
                def_type_cnt++;
        }
    }

    // Generate the initializer function.
    if (def->functions) {
        const struct zis_native_func_def *const first_func_def = &def->functions[0];
        if (!first_func_def->name && first_func_def->code && !first_func_def->meta.na && !first_func_def->meta.no) {
            const struct zis_func_obj_meta func_obj_meta = { 0, 0, first_func_def->meta.nl + 1 };
            assert(func_obj_meta.nr != 0);
            struct zis_func_obj *const init_func =
                zis_func_obj_new_native(z, func_obj_meta, first_func_def->code);
            var.init_func = init_func;
            zis_func_obj_set_module(z, init_func, var.self);
        }
    }

    // Reserve memory.
    const size_t var_cnt_max = orig_var_cnt + def_func_cnt + def_type_cnt;
    zis_map_obj_reserve(z, var.self->_name_map, var_cnt_max);
    struct zis_array_slots_obj *new_vars =
        zis_array_slots_obj_new2(z, var_cnt_max, var.self->_variables);
    var.self->_variables = new_vars;
    zis_object_write_barrier(var.self, new_vars);

    // Define functions and types.
    if (def_func_cnt) {
        const struct zis_native_func_def *func_def = def->functions;
        for (size_t i = 0; i < def_func_cnt; func_def++) {
            if (!func_def->name)
                continue;
            i++;
            struct zis_func_obj_meta func_obj_meta;
            if (zis_unlikely(!zis_func_obj_meta_conv(&func_obj_meta, func_def->meta))) {
                zis_debug_log(
                    ERROR, "Loader",
                    "(struct zis_native_func_meta){ .na=%u, .no=%u, .nl=%u }: `.nl' is too big",
                    func_def->meta.na, func_def->meta.no, func_def->meta.nl
                );
                continue;
            }
            var.temp_func = zis_func_obj_new_native(z, func_obj_meta, func_def->code);
            zis_func_obj_set_module(z, var.temp_func, var.self);
            struct zis_symbol_obj *name_sym =
                zis_symbol_registry_get(z, func_def->name, (size_t)-1);
            zis_module_obj_set(z, var.self, name_sym, zis_object_from(var.temp_func));
        }
    }
    if (def_type_cnt) {
        const struct zis_native_type_def *type_def = def->types;
        for (size_t i = 0; i < def_type_cnt; type_def++) {
            if (!type_def->name)
                continue;
            i++;
            var.temp_type = zis_type_obj_new(z);
            zis_type_obj_load_native_def(z, var.temp_type, type_def);
            struct zis_symbol_obj *name_sym =
                zis_symbol_registry_get(z, type_def->name, (size_t)-1);
            zis_module_obj_set(z, var.self, name_sym, zis_object_from(var.temp_type));
        }
    }

    zis_locals_drop(z, var);
    return zis_object_is_smallint(zis_object_from(var.init_func)) ? NULL : var.init_func;
}

void zis_module_obj_add_parent(
    struct zis_context *z,
    struct zis_module_obj *self, struct zis_module_obj *new_parent
) {
    if (self->_parent == zis_smallint_to_ptr(0)) {
        self->_parent = zis_object_from(new_parent);
        zis_object_assert_no_write_barrier(new_parent);
        return;
    }

    assert(!zis_object_is_smallint(self->_parent));
    if (zis_object_type(self->_parent) == z->globals->type_Array) {
        zis_array_obj_append(
            z,
            zis_object_cast(self->_parent, struct zis_array_obj),
            zis_object_from(new_parent)
        );
        return;
    }

    assert(zis_object_type(self->_parent) == z->globals->type_Module);
    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
    tmp_regs[0] = self->_parent, tmp_regs[1] = zis_object_from(new_parent);
    self->_parent = zis_object_from(zis_array_obj_new(z, tmp_regs, 2));
    zis_callstack_frame_free_temp(z, 2);
    zis_object_write_barrier(self, self->_parent);
}

int zis_module_obj_foreach_parent(
    struct zis_context *z, struct zis_module_obj *_self,
    int (*visitor)(struct zis_module_obj *mods[2], void *arg), void *visitor_arg
) {
    if (_self->_parent == zis_smallint_to_ptr(0))
        return 0;

    zis_locals_decl(
        z, var,
        struct zis_module_obj *self;
        struct zis_module_obj *fn_args[2];
    );
    var.self = _self;

    int status = 0;
    assert(!zis_object_is_smallint(_self->_parent));
    if (zis_object_type(_self->_parent) == z->globals->type_Array) {
        for (size_t i = 0; ; i++) {
            struct zis_object *const parent =
                zis_array_obj_get(zis_object_cast(var.self->_parent, struct zis_array_obj), i);
            if (!parent)
                break;
            assert(zis_object_type(parent) == z->globals->type_Module);
            var.fn_args[0] = var.self;
            var.fn_args[1] = zis_object_cast(parent, struct zis_module_obj);
            status = visitor(var.fn_args, visitor_arg);
            if (status)
                break;
        }
    } else {
        assert(zis_object_type(var.self->_parent) == z->globals->type_Module);
        var.fn_args[0] = var.self;
        var.fn_args[1] = zis_object_cast(var.self->_parent, struct zis_module_obj);
        status = visitor(var.fn_args, visitor_arg);
    }

    zis_locals_drop(z, var);
    return status;
}

size_t zis_module_obj_find(
    struct zis_module_obj *self,
    struct zis_symbol_obj *name
) {
    struct zis_object *const index_obj = zis_map_obj_sym_get(self->_name_map, name);
    if (zis_unlikely(!index_obj))
        return (size_t)-1;
    assert(zis_object_is_smallint(index_obj));
    const zis_smallint_t index = zis_smallint_from_ptr(index_obj);
    assert(index >= 0);
    return (size_t)index;
}

size_t zis_module_obj_set(
    struct zis_context *z, struct zis_module_obj *self,
    struct zis_symbol_obj *name, struct zis_object *value
) {
    size_t index;
    struct zis_object *const index_obj = zis_map_obj_sym_get(self->_name_map, name);
    if (zis_likely(index_obj)) {
        assert(zis_object_is_smallint(index_obj));
        const zis_smallint_t index_smi = zis_smallint_from_ptr(index_obj);
        assert(index_smi >= 0);
        index = (size_t)index_smi;
    } else {
        zis_locals_decl(
            z, var,
            struct zis_module_obj *self;
            struct zis_object *value;
        );
        var.self = self;
        var.value = value;

        index = zis_map_obj_length(var.self->_name_map);
        assert(index <= ZIS_SMALLINT_MAX);
        zis_map_obj_sym_set(
            z, var.self->_name_map,
            name, zis_smallint_to_ptr((zis_smallint_t)index)
        );
        const size_t old_vars_cap = zis_array_slots_obj_length(var.self->_variables);
        assert(old_vars_cap >= index);
        if (old_vars_cap == index) {
            struct zis_array_slots_obj *new_vars =
                zis_array_slots_obj_new2(z, old_vars_cap + 4, var.self->_variables);
            var.self->_variables = new_vars;
            zis_object_write_barrier(var.self, new_vars);
        }

        self = var.self, value = var.value;
        zis_locals_drop(z, var);
    }
    zis_array_slots_obj_set(self->_variables, index, value);
    return index;
}

struct zis_object *zis_module_obj_get(
    struct zis_module_obj *self,
    struct zis_symbol_obj *name
) {
    struct zis_object *const index_obj = zis_map_obj_sym_get(self->_name_map, name);
    if (zis_unlikely(!index_obj))
        return NULL;
    assert(zis_object_is_smallint(index_obj));
    const zis_smallint_t index_smi = zis_smallint_from_ptr(index_obj);
    assert(index_smi >= 0);
    return zis_array_slots_obj_get(self->_variables, (size_t)index_smi);
}

int zis_module_obj_do_init(
    struct zis_context *z,
    struct zis_func_obj *initializer
) {
    if (!initializer)
        return ZIS_OK;
    if (!zis_invoke_prepare_va(z, zis_object_from(initializer), NULL, NULL, 0))
        return ZIS_THR;
    return zis_invoke_func(z, initializer);
}

ZIS_NATIVE_TYPE_DEF_NB(
    Module,
    struct zis_module_obj,
    NULL, NULL, NULL
);
