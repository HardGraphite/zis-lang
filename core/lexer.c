#include "lexer.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "locals.h"
#include "memory.h"
#include "objmem.h"
#include "strutil.h"
#include "token.h"

#include "floatobj.h"
#include "intobj.h"
#include "mapobj.h"
#include "streamobj.h"
#include "stringobj.h"
#include "symbolobj.h"

#if ZIS_FEATURE_SRC

/* ----- error handling ----------------------------------------------------- */

/// Format the error message and call the handler. No-return.
zis_printf_fn_attrs(2, 3) zis_noreturn zis_noinline zis_cold_fn
static void error(
    struct zis_lexer *restrict l,
    zis_printf_fn_arg_fmtstr const char *restrict fmt, ...
) {
    char msg_buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, sizeof msg_buf, fmt, ap);
    va_end(ap);
    zis_debug_log(WARN, "Lexer", "error@(%u,%u): %s", l->line, l->column, msg_buf);
    if (l->error_handler)
        l->error_handler(l, msg_buf);
    zis_context_panic(l->z, ZIS_CONTEXT_PANIC_ABORT);
}

zis_noreturn zis_noinline zis_cold_fn
static void error_unexpected_char(struct zis_lexer *restrict l, int32_t c) {
    char buffer[8];
    if (c >= 0x80) {
        const size_t n = zis_u8char_from_code((zis_wchar_t)c, (zis_char8_t *)buffer + 1);
        assert(n && n < sizeof buffer - 2);
        buffer[0] = '"', buffer[n + 1] = '"', buffer[n + 2] = 0;
    } else if (!isprint(c)) {
        snprintf(buffer, sizeof buffer, "U+%04X", (uint32_t)c);
    } else {
        const char q = c == '"' ? '\'' : '"';
        buffer[0] = q, buffer[1] = (char)c, buffer[2] = q, buffer[3] = 0;
    }
    error(l, "unexpected character: %s", buffer);
}

zis_noreturn zis_noinline zis_cold_fn
    static void error_unexpected_end_of(struct zis_lexer *restrict l, const char *what) {
    error(l, "unexpected end of %s", what);
}

/* ----- input stream operations -------------------------------------------- */

/// Peek next character.
zis_static_force_inline int32_t stream_peek(struct zis_stream_obj *restrict stream) {
    return zis_stream_obj_peek_char(stream);
}

/// Ignore next 1-byte character.
zis_static_force_inline void stream_ignore_1(struct zis_stream_obj *restrict stream) {
    // See `zis_stream_obj_peek_char()`.
    assert(stream->_c_cur < stream->_c_end);
    assert(!(*stream->_c_cur & 0x80));
    stream->_c_cur++;
}

/// Ignore until the given character (included).
zis_static_force_inline void stream_ignore_until(
    struct zis_stream_obj *restrict stream, int32_t until_c
) {
    // TODO: needs a faster method.
    while (1) {
        const int c = zis_stream_obj_read_char(stream);
        if (c == until_c || c == -1)
            return;
    }
}

/// Get the buffer.
zis_static_force_inline const char *stream_buffer(
    struct zis_stream_obj *restrict stream, size_t *restrict size
) {
    return zis_stream_obj_char_buf_ptr(stream, 0, size);
}

/// Move the buffer pointer.
zis_static_force_inline void stream_buffer_ignore(
    struct zis_stream_obj *restrict stream, const size_t size
) {
    zis_stream_obj_char_buf_ptr(stream, size, NULL);
}

/* ----- token operations --------------------------------------------------- */

zis_static_force_inline void token_set_loc0(
    struct zis_token *restrict tok, struct zis_lexer *restrict l
) {
    tok->line0 = l->line;
    tok->column0 = l->column;
}

zis_static_force_inline void token_set_loc1(
    struct zis_token *restrict tok, struct zis_lexer *restrict l
) {
    tok->line1 = l->line;
    tok->column1 = l->column;
}

zis_static_force_inline void token_set_type(
    struct zis_token *restrict tok, enum zis_token_type tt
) {
    tok->type = tt;
}

/* ----- keyword table ------------------------------------------------------ */

