#include "moduleobj.h"

#include "context.h"
#include "globals.h"
#include "invoke.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "funcobj.h"
#include "mapobj.h"
#include "symbolobj.h"
#include "typeobj.h"

struct zis_module_obj *zis_module_obj_new_r(
    struct zis_context *z,
    struct zis_object *regs[ZIS_PARAMARRAY_STATIC 2]
) {
    // ~~ regs[0] = module, regs[1] = tmp ~~

    struct zis_module_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Module, 0, 0),
        struct zis_module_obj
    );
    regs[0] = zis_object_from(self);

    struct zis_array_slots_obj *const empty_array_slots = z->globals->val_empty_array_slots;
    self->_variables = empty_array_slots;
    self->_functions = empty_array_slots;
    zis_object_write_barrier(self, empty_array_slots);
    self->_parent = zis_smallint_to_ptr(0);

    self->_name_map = zis_object_cast(zis_smallint_to_ptr(0), struct zis_map_obj);
    struct zis_map_obj *name_map = zis_map_obj_new_r(z, regs + 1, 0.0f, 0);
    self = zis_object_cast(regs[0], struct zis_module_obj);
    self->_name_map = name_map;
    zis_object_write_barrier(self, name_map);

    return self;
}

void zis_module_obj_load_native_def(
    struct zis_context *z,
    struct zis_module_obj *self,
    const struct zis_native_module_def *def
) {
    const size_t tmp_regs_n = 5;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, tmp_regs_n);

    // ~~ tmp_regs[0] = mod, tmp_regs[1...4] = tmp ~~

    tmp_regs[0] = zis_object_from(self);

    const size_t orig_var_cnt = zis_map_obj_length(self->_name_map);
    assert(zis_array_slots_obj_length(self->_variables) >= orig_var_cnt);
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

    // ~~ tmp_regs[0] = mod, tmp_regs[1] = func_table ~~

    if (def->functions) {
        const struct zis_native_func_def *const first_func_def = &def->functions[0];
        if (!first_func_def->name && first_func_def->code) {
            struct zis_array_slots_obj *func_table = zis_array_slots_obj_new(z, NULL, 1);
            tmp_regs[1] = zis_object_from(func_table);
            struct zis_object *const init_func = zis_object_from(
                zis_func_obj_new_native(z, first_func_def->meta, first_func_def->code)
            );
            assert(zis_object_type(tmp_regs[1]) == z->globals->type_Array_Slots);
            func_table = zis_object_cast(tmp_regs[1], struct zis_array_slots_obj);
            zis_array_slots_obj_set(func_table, 0, init_func);
            assert(zis_object_type(tmp_regs[0]) == z->globals->type_Module);
            self = zis_object_cast(tmp_regs[0], struct zis_module_obj);
            self->_functions = func_table;
            zis_object_write_barrier(self, func_table);
        }
    }

    // ~~ tmp_regs[1] = name_map, tmp_regs[2...4] = tmp ~~

    const size_t var_cnt_max = orig_var_cnt + def_func_cnt + def_type_cnt;
    tmp_regs[1] = zis_object_from(self->_name_map);
    zis_map_obj_reserve_r(z, tmp_regs + 1, var_cnt_max);
    self = zis_object_cast(tmp_regs[0], struct zis_module_obj);
    zis_array_slots_obj_new2(z, var_cnt_max, self->_variables);

    // ~~ tmp_regs[1] = name_map, tmp_regs[2] = vars / funcs, tmp_regs[3] = name, tmp_regs[4] = func / type ~~

    self = zis_object_cast(tmp_regs[0], struct zis_module_obj);
    tmp_regs[1] = zis_object_from(self->_name_map);
    tmp_regs[2] = zis_object_from(self->_variables);
    if (def_func_cnt) {
        const struct zis_native_func_def *func_def = def->functions;
        for (size_t i = 0; i < def_func_cnt;) {
            if (!func_def->name)
                continue;
            i++;
            tmp_regs[4] = zis_object_from(zis_func_obj_new_native(
                z, func_def->meta, func_def->code
            ));
            zis_func_obj_set_module(
                z,
                zis_object_cast(tmp_regs[4], struct zis_func_obj),
                zis_object_cast(tmp_regs[0], struct zis_module_obj)
            );
            zis_module_obj_set(
                z,
                zis_object_cast(tmp_regs[0], struct zis_module_obj),
                zis_symbol_registry_get(z, func_def->name, (size_t)-1),
                tmp_regs[4]
            );
        }
    }
    if (def_type_cnt) {
        const struct zis_native_type_def *type_def = def->types;
        for (size_t i = 0; i < def_type_cnt;) {
            if (!type_def->name)
                continue;
            i++;
            zis_type_obj_from_native_def(z, &tmp_regs[4], type_def);
            zis_module_obj_set(
                z,
                zis_object_cast(tmp_regs[0], struct zis_module_obj),
                zis_symbol_registry_get(z, type_def->name, (size_t)-1),
                tmp_regs[4]
            );
        }
    }

    zis_callstack_frame_free_temp(z, tmp_regs_n);
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
    struct zis_context *z, struct zis_module_obj *self,
    int (*visitor)(struct zis_module_obj *mods[2], void *arg), void *visitor_arg
) {
    if (self->_parent == zis_smallint_to_ptr(0))
        return 0;

    int status = 0;
    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
    // ~~ tmp_regs[0,2] = self, tmp_regs[1] = parent ~~
    tmp_regs[2] = zis_object_from(self);

    assert(!zis_object_is_smallint(self->_parent));
    if (zis_object_type(self->_parent) == z->globals->type_Array) {
        for (size_t i = 0; ; i++) {
            struct zis_object *const parent =
                zis_array_obj_get(zis_object_cast(self->_parent, struct zis_array_obj), i);
            if (!parent)
                break;
            assert(zis_object_type(parent) == z->globals->type_Module);
            tmp_regs[0] = tmp_regs[2];
            tmp_regs[1] = parent;
            status = visitor((struct zis_module_obj **)tmp_regs, visitor_arg);
            if (status)
                break;
        }
    } else {
        assert(zis_object_type(self->_parent) == z->globals->type_Module);
        tmp_regs[0] = tmp_regs[2];
        tmp_regs[1] = self->_parent;
        status = visitor((struct zis_module_obj **)tmp_regs, visitor_arg);
    }

    zis_callstack_frame_free_temp(z, 3);
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

void zis_module_obj_set(
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
        struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
        tmp_regs[0] = zis_object_from(self), tmp_regs[1] = value;
        index = zis_map_obj_length(self->_name_map);
        assert(index <= ZIS_SMALLINT_MAX);
        zis_map_obj_sym_set(
            z, self->_name_map,
            name, zis_smallint_to_ptr((zis_smallint_t)index)
        );
        self = zis_object_cast(tmp_regs[0], struct zis_module_obj);
        const size_t old_vars_cap = zis_array_slots_obj_length(self->_variables);
        assert(old_vars_cap >= index);
        if (old_vars_cap == index) {
            struct zis_array_slots_obj *new_vars =
                zis_array_slots_obj_new2(z, old_vars_cap + 4, self->_variables);
            self = zis_object_cast(tmp_regs[0], struct zis_module_obj);
            self->_variables = new_vars;
            zis_object_write_barrier(self, new_vars);
        }
        value = tmp_regs[1];
        zis_callstack_frame_free_temp(z, 2);
    }
    zis_array_slots_obj_set(self->_variables, index, value);
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

struct zis_func_obj *zis_module_obj_function(
    const struct zis_module_obj *self, size_t index
) {
    struct zis_array_slots_obj *const func_table = self->_functions;
    if (zis_unlikely(index >= zis_array_slots_obj_length(func_table)))
        return NULL;
    struct zis_object *f = zis_array_slots_obj_get(func_table, index);
    if (zis_unlikely(zis_object_is_smallint(f)))
        return NULL;
    // assert(zis_object_type(f) == z->globals->type_Function);
    return zis_object_cast(f, struct zis_func_obj);
}

int zis_module_obj_do_init(
    struct zis_context *z,
    struct zis_module_obj *self
) {
    struct zis_func_obj *mod_init_fn = zis_module_obj_function(self, 0);
    if (!mod_init_fn)
        return ZIS_OK;

    int status;

    struct zis_object **const tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
    tmp_regs[0] = zis_object_from(self);
    tmp_regs[1] = zis_object_from(self);

    if (!zis_invoke_prepare_va(z, zis_object_from(mod_init_fn), tmp_regs, 1)) {
        tmp_regs[0] = z->callstack->frame[0];
        status = ZIS_THR;
    } else if (zis_invoke_func(z, mod_init_fn) == ZIS_THR) {
        // TODO: add to traceback.
        zis_invoke_cleanup(z);
        tmp_regs[0] = z->callstack->frame[0];
        status = ZIS_THR;
    } else {
        tmp_regs[0] = zis_invoke_cleanup(z);
        status = ZIS_OK;
    }

    assert(zis_object_type(tmp_regs[1]) == z->globals->type_Module);
    self = zis_object_cast(tmp_regs[1], struct zis_module_obj);

    if (zis_array_slots_obj_length(self->_functions) == 1) {
        self->_functions = z->globals->val_empty_array_slots;
        zis_object_write_barrier(self, self->_functions);
    } else {
        zis_array_slots_obj_set(self->_functions, 0, zis_smallint_to_ptr(0));
    }

    zis_callstack_frame_free_temp(z, 2);

    return status;
}

ZIS_NATIVE_TYPE_DEF_NB(
    Module,
    struct zis_module_obj,
    NULL, NULL, NULL
);
