// Generated from "astdef.txt".

#pragma once

struct zis_ast_node_Nil_data {
    struct zis_object *value;
};

struct zis_ast_node_Bool_data {
    struct zis_bool_obj *value;
};

struct zis_ast_node_Constant_data {
    struct zis_object *value;
};

struct zis_ast_node_Name_data {
    struct zis_symbol_obj *value;
};

struct zis_ast_node_Pos_data {
    struct zis_ast_node_obj *value;
};

struct zis_ast_node_Neg_data {
    struct zis_ast_node_obj *value;
};

struct zis_ast_node_BitNot_data {
    struct zis_ast_node_obj *value;
};

struct zis_ast_node_Not_data {
    struct zis_ast_node_obj *value;
};

struct zis_ast_node_Add_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Sub_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Mul_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Div_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Rem_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Shl_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Shr_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_BitAnd_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_BitOr_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_BitXor_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Assign_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Eq_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Ne_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Lt_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Le_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Gt_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Ge_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_And_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Or_data {
    struct zis_ast_node_obj *lhs;
    struct zis_ast_node_obj *rhs;
};

struct zis_ast_node_Subscript_data {
    struct zis_ast_node_obj *value;
    struct zis_ast_node_obj *key;
};

struct zis_ast_node_Field_data {
    struct zis_ast_node_obj *value;
    struct zis_symbol_obj *name;
};

struct zis_ast_node_Call_data {
    struct zis_ast_node_obj *value;
    struct zis_array_obj *args;
};

struct zis_ast_node_Send_data {
    struct zis_ast_node_obj *target;
    struct zis_symbol_obj *method;
    struct zis_array_obj *args;
};

struct zis_ast_node_Tuple_data {
    struct zis_array_obj *args;
};

struct zis_ast_node_Array_data {
    struct zis_array_obj *args;
};

struct zis_ast_node_Map_data {
    struct zis_array_obj *args;
};

struct zis_ast_node_Module_data {
    struct zis_string_obj *file;
    struct zis_array_obj *body;
};

#define ZIS_AST_NODE_LIST \
    E(Nil            , "value") \
    E(Bool           , "value") \
    E(Constant       , "value") \
    E(Name           , "value") \
    E(Pos            , "value") \
    E(Neg            , "value") \
    E(BitNot         , "value") \
    E(Not            , "value") \
    E(Add            , "lhs,rhs") \
    E(Sub            , "lhs,rhs") \
    E(Mul            , "lhs,rhs") \
    E(Div            , "lhs,rhs") \
    E(Rem            , "lhs,rhs") \
    E(Shl            , "lhs,rhs") \
    E(Shr            , "lhs,rhs") \
    E(BitAnd         , "lhs,rhs") \
    E(BitOr          , "lhs,rhs") \
    E(BitXor         , "lhs,rhs") \
    E(Assign         , "lhs,rhs") \
    E(Eq             , "lhs,rhs") \
    E(Ne             , "lhs,rhs") \
    E(Lt             , "lhs,rhs") \
    E(Le             , "lhs,rhs") \
    E(Gt             , "lhs,rhs") \
    E(Ge             , "lhs,rhs") \
    E(And            , "lhs,rhs") \
    E(Or             , "lhs,rhs") \
    E(Subscript      , "value,key") \
    E(Field          , "value,name") \
    E(Call           , "value,args") \
    E(Send           , "target,method,args") \
    E(Tuple          , "args") \
    E(Array          , "args") \
    E(Map            , "args") \
    E(Module         , "file,body") \
// ^^^ ZIS_AST_NODE_LIST ^^^
