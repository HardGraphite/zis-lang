/// The `Function` type.

#pragma once

#include <stdint.h>

#include "arrayobj.h"
#include "object.h"
#include "zis.h" // zis_native_func_t

struct zis_context;
struct zis_module_obj;
struct zis_object;
struct zis_symbol_obj;

/// Bytecode word.
typedef uint32_t zis_func_obj_bytecode_word_t;

/// Function metadata.
struct zis_func_obj_meta {
    uint8_t  na; ///< Number of arguments (excluding optional ones). See `struct zis_native_func_meta::na`.
    int8_t   no; ///< Number of optional arguments. See `struct zis_native_func_meta::no`.
    uint16_t nr; ///< Number of registers (arguments and local variables, including REG-0).
};

/// `-no`, assuming that `no` is negative.
zis_static_force_inline uint8_t zis_func_obj_meta_no_neg2pos(int8_t meta_no) {
    return (uint8_t)0 - (uint8_t)meta_no;
}

/// `abs(no)`.
zis_static_force_inline uint8_t zis_func_obj_meta_no_abs(int8_t meta_no) {
    return meta_no >= 0 ? (uint8_t)meta_no : zis_func_obj_meta_no_neg2pos(meta_no);
}

/// Convert func meta.
zis_nodiscard bool zis_func_obj_meta_conv(
    struct zis_func_obj_meta *dst_func_obj_meta,
    struct zis_native_func_meta src_func_def_meta
);

/// The `Function` object. The basic callable object.
/// Functions with bytecode will not be moved by the GC system.
struct zis_func_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_array_slots_obj *_symbols;
    struct zis_array_slots_obj *_constants;
    struct zis_module_obj      *_module; // Optional.
    // --- BYTES ---
    size_t _bytes_size;
    struct zis_func_obj_meta     meta;
    zis_native_func_t            native; // Optional.
    zis_func_obj_bytecode_word_t bytecode[]; // Optional.
};

/// Create a `Function` from native function.
struct zis_func_obj *zis_func_obj_new_native(
    struct zis_context *z,
    struct zis_func_obj_meta meta, zis_native_func_t code
);

/// Create a `Function` from bytecode.
struct zis_func_obj *zis_func_obj_new_bytecode(
    struct zis_context *z,
    struct zis_func_obj_meta meta,
    const zis_func_obj_bytecode_word_t *code, size_t code_len
);

/// Set module's of a function. Both `symbols` and `constants` can be NULL
/// Shall only be used immediately after function created.
void zis_func_obj_set_resources(
    struct zis_func_obj *self,
    struct zis_array_slots_obj *symbols /*=NULL*/, struct zis_array_slots_obj *constants /*=NULL*/
);

/// Set parent module of a function. Shall only be used immediately after function created.
void zis_func_obj_set_module(
    struct zis_context *z,
    struct zis_func_obj *self, struct zis_module_obj *mod
);

/// Get parent module of a function.
zis_static_force_inline struct zis_module_obj *
zis_func_obj_module(const struct zis_func_obj *self) {
    return self->_module;
}

/// Get the length of the symbol table.
zis_static_force_inline size_t
zis_func_obj_symbol_count(const struct zis_func_obj *self) {
    return zis_array_slots_obj_length(self->_symbols);
}

/// Get a symbol from function symbol table.
zis_static_force_inline struct zis_symbol_obj *
zis_func_obj_symbol(const struct zis_func_obj *self, size_t id) {
    struct zis_object *sym = zis_array_slots_obj_get(self->_symbols, id);
    return zis_object_cast(sym, struct zis_symbol_obj);
}

/// Get the length of the constant table.
zis_static_force_inline size_t
zis_func_obj_constant_count(const struct zis_func_obj *self) {
    return zis_array_slots_obj_length(self->_constants);
}

/// Get a constant from function constant table.
zis_static_force_inline struct zis_object *
zis_func_obj_constant(const struct zis_func_obj *self, size_t id) {
    return zis_array_slots_obj_get(self->_constants, id);
}

/// Get the number of instructions in the bytecode sequence.
size_t zis_func_obj_bytecode_length(const struct zis_func_obj *self);
