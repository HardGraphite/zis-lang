// Generated from "astdef.txt".

#pragma once

struct zis_ast_node_Constant_data {
    struct zis_object *value;
};

struct zis_ast_node_Name_data {
    struct zis_symbol_obj *value;
};

struct zis_ast_node_Module_data {
    struct zis_string_obj *file;
    struct zis_array_obj *body;
};

#define ZIS_AST_NODE_LIST \
    E(Constant       , "value") \
    E(Name           , "value") \
    E(Module         , "file,body") \
// ^^^ ZIS_AST_NODE_LIST ^^^
