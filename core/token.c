#include "token.h"

#include <string.h>

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

#pragma pack(push, 1)

static const char operators_text[] =
#define E(NAME, TEXT, PRECEDENCE) TEXT "\0"
    ZIS_UNARY_OPERATOR_LIST
#undef E

#define E(NAME, TEXT, PRECEDENCE) TEXT "\0"
    ZIS_BINARY_OPERATOR_LIST
#undef E

#define E(NAME, TEXT) TEXT "\0"
    ZIS_SPECIAL_OPERATOR_LIST
#undef E
;

static const char lit_types_text[] =
#define E(NAME, TEXT) TEXT "\0"
    ZIS_LITERAL_TYPE_LIST
#undef E
;

static const char rest_tokens_text[] =
    "identifier\0"
    "end-of-statement\0"
    "end-of-source\0"
;

#pragma pack(pop)

const char *zis_token_type_represent(enum zis_token_type tt) {
    if (zis_token_type_is_keyword(tt))
        return zis_token_keyword_text(tt);

    const char *text_table;
    unsigned int text_index;

    if (zis_uint_in_range(unsigned, tt, ZIS_TOK_OP_POS, ZIS_TOK_R_BRACE))
        text_table = operators_text, text_index = (unsigned int)tt;
    else if (zis_token_type_is_literal(tt))
        text_table = lit_types_text, text_index = (unsigned int)tt - (unsigned int)ZIS_TOK_LIT_INT;
    else
        text_table = rest_tokens_text, text_index = (unsigned int)tt - (unsigned int)ZIS_TOK_IDENTIFIER;

    for (; text_index; text_table += strlen(text_table) + 1, text_index--)
        assert(text_table[0]);

    return text_table;
}

#endif // ZIS_FEATURE_SRC