/// Create the keyword table.
static struct zis_map_obj *keyword_table_new(struct zis_context *z) {
    const unsigned int first_kw_tok_id = (unsigned int)ZIS_TOK_KW_NIL;
    const unsigned int kw_count =
#define E(NAME, TEXT) + 1
    ZIS_KEYWORD_LIST
#undef E
    ;

    zis_locals_decl_1(z, var, struct zis_map_obj *kwt);
    zis_locals_zero(var);
    var.kwt = zis_map_obj_new(z, 1.0f, kw_count);

    for (unsigned int tt = first_kw_tok_id, tt_end = tt + kw_count; tt < tt_end; tt++) {
        const char *name_str = zis_token_keyword_text(tt);
        struct zis_symbol_obj *name_sym = zis_symbol_registry_get(z, name_str, (size_t)-1);
        struct zis_object *tt_smi = zis_smallint_to_ptr((zis_smallint_t)tt);
        zis_map_obj_sym_set(z, var.kwt, name_sym, tt_smi);
    }
    assert(zis_map_obj_length(var.kwt) == (size_t)kw_count);

    zis_locals_drop(z, var);
    return var.kwt;
}

/// Check whether a symbol is a keyword.
/// If it is, returns a `ZIS_TOK_KW_*` value; otherwise, returns 0.
static int keyword_table_lookup(struct zis_map_obj *kwt, struct zis_symbol_obj *sym) {
    struct zis_object *res = zis_map_obj_sym_get(kwt, sym);
    if (!res)
        return 0;
    assert(zis_object_is_smallint(res));
    zis_smallint_t x = zis_smallint_from_ptr(res);
    assert(x > 0 && x < 256);
    int tt = (int)x;
    assert(zis_token_type_is_keyword(tt));
    return tt;
}

/* ----- scanning  ---------------------------------------------------------- */

zis_static_force_inline void loc_next_char(struct zis_lexer *restrict l) {
    l->column++;
}

zis_static_force_inline void loc_next_char_n(struct zis_lexer *restrict l, unsigned int n) {
    l->column += n;
}

zis_static_force_inline void loc_next_line(struct zis_lexer *restrict l) {
    l->column = 1;
    l->line++;
}

zis_static_force_inline void clear_temp_var(struct zis_lexer *restrict l) {
    l->temp_var = zis_smallint_to_ptr(0);
}

