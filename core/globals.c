#include "globals.h"

#include <string.h>

#include "attributes.h"
#include "compat.h"
#include "context.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "arrayobj.h"
#include "boolobj.h"
#include "bytesobj.h"
#include "moduleobj.h"
#include "nilobj.h"
#include "stringobj.h"
#include "symbolobj.h"
#include "tupleobj.h"
#include "typeobj.h"

#define E(NAME)  extern const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR(NAME);
_ZIS_BUILTIN_TYPE_LIST0
_ZIS_BUILTIN_TYPE_LIST1
_ZIS_BUILTIN_TYPE_LIST2
#undef E

/// Alloc types. See `_zis_type_obj_bootstrap_alloc()`.
zis_cold_fn static void _init_types_0(
    struct zis_context_globals *g, struct zis_context *z
) {
    struct zis_type_obj dummy_type_Type;
    memset(&dummy_type_Type, 0, sizeof dummy_type_Type);
    dummy_type_Type._slots_num = ZIS_NATIVE_TYPE_VAR(Type).slots_num;
    dummy_type_Type._bytes_len = ZIS_NATIVE_TYPE_VAR(Type).bytes_size;
    dummy_type_Type._obj_size  = // See `zis_type_obj_load_native_def()`.
        ZIS_OBJECT_HEAD_SIZE + dummy_type_Type._slots_num * sizeof(void *) + dummy_type_Type._bytes_len;
    assert(dummy_type_Type._slots_num != (size_t)-1 && dummy_type_Type._bytes_len != (size_t)-1);
    g->type_Type = &dummy_type_Type; // Make `zis_objmem_alloc()` work.
    g->type_Type = _zis_type_obj_bootstrap_alloc(z, &ZIS_NATIVE_TYPE_VAR(Type));
    assert(zis_object_type(zis_object_from(g->type_Type)) == &dummy_type_Type);
    zis_object_meta_set_type_ptr(g->type_Type->_meta, g->type_Type); // Replace with the real type.

#define E(NAME) \
    g->type_##NAME = _zis_type_obj_bootstrap_alloc(z, &ZIS_NATIVE_TYPE_VAR(NAME));

    _ZIS_BUILTIN_TYPE_LIST1
    _ZIS_BUILTIN_TYPE_LIST2

#undef E
}

/// Initialize type objects. See `_zis_type_obj_bootstrap_init()`.
zis_cold_fn static void _init_types_1(
    struct zis_context_globals *g, struct zis_context *z
) {
#define E(NAME) \
    _zis_type_obj_bootstrap_init(z, g->type_##NAME);

    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
    _ZIS_BUILTIN_TYPE_LIST2

#undef E
}

/// Load types.
zis_cold_fn static void _init_types_2(
    struct zis_context_globals *g, struct zis_context *z
) {
#define E(NAME) \
    zis_type_obj_load_native_def(z, g->type_##NAME, &ZIS_NATIVE_TYPE_VAR(NAME));

    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
    _ZIS_BUILTIN_TYPE_LIST2

#undef E
}

/// Initialize simple values.
zis_cold_fn static void _init_values_0(
    struct zis_context_globals *g, struct zis_context *z
) {
    g->val_nil = _zis_nil_obj_new(z);
    g->val_true = _zis_bool_obj_new(z, true);
    g->val_false = _zis_bool_obj_new(z, false);
    g->val_empty_string = _zis_string_obj_new_empty(z);
    g->val_empty_bytes = _zis_bytes_obj_new_empty(z);
    g->val_empty_tuple = _zis_tuple_obj_new_empty(z);
    g->val_empty_array_slots = _zis_array_slots_obj_new_empty(z);
}

/// Initialize the rest values.
zis_cold_fn static void _init_values_1(
    struct zis_context_globals *g, struct zis_context *z
) {
    g->val_mod_prelude = zis_module_obj_new(z, false);
    g->val_mod_unnamed = zis_module_obj_new(z, true);
}

/// Initialize symbols.
zis_cold_fn static void _init_symbols(
    struct zis_context_globals *g, struct zis_context *z
) {

#define E(NAME) g-> sym_##NAME = zis_symbol_registry_get(z, #NAME, (size_t)-1);

    _ZIS_BUILTIN_SYM_LIST

#undef E

}

/// Do initialization.
zis_cold_fn static void globals_init(struct zis_context_globals *g, struct zis_context *z) {
    assert(!z->globals);
    z->globals = g;

    // ## 1. Allocate types but do not initialize. The type objects are
    // not complete yet, but should be safe to be the argument of `zis_objmem_alloc()`.
    _init_types_0(g, z);

    // ## 2. Create simple global values. Some of them will be used to during
    // the initialization of the type objects.
    _init_values_0(g, z);

    // ## 3. Initialize type objects. They are complete now.
    _init_types_1(g, z);

    // ## 4. Create the rest global values. Some of them will be used when
    // loading type definitions of the type objects.
    _init_values_1(g, z);

    // ## 5. Load type definitions of the type objects.
    _init_types_2(g, z);

    // ## 6. Create other objects.
    _init_symbols(g, z);

    z->globals = NULL;
}

/// GC visitor. See `zis_objmem_object_visitor_t`.
static void globals_gc_visitor(void *_g, enum zis_objmem_obj_visit_op op) {
    struct zis_context_globals *const g = _g;
    const size_t go_n = sizeof(*g) / sizeof(struct zis_object *);
    struct zis_object **const go_begin = (struct zis_object **)g;
    struct zis_object **const go_end   = go_begin + go_n;
    zis_objmem_visit_object_vec(go_begin, go_end, op);
}

zis_cold_fn struct zis_context_globals *zis_context_globals_create(struct zis_context *z) {
    struct zis_context_globals *const g = zis_mem_alloc(sizeof(struct zis_context_globals));
    memset(g, 0xff, sizeof *g); // Fill globals with small integers.
    zis_objmem_add_gc_root(z, g, globals_gc_visitor);
    globals_init(g, z);
    return g;
}

zis_cold_fn void zis_context_globals_destroy(struct zis_context_globals *g, struct zis_context *z) {
    zis_objmem_remove_gc_root(z, g);
    zis_mem_free(g);
}
