/// The `Function` type.

#pragma once

#include <stdint.h>

#include "arrayobj.h"
#include "object.h"
#include "zis.h" // zis_native_func_t, struct zis_func_meta

struct zis_context;
struct zis_module_obj;
struct zis_object;

/// Bytecode word.
typedef uint32_t zis_func_obj_bytecode_word_t;

/// The `Function` object. The basic callable object.
struct zis_func_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_array_slots_obj *_symbols;
    struct zis_array_slots_obj *_constants;
    struct zis_module_obj      *_module; // Optional.
    // --- BYTES ---
    size_t _bytes_size;
    struct zis_func_meta         meta;
    zis_native_func_t            native; // Optional.
    zis_func_obj_bytecode_word_t bytecode[]; // Optional.
};

/// Create a `Function` from native function. `module` is optional.
struct zis_func_obj *zis_func_obj_new_native(
    struct zis_context *z,
    struct zis_func_meta meta, zis_native_func_t code
);

/// Create a `Function` from bytecode. `module` is optional.
struct zis_func_obj *zis_func_obj_new_bytecode(
    struct zis_context *z,
    struct zis_func_meta meta,
    const zis_func_obj_bytecode_word_t *code, size_t code_len
);

/// Set parent module of a function. Shall only be used after function created.
void zis_func_obj_set_module(
    struct zis_context *z,
    struct zis_func_obj *self, struct zis_module_obj *mod
);

/// Get parent module of a function.
zis_static_force_inline struct zis_module_obj *
zis_func_obj_module(const struct zis_func_obj *self) {
    return self->_module;
}

/// Get a symbol from function symbol table.
zis_static_force_inline struct zis_object *
zis_func_obj_symbol(const struct zis_func_obj *self, size_t id) {
    return zis_array_slots_obj_get(self->_symbols, id);
}

/// Get a constant from function constant table.
zis_static_force_inline struct zis_object *
zis_func_obj_constant(const struct zis_func_obj *self, size_t id) {
    return zis_array_slots_obj_get(self->_constants, id);
}