static void scan_number(
    struct zis_lexer *l, struct zis_token *restrict tok
) {
    // token_set_loc0(tok, l);
    token_set_type(tok, ZIS_TOK_LIT_INT);

    struct zis_context *const z = l->z;
    struct zis_stream_obj *const input = l->input;
    unsigned int digit_base = 10;

    int32_t c = stream_peek(input);
    assert(isdigit(c));
    if (c == '0') {
        stream_ignore_1(input);
        c = stream_peek(input);
        switch (tolower(c)) {
        case 'b':
            digit_base = 2;
            break;
        case 'o':
            digit_base = 8;
            break;
        case 'x':
            digit_base = 16;
            break;
        case '.':
            tok->value = zis_smallint_to_ptr(0);
            goto scan_floating_point;
        default:
            if (!(isdigit(c) || c == '_')) {
                if (isalpha(c))
                    error_unexpected_char(l, c);
                token_set_loc1(tok, l);
                tok->value = zis_smallint_to_ptr(0);
                loc_next_char(l);
                zis_debug_log(TRACE, "Lexer", "int: base=10, val=0");
                return;
            }
            break;
        }
        if (isalpha(c)) {
            stream_ignore_1(input);
            loc_next_char(l);
        }
    }

    struct zis_object **temp_result_ref = &l->temp_var;
    bool has_temp_result = false;
    while (true) {
        size_t buf_sz;
        const char *buf = stream_buffer(input, &buf_sz);
        if (!buf) {
            if (!has_temp_result)
                error_unexpected_end_of(l, "number literal");
            break;
        }
        const char *buf_end = buf + buf_sz;
        struct zis_object *int_obj =
            zis_int_obj_or_smallint_s(z, buf, &buf_end, digit_base);
        const size_t consumed_size = (size_t)(buf_end - buf);
        assert(consumed_size <= buf_sz);
        loc_next_char_n(l, (unsigned int)consumed_size);
        stream_buffer_ignore(input, consumed_size);
        if (zis_unlikely(!int_obj)) {
            assert(consumed_size == 0); // Otherwise it would be a "too large" error, which is impossible for a short integer literal.
            error_unexpected_end_of(l, "number literal");
        }
        if (has_temp_result) {
            struct zis_object *prev_result = *temp_result_ref;
            *temp_result_ref = int_obj;
            assert(consumed_size <= ZIS_SMALLINT_MAX);
            struct zis_object *const weight = zis_int_obj_or_smallint_pow(
                z,
                zis_smallint_to_ptr(digit_base),
                zis_smallint_to_ptr((zis_smallint_t)consumed_size)
            );
            prev_result = zis_int_obj_or_smallint_mul(z, prev_result, weight);
            if (zis_unlikely(!prev_result))
                error(l, "the integer constant is too large");
            int_obj = zis_int_obj_or_smallint_add(z, *temp_result_ref /* int_obj */, prev_result);
            if (zis_unlikely(!int_obj))
                error(l, "the integer constant is too large");
        } else {
            has_temp_result = true;
        }
        *temp_result_ref = int_obj;
        if (consumed_size < buf_sz || zis_char_digit((zis_wchar_t)stream_peek(input)) >= digit_base)
            break;
    }
    tok->value = *temp_result_ref;
    clear_temp_var(l);

    if (stream_peek(input) != '.') {
        token_set_loc1(tok, l), tok->column1--;
        zis_debug_log(
            TRACE, "Lexer", "int: base=%u, val=%ji(0 if too long)",
            digit_base,
            zis_object_is_smallint(tok->value) ? (intmax_t)zis_smallint_from_ptr(tok->value) : INTMAX_C(0)
        );
        return;
    }

scan_floating_point:
    token_set_type(tok, ZIS_TOK_LIT_FLOAT);

    double float_value;
    if (zis_object_is_smallint(tok->value))
        float_value = (double)zis_smallint_from_ptr(tok->value);
    else
        float_value = zis_int_obj_value_f(tok->value_int);
    if (float_value == HUGE_VAL)
        error(l, "too large");
    assert(float_value >= 0.0);

    stream_ignore_1(input); // "."
    loc_next_char(l);
    uint64_t fractional_part = 0; // as integer
    unsigned int fractional_part_len = 0; // number of digits added to `fractional_part`
    unsigned int fractional_char_count = 0; // total number of characters
    for (; ; fractional_char_count++) {
        c = stream_peek(input);
        const unsigned int x = zis_char_digit((zis_wchar_t)c);
        if (x >= digit_base) {
            if (c == '_')
                continue;
            if (!fractional_char_count)
                error_unexpected_end_of(l, "number literal");
            break;
        }
        stream_ignore_1(input);
        if (fractional_part < UINT64_MAX / digit_base) {
            fractional_part = fractional_part * digit_base + x;
            fractional_part_len++;
        }
    }
    loc_next_char_n(l, fractional_char_count);
    float_value += (double)fractional_part / pow(digit_base, fractional_part_len);

    token_set_loc1(tok, l), tok->column1--;
    tok->value_float = zis_float_obj_new(z, float_value);
    zis_debug_log(
        TRACE, "Lexer", "float: base=%u, val=%f",
        digit_base, float_value
    );
}

