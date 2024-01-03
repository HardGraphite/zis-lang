/// Builtin global variables like types and constants.

#pragma once

struct zis_context;
struct zis_type_obj;

#define _ZIS_BUILTIN_VAL_LIST \
    E(struct zis_nil_obj        , nil               ) \
    E(struct zis_bool_obj       , true              ) \
    E(struct zis_bool_obj       , false             ) \
    E(struct zis_string_obj     , empty_string      ) \
    E(struct zis_tuple_obj      , empty_tuple       ) \
    E(struct zis_array_slots_obj, empty_array_slots ) \
    E(struct zis_module_obj     , common_top_module ) \
// ^^^ _ZIS_BUILTIN_VAL_LIST ^^^

#define _ZIS_BUILTIN_TYPE_LIST \
    /* E(Type) */              \
    E(Array)                   \
    E(Array_Slots)             \
    E(Bool)                    \
    E(Exception)               \
    E(Float)                   \
    E(Function)                \
    E(Int)                     \
    E(Map)                     \
    E(Map_Node)                \
    E(Module)                  \
    E(Nil)                     \
    E(Path)                    \
    E(String)                  \
    E(Symbol)                  \
    E(Tuple)                   \
// ^^^ _ZIS_BUILTIN_TYPE_LIST ^^^

/// Globals. This is a GC root.
struct zis_context_globals {

#define E(TYPE, NAME) TYPE * val_##NAME ;
    _ZIS_BUILTIN_VAL_LIST
#undef E

#define E(NAME) struct zis_type_obj * type_##NAME ;
    _ZIS_BUILTIN_TYPE_LIST
    E(Type)
#undef E

};

/// Create globals.
struct zis_context_globals *zis_context_globals_create(struct zis_context *z);

/// Destroy globals.
void zis_context_globals_destroy(struct zis_context_globals *g, struct zis_context *z);
