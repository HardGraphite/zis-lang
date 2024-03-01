/// Lexical tokens.

#pragma once

#include <assert.h>

#include "algorithm.h"
#include "attributes.h"

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_object;
struct zis_float_obj;
struct zis_int_obj;
struct zis_string_obj;
struct zis_symbol_obj;

#define ZIS_UNARY_OPERATOR_LIST \
    E(POS          , "+"  , -3) \
    E(NEG          , "-"  , -3) \
    E(BIT_NOT      , "~"  , -3) \
    E(NOT          , "!"  , -3) \
// ^^^ ZIS_UNARY_OPERATOR_LIST ^^^

#define ZIS_BINARY_OPERATOR_LIST \
    E(ADD           , "+"  ,  5) \
    E(SUB           , "-"  ,  5) \
    E(MUL           , "*"  ,  4) \
    E(DIV           , "/"  ,  4) \
    E(REM           , "%"  ,  4) \
    E(SHL           , "<<" ,  6) \
    E(SHR           , ">>" ,  6) \
    E(BIT_AND       , "&"  , 10) \
    E(BIT_OR        , "|"  , 12) \
    E(BIT_XOR       , "^"  , 11) \
    E(EQL           , "="  ,-15) \
    E(EQ            , "==" ,  9) \
    E(NE            , "!=" ,  9) \
    E(LT            , "<"  ,  8) \
    E(LE            , "<=" ,  8) \
    E(GT            , ">"  ,  8) \
    E(GE            , ">=" ,  8) \
    E(AND           , "&&" , 13) \
    E(OR            , "||" , 14) \
    E(SUBSCRIPT     , "[...]",2) \
    E(PERIOD        , "."  ,  1) \
    E(ADD_EQL       , "+=" ,-15) \
    E(SUB_EQL       , "-=" ,-15) \
    E(MUL_EQL       , "*=" ,-15) \
    E(DIV_EQL       , "/=" ,-15) \
    E(REM_EQL       , "%=" ,-15) \
    E(SHL_EQL       , "<<=",-15) \
    E(SHR_EQL       , ">>=",-15) \
    E(BIT_AND_EQL   , "&=" ,-15) \
    E(BIT_OR_EQL    , "|=" ,-15) \
    E(BIT_XOR_EQL   , "^=" ,-15) \
    E(COLON         , ":"  ,  3) \
    E(CALL          , "(...)",2) \
// ^^^ ZIS_BINARY_OPERATOR_LIST ^^^

#define ZIS_SPECIAL_OPERATOR_LIST \
    E(AT            , "@"       ) \
    E(QUESTION      , "?"       ) \
    E(DOLLAR        , "$"       ) \
    E(DOTDOT        , ".."      ) \
    E(ELLIPSIS      , "..."     ) \
    E(L_ARROW       , "<-"      ) \
    E(R_ARROW       , "->"      ) \
    E(COMMA         , ","       ) \
    E(L_PAREN       , "("       ) \
    E(R_PAREN       , ")"       ) \
    E(L_BRACKET     , "["       ) \
    E(R_BRACKET     , "]"       ) \
    E(L_BRACE       , "{"       ) \
    E(R_BRACE       , "}"       ) \
// ^^^ ZIS_SPECIAL_OPERATOR_LIST ^^^

#define ZIS_KEYWORD_LIST    \
    E(NIL     , "nil"     ) \
    E(TRUE    , "true"    ) \
    E(FALSE   , "false"   ) \
    E(FUNC    , "func"    ) \
    E(STRUCT  , "struct"  ) \
    E(IF      , "if"      ) \
    E(ELIF    , "elif"    ) \
    E(ELSE    , "else"    ) \
    E(WHILE   , "while"   ) \
    E(FOR     , "for"     ) \
    E(BREAK   , "break"   ) \
    E(CONTINUE, "continue") \
    E(RETURN  , "return"  ) \
    E(THROW   , "throw"   ) \
    E(END     , "end"     ) \