static zis_wchar_t _lit_str_esc_trans(const char *restrict s, const char **restrict s_end_p) {
    const char *s_end = *s_end_p;
    const ptrdiff_t s_n = s_end - s;
    if (!s_n)
        return (zis_wchar_t)-1;

    zis_wchar_t result;
    switch (s[0]) {
    case '\'':
        result = '\'';
        break;
    case '"':
        result = '"';
        break;
    case '\\':
        result = '\\';
        break;
    case 'a':
        result = '\a';
        break;
    case 'b':
        result = '\b';
        break;
    case 'f':
        result = '\f';
        break;
    case 'n':
        result = '\n';
        break;
    case 'r':
        result = '\r';
        break;
    case 't':
        result = '\t';
        break;
    case 'v':
        result = '\v';
        break;
    case 'x':
        if (s_n >= 3 && isdigit(s[1]) && isxdigit(s[2])) {
            const int x = (s[1] - '0') * 16 + (isdigit(s[2]) ? s[2] - '0' : tolower(s[2]) - 'a' + 10);
            if (x < 0x80) {
                result = (zis_wchar_t)x;
                s += 2;
                break;
            }
        }
        return (zis_wchar_t)-1;
    case 'u':
        if (s_n < 4 || s[1] != '{')
            return (zis_wchar_t)-1;
        result = 0;
        for (ptrdiff_t i = 2; ; i++) {
            const char c = s[i];
            if (c == '}') {
                s += i;
                break;
            }
            if (i >= s_n || !isxdigit(c))
                return (zis_wchar_t)-1;
            result = result * 16 + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
            if (result > 0x10fff)
                return (zis_wchar_t)-1;
        }
        break;
    default:
        return (zis_wchar_t)-1;
    }

    *s_end_p = s + 1;
    return result;
}

static void scan_string(
    struct zis_lexer *l, struct zis_token *restrict tok,
    int32_t delimiter, bool allow_escape_sequences
) {
    // token_set_loc0(tok, l);
    token_set_type(tok, ZIS_TOK_LIT_STRING);

    struct zis_context *const z = l->z;
    struct zis_stream_obj *const input = l->input;
    assert(stream_peek(input) == delimiter);
    stream_ignore_1(input);
    loc_next_char(l);

    struct zis_string_obj **temp_result_ref = (struct zis_string_obj **)&l->temp_var;
    bool has_temp_result = false;
    for (bool end_reached = false; !end_reached; ) {
        size_t buf_sz;
        const char *buf = stream_buffer(input, &buf_sz);
        if (!buf)
            error_unexpected_end_of(l, "input stream before the string literal terminates");
        const char *end_p = buf;
        assert(buf_sz);
        for (const char *const buf_end = buf + buf_sz; end_p <= buf_end; ) {
            end_p = memchr(end_p, (int)delimiter, (size_t)(buf_end - end_p));
            if (!end_p) {
                end_p = (char *)zis_u8str_find_end((const zis_char8_t *)buf, buf_sz);
                if (!end_p)
                    error(l, "illegal string literal");
                break;
            }
            if (end_p[-1] != '\\' || !allow_escape_sequences) {
                end_reached = true;
                break;
            }
            for (const char *p = buf; ; ) {
                assert(p <= end_p);
                p = memchr(p, '\\', (size_t)(end_p - p));
                if (!p) {
                    end_reached = true;
                    goto end_p_ready;
                }
                if (p == end_p - 1) {
                    end_p++;
                    break;
                }
                p += 2;
            }
        }
    end_p_ready:;
        const size_t consumed_size = (size_t)(end_p - buf);
        assert(consumed_size <= buf_sz);
        struct zis_string_obj *str_obj =
            allow_escape_sequences ?
            zis_string_obj_new_esc(z, buf, consumed_size, _lit_str_esc_trans) :
            zis_string_obj_new(z, buf, consumed_size);
        stream_buffer_ignore(input, consumed_size);
        loc_next_char_n(l, (unsigned int)consumed_size);
        if (!str_obj)
            error(l, "illegal string literal");
        if (has_temp_result) {
            assert(zis_object_type(zis_object_from(*temp_result_ref)) == z->globals->type_String);
            str_obj = zis_string_obj_concat(z, *temp_result_ref, str_obj);
        } else {
            has_temp_result = true;
        }
        *temp_result_ref = str_obj;
    }
    assert(zis_object_type(zis_object_from(*temp_result_ref)) == z->globals->type_String);
    tok->value_string = *temp_result_ref;
    clear_temp_var(l);

    assert(stream_peek(input) == delimiter);
    token_set_loc1(tok, l);
    stream_ignore_1(input);
    loc_next_char(l);

    zis_debug_log(
        TRACE, "Lexer", "string: ``%s''",
        zis_string_obj_data_utf8(tok->value_string) ? zis_string_obj_data_utf8(tok->value_string) : "..."
    );
}

