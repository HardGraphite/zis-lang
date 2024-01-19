//%% [module]
//%% name = prelude
//%% required = YES
//%% force-embedded = YES

#include <stddef.h>

#include <zis.h>
#include <core/attributes.h>
#include <core/context.h>
#include <core/globals.h>
#include <core/object.h>
#include <core/stack.h>

/* ----- builtin types ------------------------------------------------------ */

#pragma pack(push, 1)

static const char *const _prelude_types_name[] = {
#define E(X)  #X ,
    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
#undef E
};

static const uint8_t _prelude_types_index[] = {
#define E(X)  offsetof(struct zis_context_globals, type_##X) / sizeof(void *),
    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
#undef E
};

static_assert(sizeof(struct zis_context_globals) / sizeof(void *) <= UINT8_MAX, "_prelude_types_off");

#pragma pack(pop)

zis_cold_fn static void prelude_load_types(struct zis_context *z) {
    const unsigned int type_count = sizeof _prelude_types_name / sizeof _prelude_types_name[0];
    struct zis_context_globals *const g = z->globals;
    struct zis_object **const fp = z->callstack->frame;
    for (unsigned int i = 0; i < type_count; i++) {
        const char *name = _prelude_types_name[i];
        struct zis_type_obj *obj = ((struct zis_type_obj **)g)[_prelude_types_index[i]];
        fp[0] = zis_object_from(obj);
        zis_store_global(z, 0, name, (size_t)-1);
    }
}

/* ----- define the module -------------------------------------------------- */

zis_cold_fn static int F_init(zis_t z) {
    prelude_load_types(z);
    return ZIS_OK;
}

static const struct zis_native_func_def M_funcs[] = {
    { NULL    , {0, 0, 1}, F_init     },
    { NULL    , {0, 0, 0}, NULL       },
};

ZIS_NATIVE_MODULE(prelude) = {
    .name = "prelude",
    .functions = M_funcs,
    .types = NULL,
};