// ^^^ ZIS_KEYWORD_LIST ^^^

#define ZIS_LITERAL_TYPE_LIST \
    E(INT   , "integer"     ) \
    E(FLOAT , "floating-point") \
    E(STRING, "string"      ) \
    E(SYMBOL, "symbol"      ) \
// ^^^ ZIS_LITERAL_TYPE_LIST ^^^

#if ZIS_FEATURE_SRC

/// Type of lexical tokens.
enum zis_token_type {

#define E(NAME, TEXT, PRECEDENCE) ZIS_TOK_OP_##NAME ,
    ZIS_UNARY_OPERATOR_LIST
#undef E

#define E(NAME, TEXT, PRECEDENCE) ZIS_TOK_OP_##NAME ,
    ZIS_BINARY_OPERATOR_LIST
#undef E

#define E(NAME, TEXT) ZIS_TOK_##NAME ,
    ZIS_SPECIAL_OPERATOR_LIST
#undef E

#define E(NAME, TEXT) ZIS_TOK_KW_##NAME ,
    ZIS_KEYWORD_LIST
#undef E

#define E(NAME, TEXT) ZIS_TOK_LIT_##NAME ,
    ZIS_LITERAL_TYPE_LIST
#undef E

    ZIS_TOK_IDENTIFIER,

    ZIS_TOK_EOS, ///< End of statement.
    ZIS_TOK_EOF, ///< End of file.

    _ZIS_TOK_COUNT
};

/// Check whether the token is a unary operator.
#define zis_token_type_is_un_op(tok_type) \
    zis_uint_in_range(unsigned, tok_type, ZIS_TOK_OP_POS, ZIS_TOK_OP_NOT)

/// Check whether the token is a binary operator.
#define zis_token_type_is_bin_op(tok_type) \
    zis_uint_in_range(unsigned, tok_type, ZIS_TOK_OP_ADD, ZIS_TOK_OP_CALL)

/// Check whether the token is an unary/binary operator.
#define zis_token_type_is_operator(tok_type) \
    zis_uint_in_range(unsigned, tok_type, ZIS_TOK_OP_POS, ZIS_TOK_OP_CALL)

/// Check whether the token is a keyword.
#define zis_token_type_is_keyword(tok_type) \
    zis_uint_in_range(unsigned, tok_type, ZIS_TOK_KW_NIL, ZIS_TOK_KW_END)

/// Check whether the token is a literal.
#define zis_token_type_is_literal(tok_type) \
    zis_uint_in_range(unsigned, tok_type, ZIS_TOK_LIT_INT, ZIS_TOK_LIT_SYMBOL)

extern const signed char _zis_token_operator_precedences[(unsigned)ZIS_TOK_OP_CALL + 1U];

/// Get the precedence of an operator. Negative value means a right-to-left associativity.
zis_static_force_inline signed char zis_token_operator_precedence(enum zis_token_type tt) {
    assert(zis_token_type_is_operator(tt));
    return _zis_token_operator_precedences[(unsigned)tt];
}

extern const char *const _zis_token_keyword_texts[(unsigned)ZIS_TOK_KW_END - (unsigned)ZIS_TOK_KW_NIL + 1U];

/// Get the text representation of a keyword.
zis_static_force_inline const char *zis_token_keyword_text(enum zis_token_type tt) {
    assert(zis_token_type_is_keyword(tt));
    return _zis_token_keyword_texts[(unsigned)tt - (unsigned)ZIS_TOK_KW_NIL];
}

/// Represent the token type as a string. This can be very slow.
const char *zis_token_type_represent(enum zis_token_type tt);

/// Lexical tokens.
struct zis_token {
    unsigned int line0, column0, line1, column1;
    enum zis_token_type type;
    union {
        struct zis_object     *value;
        struct zis_int_obj    *value_int;
        struct zis_float_obj  *value_float;
        struct zis_string_obj *value_string;
        struct zis_symbol_obj *value_identifier;
    };
};

#endif // ZIS_FEATURE_SRC