static void scan_identifier_or_keyword(struct zis_lexer *restrict l, struct zis_token *restrict tok) {
    // token_set_loc0(tok, l);
    token_set_type(tok, ZIS_TOK_IDENTIFIER);

    struct zis_context *const z = l->z;
    struct zis_stream_obj *const input = l->input;

    struct zis_symbol_obj **temp_result_ref = (struct zis_symbol_obj **)&l->temp_var;
    bool has_temp_result = false;
    for (bool end_reached = false; !end_reached; ) {
        size_t buf_sz;
        const char *buf = stream_buffer(input, &buf_sz);
        if (!buf) {
            assert(has_temp_result);
            break;
        }
        const char *end_p = buf;
        assert(buf_sz);
        for (const char *const buf_end = buf + buf_sz; ; ) {
            const char c = *end_p;
            const size_t n = zis_u8char_len_1((zis_char8_t)c);
            if (!n) {
                loc_next_char_n(l, (unsigned int)(end_p - buf));
                error_unexpected_end_of(l, "identifier");
            }
            if (n == 1 && !(isalnum(c) || c == '_')) {
                end_reached = true;
                break;
            }
            const char *new_end_p = end_p + n;
            if (new_end_p >= buf_end) {
                if (new_end_p == buf_end)
                    end_p = new_end_p;
                break;
            }
            end_p = new_end_p;
        }
        const size_t consumed_size = (size_t)(end_p - buf);
        assert(consumed_size <= buf_sz);
        loc_next_char_n(l, (unsigned int)consumed_size);
        struct zis_symbol_obj *sym_obj;
        if (has_temp_result) {
            assert(zis_object_type(zis_object_from(*temp_result_ref)) == z->globals->type_Symbol);
            sym_obj = zis_symbol_registry_get2(
                z,
                zis_symbol_obj_data(*temp_result_ref), zis_symbol_obj_data_size(*temp_result_ref),
                buf, consumed_size
            );
        } else {
            has_temp_result = true;
            sym_obj = zis_symbol_registry_get(z, buf, consumed_size);
        }
        *temp_result_ref = sym_obj;
        stream_buffer_ignore(input, consumed_size);
    }
    assert(zis_object_type(zis_object_from(*temp_result_ref)) == z->globals->type_Symbol);
    tok->value_identifier = *temp_result_ref;
    const int kw_id = keyword_table_lookup(l->keywords, tok->value_identifier);
    if (kw_id)
        token_set_type(tok, (enum zis_token_type)kw_id);
    clear_temp_var(l);

    token_set_loc1(tok, l), tok->column1--;

    zis_debug_log(
        TRACE, "Lexer", "identifier%s: %.*s",
        tok->type == ZIS_TOK_IDENTIFIER ? "" : " (keyword)",
        (int)zis_symbol_obj_data_size(tok->value_identifier),
        zis_symbol_obj_data(tok->value_identifier)
    );
}

