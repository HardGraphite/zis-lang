#include "moduleobj.h"

#include "context.h"
#include "debug.h"
#include "globals.h"
#include "invoke.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "exceptobj.h"
#include "funcobj.h"
#include "mapobj.h"
#include "symbolobj.h"
#include "tupleobj.h"
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

zis_noinline static size_t _named_func_def_arr_len(const struct zis_native_func_def__named_ref *restrict arr) {
    if (!arr)
        return 0;
    const struct zis_native_func_def__named_ref *end_p;
    for (end_p = arr; end_p->def; end_p++);
    return (size_t)(end_p - arr);
}

zis_noinline static size_t _named_type_def_arr_len(const struct zis_native_type_def__named_ref *restrict arr) {
    if (!arr)
        return 0;
    const struct zis_native_type_def__named_ref *end_p;
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
    const size_t def_func_cnt = _named_func_def_arr_len(def->functions);
    const size_t def_type_cnt = _named_type_def_arr_len(def->types);
    const size_t def_var_cnt  = _named_var_def_arr_len(def->variables);

    // Generate the initializer function.
    if (def_func_cnt && !def->functions[0].name) {
        const struct zis_native_func_def *const first_func_def = def->functions[0].def;
        const struct zis_func_obj_meta func_obj_meta = { 0, 0, first_func_def->meta.nl + 1 };
        assert(func_obj_meta.nr != 0);
        struct zis_func_obj *const init_func =
            zis_func_obj_new_native(z, func_obj_meta, first_func_def->code);
        var.init_func = init_func;
        zis_func_obj_set_module(z, init_func, var.self);
    }

    // Reserve memory.
    const size_t var_cnt_max = orig_var_cnt + def_func_cnt + def_type_cnt + def_var_cnt;
    zis_map_obj_reserve(z, var.self->_name_map, var_cnt_max);
    struct zis_array_slots_obj *new_vars =
        zis_array_slots_obj_new2(z, var_cnt_max, var.self->_variables);
    var.self->_variables = new_vars;
    zis_object_write_barrier(var.self, new_vars);

    // Define functions and types.
    if (def_func_cnt) {
        const struct zis_native_func_def__named_ref *func_def_list = def->functions;
        for (size_t i = func_def_list[0].name ? 0 : 1; i < def_func_cnt; i++) {
            const char *const func_name = func_def_list[i].name;
            const struct zis_native_func_def *const func_def = func_def_list[i].def;
            assert(func_name);
            struct zis_func_obj_meta func_obj_meta;
            if (zis_unlikely(!zis_func_obj_meta_conv(&func_obj_meta, func_def->meta))) {
                zis_debug_log(
                    ERROR, "Loader",
                    "(struct zis_native_func_meta){ .na=%u, .no=%u, .nl=%u }: illegal",
                    func_def->meta.na, func_def->meta.no, func_def->meta.nl
                );
                continue;
            }
            var.temp_func = zis_func_obj_new_native(z, func_obj_meta, func_def->code);
            zis_func_obj_set_module(z, var.temp_func, var.self);
            struct zis_symbol_obj *name_sym =
                zis_symbol_registry_get(z, func_name, (size_t)-1);
            zis_module_obj_set(z, var.self, name_sym, zis_object_from(var.temp_func));
        }
    }
    if (def_type_cnt) {
        const struct zis_native_type_def__named_ref *type_def_list = def->types;
        for (size_t i = 0; i < def_type_cnt; i++) {
            const char *const type_name = type_def_list[i].name;
            const struct zis_native_type_def *const type_def = type_def_list[i].def;
            var.temp_type = zis_type_obj_new(z);
            zis_type_obj_load_native_def(z, var.temp_type, type_def);
            struct zis_symbol_obj *name_sym = zis_symbol_registry_get(z, type_name, (size_t)-1);
            zis_module_obj_set(z, var.self, name_sym, zis_object_from(var.temp_type));
        }
    }
    if (def_var_cnt) {
        const struct zis_native_value_def__named *var_def_list = def->variables;
        for (size_t i = 0; i < def_type_cnt; i++) {
            const struct zis_native_value_def__named *const var_def = &var_def_list[i];
            if (zis_unlikely(zis_make_value(z, 0, &var_def->value) != ZIS_OK))
                continue;
            struct zis_symbol_obj *name_sym = zis_symbol_registry_get(z, var_def->name, (size_t)-1);
            struct zis_object *value = zis_context_get_reg0(z);
            zis_module_obj_set(z, var.self, name_sym, value);
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
        zis_object_assert_no_write_barrier(self->_parent);
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
                zis_array_obj_get_checked(zis_object_cast(var.self->_parent, struct zis_array_obj), i);
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

struct _parent_get_state {
    struct zis_symbol_obj *name;
    struct zis_object *variable;
};

static int _parent_get_fn(struct zis_module_obj *modules[2], void *_arg) {
    struct _parent_get_state *state = _arg;
    struct zis_object *v = zis_module_obj_get(modules[1], state->name);
    if (!v)
        return 0;
    state->variable = v;
    return 1;
}

struct zis_object *zis_module_obj_parent_get(
    struct zis_context *z,
    struct zis_module_obj *_self, struct zis_symbol_obj *_name
) {
    zis_locals_decl(z, var, struct _parent_get_state state;);
    zis_locals_zero(var);
    var.state.name = _name;
    const bool found = zis_module_obj_foreach_parent(z, _self, _parent_get_fn, &var.state);
    zis_locals_drop(z, var);
    return found ? var.state.variable : NULL;
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

#define assert_arg1_Module(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Module)))

ZIS_NATIVE_FUNC_DEF(T_Module_M_operator_get_fld, z, {2, 0, 2}) {
    /*#DOCSTR# func Module:\'.'(name :: Symbol) :: Any
    Gets global variables. */
    assert_arg1_Module(z);
    struct zis_object **frame = z->callstack->frame;
    if (zis_unlikely(!zis_object_type_is(frame[2], z->globals->type_Symbol))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN, ".", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    struct zis_module_obj *const self = zis_object_cast(frame[1], struct zis_module_obj);
    struct zis_symbol_obj *const name = zis_object_cast(frame[2], struct zis_symbol_obj);
    struct zis_object *val = zis_module_obj_get(self, name);
    if (zis_unlikely(!val)) {
        val = zis_module_obj_parent_get(z, self, name);
        if (!val) {
            frame[0] = zis_object_from(zis_exception_obj_format_common(
                z, ZIS_EXC_FMT_NAME_NOT_FOUND, "variable", frame[2]
            ));
            return ZIS_THR;
        }
    }
    frame[0] = val;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Module_M_operator_set_fld, z, {3, 0, 3}) {
    /*#DOCSTR# func Module:\'.='(name :: Symbol, value :: Any) :: Any
    Updates global variables. */
    assert_arg1_Module(z);
    struct zis_object **frame = z->callstack->frame;
    if (zis_unlikely(!zis_object_type_is(frame[2], z->globals->type_Symbol))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN, ".=", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    struct zis_module_obj *const self = zis_object_cast(frame[1], struct zis_module_obj);
    struct zis_symbol_obj *const name = zis_object_cast(frame[2], struct zis_symbol_obj);
    zis_module_obj_set(z, self, name, frame[3]);
    frame[0] = frame[3];
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Module_M_list_vars, z, {1, 0, 1}) {
    /*#DOCSTR# func Module:list_vars() :: Array[Tuple[Symbol, Object]]
    Lists the variables in the module. Returns an array of name-value pairs. */
    assert_arg1_Module(z);
    struct zis_object **frame = z->callstack->frame;
    zis_locals_decl(
        z, var,
        struct zis_module_obj *self;
        struct zis_array_obj  *list;
        struct zis_object     *pair[2];
    );
    zis_locals_zero(var);
    var.self = zis_object_cast(frame[1], struct zis_module_obj);
    const size_t var_count = zis_array_slots_obj_length(var.self->_variables);
    var.list = zis_array_obj_new2(z, var_count, NULL, 0);
    for (size_t i = 0; i < var_count; i++) {
        var.pair[0] = zis_map_obj_reverse_lookup(z, var.self->_name_map, zis_smallint_to_ptr((zis_smallint_t)i));
        if (!var.pair[0])
            break;
        var.pair[1] = zis_array_slots_obj_get(var.self->_variables, i);
        assert(var.pair[0]), assert(var.pair[1]);
        zis_array_obj_append(z, var.list, zis_object_from(zis_tuple_obj_new(z, var.pair, 2)));
    }
    frame[0] = zis_object_from(var.list);
    zis_locals_drop(z, var);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_module_D_methods,
    { "."           , &T_Module_M_operator_get_fld  },
    { ".="          , &T_Module_M_operator_set_fld  },
    { "list_vars"   , &T_Module_M_list_vars         },
);

ZIS_NATIVE_TYPE_DEF_NB(
    Module,
    struct zis_module_obj,
    NULL, T_module_D_methods, NULL
);
