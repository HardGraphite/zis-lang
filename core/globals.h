/// Builtin global variables like types and constants.

#pragma once

struct zis_context;
struct zis_type_obj;

#define _ZIS_BUILTIN_TYPE_LIST \
    /* E(Type) */              \
// ^^^ _ZIS_BUILTIN_TYPE_LIST ^^^

/// Globals. This is a GC root.
struct zis_context_globals {

#define E(NAME) struct zis_type_obj * type_##NAME ;
    _ZIS_BUILTIN_TYPE_LIST
    E(Type)
#undef E

};

/// Create globals.
struct zis_context_globals *zis_context_globals_create(struct zis_context *z);

/// Destroy globals.
void zis_context_globals_destroy(struct zis_context_globals *g, struct zis_context *z);
