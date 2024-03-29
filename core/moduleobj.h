/// The `Module` type.

#pragma once

#include "attributes.h"
#include "object.h"

#include "arrayobj.h"

struct zis_context;
struct zis_func_obj;
struct zis_map_obj;
struct zis_native_module_def;
struct zis_symbol_obj;

/// The `Module` object.
struct zis_module_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_map_obj *_name_map; // { name (Symbol) -> var_index (smallint) }
    struct zis_array_slots_obj *_variables; // { variable }
    struct zis_object *_parent; // smallint{0} / Module / Array[Module]
};

/// Create an empty `Module` object.
struct zis_module_obj *zis_module_obj_new(
    struct zis_context *z,
    bool parent_prelude
);

/// Load a native module definition.
/// Returns the initializer function if exists.
zis_nodiscard struct zis_func_obj *zis_module_obj_load_native_def(
    struct zis_context *z,
    struct zis_module_obj *self,
    const struct zis_native_module_def *def
);

/// Register a parent module.
void zis_module_obj_add_parent(
    struct zis_context *z,
    struct zis_module_obj *self, struct zis_module_obj *new_parent
);

/// Iterate over the module parents.
/// The first argument (`mods`) of callback function (`visitor`) is an array of modules,
/// where the first element is the module itself and the second is a parent module.
int zis_module_obj_foreach_parent(
    struct zis_context *z, struct zis_module_obj *self,
    int (*visitor)(struct zis_module_obj *mods[2], void *arg), void *visitor_arg
);

/// Set module global variable by index. No bounds checking.
zis_static_force_inline void zis_module_obj_set_i(
    struct zis_module_obj *self, size_t index, struct zis_object *value
) {
    zis_array_slots_obj_set(self->_variables, index, value);
}

/// Get module global variable by index. No bounds checking.
zis_static_force_inline struct zis_object *zis_module_obj_get_i(
    const struct zis_module_obj *self, size_t index
) {
    return zis_array_slots_obj_get(self->_variables, index);
}

/// Get number of module global variables.
zis_static_force_inline size_t zis_module_obj_var_count(
    const struct zis_module_obj *self
) {
    return zis_array_slots_obj_length(self->_variables);
}

/// Query the index of a module global variable by name. Return -1 if it does not exist.
size_t zis_module_obj_find(
    struct zis_module_obj *self,
    struct zis_symbol_obj *name
);

/// Set module global variable. Returns the variable index.
size_t zis_module_obj_set(
    struct zis_context *z, struct zis_module_obj *self,
    struct zis_symbol_obj *name, struct zis_object *value
);

/// Get module global variable. Return NULL if it does not exist.
struct zis_object *zis_module_obj_get(
    struct zis_module_obj *self,
    struct zis_symbol_obj *name
);

/// Get parent module global variable. Return NULL if it does not exist.
struct zis_object *zis_module_obj_parent_get(
    struct zis_context *z,
    struct zis_module_obj *self, struct zis_symbol_obj *name
);

/// Call module initializer function.
int zis_module_obj_do_init(
    struct zis_context *z,
    struct zis_func_obj *initializer /* = NULL */
);
