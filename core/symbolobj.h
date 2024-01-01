/// The `Symbol` type.

#pragma once

#include "object.h"

struct zis_context;

/* ----- symbol ------------------------------------------------------------- */

/// `Symbol` object. A symbol object holding a specific value is unique.
struct zis_symbol_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size;
    struct zis_symbol_obj *_registry_next; // Nullable. Used by zis_symbol_registry.
    size_t hash; // string hash code
    char   data[]; // symbol string
};

/// Get symbol string (UTF-8).
/// This is not a C-style (NUL-terminated) string. See `zis_symbol_obj_data_size()`.
zis_static_force_inline const char *
zis_symbol_obj_data(const struct zis_symbol_obj *self) {
    return self->data;
}

/// Get size of symbol string (number of bytes).
size_t zis_symbol_obj_data_size(const struct zis_symbol_obj *self);

/// Get symbol hash code.
zis_static_force_inline size_t
zis_symbol_obj_hash(const struct zis_symbol_obj *self) {
    return self->hash;
}

/* ----- symbol registry ---------------------------------------------------- */

/// Symbol registry, managing all existing symbols.
/// This is a GC root.
/// This is a weak-ref collection.
struct zis_symbol_registry;

/// Create a symbol registry.
struct zis_symbol_registry *zis_symbol_registry_create(struct zis_context *z);

/// Destroy a symbol registry.
void zis_symbol_registry_destroy(struct zis_symbol_registry *sr, struct zis_context *z);

/// Create or retrieve a `Symbol` object based on the given UTF-8 string `s`.
/// If `n` is -1, calculate string length with `strlen()`.
struct zis_symbol_obj *zis_symbol_registry_get(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */
);

/// Retrieve a `Symbol` object like `zis_symbol_registry_get()`.
/// Returns NULL if the symbol has not been registered.
struct zis_symbol_obj *zis_symbol_registry_find(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */
);
