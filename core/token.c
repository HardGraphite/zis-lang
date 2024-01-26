#include "token.h"

#if ZIS_FEATURE_SRC

static_assert((unsigned)ZIS_TOK_OP_POS == 0, "");
static_assert((unsigned)ZIS_TOK_OP_NOT + 1 == (unsigned)ZIS_TOK_OP_ADD, "");

const signed char
_zis_token_operator_precedences[(unsigned)ZIS_TOK_OP_SUBSCRIPT + 1U] = {

#define E(NAME, TEXT, PRECEDENCE) [(unsigned)ZIS_TOK_OP_##NAME] = PRECEDENCE ,
    ZIS_UNARY_OPERATOR_LIST
    ZIS_BINARY_OPERATOR_LIST
#undef E

};

#pragma pack(push, 1)

const char *const
_zis_token_keyword_texts[(unsigned)ZIS_TOK_KW_END - (unsigned)ZIS_TOK_KW_NIL + 1U] = {

#define E(NAME, TEXT) [(unsigned)ZIS_TOK_KW_##NAME - (unsigned)ZIS_TOK_KW_NIL] = TEXT ,
    ZIS_KEYWORD_LIST
#undef E

};

#pragma pack(pop)

#endif // ZIS_FEATURE_SRC
