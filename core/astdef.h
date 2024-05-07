// Generated from "astdef.txt".

#pragma once

struct zis_ast_node_Nil_data {
    struct zis_object *_;
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

struct zis_ast_node_Import_data {
    struct zis_ast_node_obj *value;
};

struct zis_ast_node_Return_data {
    struct zis_object *value;
};

struct zis_ast_node_Throw_data {
    struct zis_object *value;
};

struct zis_ast_node_Break_data {
    struct zis_object *_;
};

struct zis_ast_node_Continue_data {
    struct zis_object *_;
};

struct zis_ast_node_Cond_data {
    struct zis_array_obj *args;
};

struct zis_ast_node_While_data {
    struct zis_ast_node_obj *cond;
    struct zis_array_obj *body;
};

struct zis_ast_node_Func_data {
    struct zis_symbol_obj *name;
    struct zis_array_obj *args;
    struct zis_array_obj *body;
};

struct zis_ast_node_Module_data {
    struct zis_object *file;
    struct zis_array_obj *body;
};

#define ZIS_AST_NODE_LIST \
    E(Nil            , "Object\0_\0") \
    E(Bool           , "Bool\0value\0") \
    E(Constant       , "Object\0value\0") \
    E(Name           , "Symbol\0value\0") \
    E(Pos            , "Node\0value\0") \
    E(Neg            , "Node\0value\0") \
    E(BitNot         , "Node\0value\0") \
    E(Not            , "Node\0value\0") \
    E(Add            , "Node\0lhs\0Node\0rhs\0") \
    E(Sub            , "Node\0lhs\0Node\0rhs\0") \
    E(Mul            , "Node\0lhs\0Node\0rhs\0") \
    E(Div            , "Node\0lhs\0Node\0rhs\0") \
    E(Rem            , "Node\0lhs\0Node\0rhs\0") \
    E(Shl            , "Node\0lhs\0Node\0rhs\0") \
    E(Shr            , "Node\0lhs\0Node\0rhs\0") \
    E(BitAnd         , "Node\0lhs\0Node\0rhs\0") \
    E(BitOr          , "Node\0lhs\0Node\0rhs\0") \
    E(BitXor         , "Node\0lhs\0Node\0rhs\0") \
    E(Assign         , "Node\0lhs\0Node\0rhs\0") \
    E(Eq             , "Node\0lhs\0Node\0rhs\0") \
    E(Ne             , "Node\0lhs\0Node\0rhs\0") \
    E(Lt             , "Node\0lhs\0Node\0rhs\0") \
    E(Le             , "Node\0lhs\0Node\0rhs\0") \
    E(Gt             , "Node\0lhs\0Node\0rhs\0") \
    E(Ge             , "Node\0lhs\0Node\0rhs\0") \
    E(And            , "Node\0lhs\0Node\0rhs\0") \
    E(Or             , "Node\0lhs\0Node\0rhs\0") \
    E(Subscript      , "Node\0value\0Node\0key\0") \
    E(Field          , "Node\0value\0Symbol\0name\0") \
    E(Call           , "Node\0value\0Array\0args\0") \
    E(Send           , "Symbol\0method\0Array\0args\0") \
    E(Tuple          , "Array\0args\0") \
    E(Array          , "Array\0args\0") \
    E(Map            , "Array\0args\0") \
    E(Import         , "Node\0value\0") \
    E(Return         , "Object\0value\0") \
    E(Throw          , "Object\0value\0") \
    E(Break          , "Object\0_\0") \
    E(Continue       , "Object\0_\0") \
    E(Cond           , "Array\0args\0") \
    E(While          , "Node\0cond\0Array\0body\0") \
    E(Func           , "Symbol\0name\0Array\0args\0Array\0body\0") \
    E(Module         , "Object\0file\0Array\0body\0") \
// ^^^ ZIS_AST_NODE_LIST ^^^