/// Scan for next token.
static void scan_next(struct zis_lexer *restrict l, struct zis_token *restrict tok) {
    struct zis_stream_obj *const input = l->input;
    int32_t first_char;

scan_next_char:
    token_set_loc0(tok, l);
    switch ((first_char = stream_peek(input))) {

// For 'C': C.
#define CASE_OPERATOR_1(C, TOK_TYPE_C) \
    do {                          \
        token_set_type(tok, TOK_TYPE_C); \
        goto input_ignore1__token_set_loc1__loc_next__return; \
    } while(0)

// For 'C': C, C=.
#define CASE_OPERATOR_2(C, TOK_TYPE_C, TOK_TYPE_C_EQL) \
    do {                                               \
        stream_ignore_1(input);                        \
        if (stream_peek(input) == '=') {               \
            token_set_type(tok, TOK_TYPE_C_EQL);       \
            goto loc_next__input_ignore1__token_set_loc1__loc_next__return; \
        } else {                                       \
            token_set_type(tok, TOK_TYPE_C);           \
            goto token_set_loc1__loc_next__return;     \
        }                                              \
    } while(0)

// For 'C': C, C=, CC.
#define CASE_OPERATOR_3(C, TOK_TYPE_C, TOK_TYPE_C_EQL, TOK_TYPE_CC) \
    CASE_OPERATOR_3X(C, TOK_TYPE_C, TOK_TYPE_C_EQL, C, TOK_TYPE_CC)

// For 'C': C, C=, CX.
#define CASE_OPERATOR_3X(C, TOK_TYPE_C, TOK_TYPE_C_EQL, X, TOK_TYPE_CX) \
    do {                                                                \
        stream_ignore_1(input);                                         \
        const int32_t second_char = stream_peek(input);                 \
        if (second_char == X) {                                         \
            token_set_type(tok, TOK_TYPE_CX);                           \
            goto loc_next__input_ignore1__token_set_loc1__loc_next__return; \
        } else if (second_char == '=') {                                \
            token_set_type(tok, TOK_TYPE_C_EQL);                        \
            goto loc_next__input_ignore1__token_set_loc1__loc_next__return; \
        } else {                                                        \
            token_set_type(tok, TOK_TYPE_C);                            \
            goto token_set_loc1__loc_next__return;                      \
        }                                                               \
    } while(0)

    case '\t':
    case '\v':
    case '\f':
    case ' ':
        stream_ignore_1(input);
        loc_next_char(l);
        goto scan_next_char;

    case '\r':
        stream_ignore_1(input);
        loc_next_char(l);
        if (zis_unlikely(stream_peek(input) != '\n'))
            error_unexpected_char(l, '\r');
        zis_fallthrough;

    case '\n':
        if (zis_unlikely(l->ignore_eol)) {
            stream_ignore_1(input);
            loc_next_line(l);
            goto scan_next_char;
        }
        token_set_type(tok, ZIS_TOK_EOS);
        loc_next_line(l), l->column--;
        goto input_ignore1__token_set_loc1__loc_next__return;

    case ';':
        if (zis_unlikely(l->ignore_eol))
            error_unexpected_char(l, ';');
        token_set_type(tok, ZIS_TOK_EOS);
        goto input_ignore1__token_set_loc1__loc_next__return;

    case '#':
        stream_ignore_until(input, '\n');
        loc_next_line(l);
        goto scan_next_char;

    case '\\':
        stream_ignore_1(input);
        loc_next_char(l);
        first_char = stream_peek(input);
        if (first_char == '\n') {
            stream_ignore_1(input);
            loc_next_line(l);
            goto scan_next_char;
        } else if (first_char == '"' || first_char == '\'') {
            scan_string(l, tok, first_char, true);
            assert(tok->type == ZIS_TOK_LIT_STRING);
            tok->value_identifier = zis_symbol_registry_gets(l->z, tok->value_string);
            tok->type = ZIS_TOK_IDENTIFIER;
            break;
        } else {
            if (first_char == -1)
                error_unexpected_end_of(l, "input stream");
            else
                error_unexpected_char(l, first_char);
        }

    case '!': // "!", "!="
        CASE_OPERATOR_2('!', ZIS_TOK_OP_NOT, ZIS_TOK_OP_NE);

    case '$': // "$"
        CASE_OPERATOR_1('$', ZIS_TOK_DOLLAR);

    case '%': // "%", "%="
        CASE_OPERATOR_2('%', ZIS_TOK_OP_REM, ZIS_TOK_OP_REM_EQL);

    case '&': // "&", "&=", "&&"
        CASE_OPERATOR_3('&', ZIS_TOK_OP_BIT_AND, ZIS_TOK_OP_BIT_AND_EQL, ZIS_TOK_OP_AND);

    case '*': // "*", "*=", "**"
        CASE_OPERATOR_3('*', ZIS_TOK_OP_MUL, ZIS_TOK_OP_MUL_EQL, ZIS_TOK_OP_POW);

    case '+': // "+", "+="
        CASE_OPERATOR_2('+', ZIS_TOK_OP_ADD, ZIS_TOK_OP_ADD_EQL);

    case ',': // ","
        CASE_OPERATOR_1(',', ZIS_TOK_COMMA);

    case '-': // "-", "-=", "->"
        CASE_OPERATOR_3X('-', ZIS_TOK_OP_SUB, ZIS_TOK_OP_SUB_EQL, '>', ZIS_TOK_R_ARROW);

    case '.': {
        stream_ignore_1(input);
        if (stream_peek(input) == '.') {
            stream_ignore_1(input);
            if (stream_peek(input) == '.') {
                loc_next_char(l);
                token_set_type(tok, ZIS_TOK_ELLIPSIS);
                goto loc_next__input_ignore1__token_set_loc1__loc_next__return;
            }
            token_set_type(tok, ZIS_TOK_DOTDOT);
            goto loc_next__input_ignore1__token_set_loc1__loc_next__return;
        }
        token_set_type(tok, ZIS_TOK_OP_PERIOD);
        goto token_set_loc1__loc_next__return;
    }

    case '/': // "/", "/="
        CASE_OPERATOR_2('/', ZIS_TOK_OP_DIV, ZIS_TOK_OP_DIV_EQL);

    case ':': // ":"
        CASE_OPERATOR_1(':', ZIS_TOK_OP_COLON);

    case '<': // "<", "<=", "<<", "<-", "<=>"
        stream_ignore_1(input);
        {
            const int32_t second_char = stream_peek(input);
            if (second_char == '=') {
                stream_ignore_1(input);
                loc_next_char(l);
                if (stream_peek(input) == '>') {
                    token_set_type(tok, ZIS_TOK_OP_CMP);
                    goto loc_next__input_ignore1__token_set_loc1__loc_next__return;
                }
                token_set_type(tok, ZIS_TOK_OP_LE);
                goto token_set_loc1__loc_next__return;
            } else if (second_char == '<') {
                token_set_type(tok, ZIS_TOK_OP_SHL);
                goto loc_next__input_ignore1__token_set_loc1__loc_next__return;
            } else if (second_char == '-') {
                token_set_type(tok, ZIS_TOK_L_ARROW);
                goto loc_next__input_ignore1__token_set_loc1__loc_next__return;
            } else {
                token_set_type(tok, ZIS_TOK_OP_LT);
                goto token_set_loc1__loc_next__return;
            }
        }

    case '=': // "=", "=="
        CASE_OPERATOR_2('=', ZIS_TOK_OP_EQL, ZIS_TOK_OP_EQ);

    case '>': // ">", ">=", ">>"
        CASE_OPERATOR_3('>', ZIS_TOK_OP_GT, ZIS_TOK_OP_GE, ZIS_TOK_OP_SHR);

    case '?': // "?"
        CASE_OPERATOR_1('?', ZIS_TOK_QUESTION);

    case '@': // "@", `@"..."'
        stream_ignore_1(input);
        {
            const int32_t second_char = stream_peek(input);
            if (second_char == '"' || second_char == '\'') {
                scan_string(l, tok, second_char, false);
                break;
            }
        }
        token_set_type(tok, ZIS_TOK_AT);
        goto token_set_loc1__loc_next__return;

    case '^': // "^", "^="
        CASE_OPERATOR_2('^', ZIS_TOK_OP_BIT_XOR, ZIS_TOK_OP_BIT_XOR_EQL);

    case '|': // "|", "|="
        CASE_OPERATOR_2('|', ZIS_TOK_OP_BIT_OR, ZIS_TOK_OP_BIT_OR_EQL);

    case '~':
        CASE_OPERATOR_1('~', ZIS_TOK_OP_BIT_NOT);

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        scan_number(l, tok);
        break;

    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case '_':
    case_identifier:
        scan_identifier_or_keyword(l, tok);
        break;

    case '"':
    case '\'':
        scan_string(l, tok, first_char, true);
        break;

    case '(':
        CASE_OPERATOR_1('(', ZIS_TOK_L_PAREN);

    case ')':
        CASE_OPERATOR_1(')', ZIS_TOK_R_PAREN);

    case '[':
        CASE_OPERATOR_1('[', ZIS_TOK_L_BRACKET);

    case ']':
        CASE_OPERATOR_1(']', ZIS_TOK_R_BRACKET);

    case '{':
        CASE_OPERATOR_1('{', ZIS_TOK_L_BRACE);

    case '}':
        CASE_OPERATOR_1('}', ZIS_TOK_R_BRACE);

    default:
        if (zis_likely(first_char >= 0x80)) {
            goto case_identifier;
        }
        if (first_char == -1) {
            if (l->input_eof) {
                token_set_type(tok, ZIS_TOK_EOF);
                goto token_set_loc1__loc_next__return;
            } else {
                l->input_eof = true;
                token_set_type(tok, ZIS_TOK_EOS);
                token_set_loc1(tok, l);
                return;
            }
        }
        error_unexpected_char(l, first_char);

#undef CASE_OPERATOR_1
#undef CASE_OPERATOR_2
#undef CASE_OPERATOR_3
#undef CASE_OPERATOR_3X

    }
    return;

loc_next__input_ignore1__token_set_loc1__loc_next__return:
    loc_next_char(l);

input_ignore1__token_set_loc1__loc_next__return:
    stream_ignore_1(input);

token_set_loc1__loc_next__return:
    token_set_loc1(tok, l);
    loc_next_char(l);
    // return;
}

