#include "globals.h"

#include <string.h>

#include "context.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "typeobj.h"

// Declare type definitions.
#define E(NAME)  extern const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR(NAME);
_ZIS_BUILTIN_TYPE_LIST
E(Type)
#undef E

/// Initialize type globals.
static void globals_init_types(struct zis_context_globals *g, struct zis_context *z) {
    void *const orig_z_g = z->globals;
    z->globals = g;

    // Make type for Type.
    struct zis_type_obj dummy_type_Type;
    dummy_type_Type._slots_num = ZIS_NATIVE_TYPE_VAR(Type).slots_num;
    dummy_type_Type._bytes_len = ZIS_NATIVE_TYPE_VAR(Type).bytes_size;
    dummy_type_Type._obj_size  =
        ZIS_OBJECT_HEAD_SIZE + dummy_type_Type._slots_num + dummy_type_Type._bytes_len;
    assert(dummy_type_Type._slots_num != (size_t)-1 && dummy_type_Type._bytes_len != (size_t)-1);
    g->type_Type = &dummy_type_Type; // Make `zis_type_obj_from_native_def()` work.
    zis_type_obj_from_native_def(z, (struct zis_object **)&g->type_Type, &ZIS_NATIVE_TYPE_VAR(Type));
    assert(zis_object_type(zis_object_from(g->type_Type)) == &dummy_type_Type);
    zis_object_meta_set_type_ptr(g->type_Type->_meta, g->type_Type); // Replace with real type.

    // Make other types.
#define E(NAME)  \
    zis_type_obj_from_native_def( \
        z, (struct zis_object **)& g->type_##NAME, &ZIS_NATIVE_TYPE_VAR(Type)) \
    );
    _ZIS_BUILTIN_TYPE_LIST
#undef E

    z->globals = orig_z_g;
}

/// GC visitor. See `zis_objmem_object_visitor_t`.
static void globals_gc_visitor(void *_g, enum zis_objmem_obj_visit_op op) {
    struct zis_object **const gv = (struct zis_object **)_g;
    for (size_t i = 0, n = sizeof(struct zis_context_globals) / sizeof(void *); i < n; i++) {
        struct zis_object **p = gv + i;
        zis_objmem_visit_object(*p, op);
    }
}

struct zis_context_globals *zis_context_globals_create(struct zis_context *z) {
    struct zis_context_globals *const g = zis_mem_alloc(sizeof(struct zis_context_globals));
    memset(g, 0xff, sizeof *g); // Fill globals with small integers.
    zis_objmem_add_gc_root(z, g, globals_gc_visitor);
    globals_init_types(g, z);
    return g;
}

void zis_context_globals_destroy(struct zis_context_globals *g, struct zis_context *z) {
    zis_objmem_remove_gc_root(z, g);
    zis_mem_free(g);
}
