// Generated from "astdef.txt".

#pragma once

struct zis_ast_node_Constant_data {
    struct zis_object *value;
};

struct zis_ast_node_Name_data {
    struct zis_symbol_obj *value;
};

#define ZIS_AST_NODE_LIST \
    E(Constant       , "value") \
    E(Name           , "value") \
// ^^^ ZIS_AST_NODE_LIST ^^^