/* ----- public functions --------------------------------------------------- */

void zis_lexer_init(struct zis_lexer *restrict l, struct zis_context *z) {
    struct zis_map_obj *keyword_table = z->globals->val_lexer_keywords;
    if (zis_object_is_smallint(zis_object_from(keyword_table))) {
        // NOTE: see "globals.c".
        keyword_table = keyword_table_new(z);
        z->globals->val_lexer_keywords = keyword_table;
    }

    zis_lexer_finish(l);
    l->z = z;
    l->keywords = keyword_table;
}

void zis_lexer_start(
    struct zis_lexer *restrict l,
    struct zis_stream_obj *input_stream, zis_lexer_error_handler_t error_handler
) {
    l->line = 1, l->column = 1;
    l->ignore_eol = 0;
    l->input_eof = false;
    l->input = input_stream;
    clear_temp_var(l);
    l->error_handler = error_handler;
    assert(l->keywords == l->z->globals->val_lexer_keywords);
}

void zis_lexer_finish(struct zis_lexer *restrict l) {
    l->input = zis_object_cast(zis_smallint_to_ptr(0), struct zis_stream_obj);
    clear_temp_var(l);
    l->error_handler = NULL;
}

void zis_lexer_next(struct zis_lexer *restrict l, struct zis_token *restrict tok) {
    scan_next(l, tok);
    assert(zis_object_is_smallint(l->temp_var));
    zis_debug_log(
        TRACE, "Lexer", "token: loc=(%u,%u-%u,%u), type=%i, name=%s",
        tok->line0, tok->column0, tok->line1, tok->column1, tok->type,
        zis_token_type_represent(tok->type)
    );
}

void zis_lexer_ignore_eol_begin(struct zis_lexer *restrict l) {
    l->ignore_eol++;
    assert(l->ignore_eol != 0);
    zis_debug_log(TRACE, "Lexer", "ignore-eol: begin (#%u)", l->ignore_eol);
}

void zis_lexer_ignore_eol_end(struct zis_lexer *restrict l) {
    assert(l->ignore_eol != 0);
    zis_debug_log(TRACE, "Lexer", "ignore-eol: end (#%u)", l->ignore_eol);
    l->ignore_eol--;
}

void _zis_lexer_gc_visit(struct zis_lexer *restrict l, int op) {
    struct zis_object **begin = (struct zis_object **)&l->input;
    struct zis_object **end = begin + 3;
    assert((void *)begin[0] == (void *)l->input);
    assert((void *)begin[1] == (void *)l->keywords);
    assert((void *)begin[2] == (void *)l->temp_var);
    zis_objmem_visit_object_vec(begin, end, (enum zis_objmem_obj_visit_op)op);
}

#endif // ZIS_FEATURE_SRC
