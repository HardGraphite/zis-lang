/// Builtin global variables like types and constants.

#pragma once

struct zis_context;
struct zis_type_obj;

/// List of special values.
#define _ZIS_BUILTIN_VAL_LIST \
    E(struct zis_nil_obj        , nil               ) \
    E(struct zis_bool_obj       , true              ) \
    E(struct zis_bool_obj       , false             ) \
    E(struct zis_string_obj     , empty_string      ) \
    E(struct zis_bytes_obj      , empty_bytes       ) \
    E(struct zis_tuple_obj      , empty_tuple       ) \
    E(struct zis_array_slots_obj, empty_array_slots ) \
    E(struct zis_module_obj     , mod_prelude       ) \
    E(struct zis_module_obj     , mod_unnamed       ) \
    E(struct zis_stream_obj     , stream_stdin      ) \
    E(struct zis_stream_obj     , stream_stdout     ) \
    E(struct zis_stream_obj     , stream_stderr     ) \
// ^^^ _ZIS_BUILTIN_VAL_LIST ^^^

/// List of type (Type).
#define _ZIS_BUILTIN_TYPE_LIST0 \
    E(Type)                     \
// ^^^ _ZIS_BUILTIN_TYPE_LIST0 ^^^

/// List of types (public).
#define _ZIS_BUILTIN_TYPE_LIST1 \
    E(Array)                    \
    E(Bool)                     \
    E(Bytes)                    \
    E(Exception)                \
    E(Float)                    \
    E(Int)                      \
    E(Map)                      \
    E(Nil)                      \
    E(Path)                     \
    E(Stream)                   \
    E(String)                   \
    E(Symbol)                   \
    E(Tuple)                    \
// ^^^ _ZIS_BUILTIN_TYPE_LIST1 ^^^

/// List of types (internal).
#define _ZIS_BUILTIN_TYPE_LIST2 \
    E(Array_Slots)              \
    E(Function)                 \
    E(Map_Node)                 \
    E(Module)                   \
// ^^^ _ZIS_BUILTIN_TYPE_LIST2 ^^^

/// List of frequently used symbols.
#define _ZIS_BUILTIN_SYM_LIST \
    E(init)                   \
// ^^^ _ZIS_BUILTIN_SYM_LIST ^^^

/// Globals. This is a GC root.
struct zis_context_globals {

#define E(TYPE, NAME) TYPE * val_##NAME ;
    _ZIS_BUILTIN_VAL_LIST
#undef E

#define E(NAME) struct zis_type_obj * type_##NAME ;
    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
    _ZIS_BUILTIN_TYPE_LIST2
#undef E

#define E(NAME) struct zis_symbol_obj * sym_##NAME ;
    _ZIS_BUILTIN_SYM_LIST
#undef E

};

/// Create globals.
struct zis_context_globals *zis_context_globals_create(struct zis_context *z);

/// Destroy globals.
void zis_context_globals_destroy(struct zis_context_globals *g, struct zis_context *z);
