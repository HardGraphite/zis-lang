#include "lexer.h"

#include <ctype.h>
#include <math.h> // HUGE_VAL
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "memory.h"
#include "objmem.h"
#include "strutil.h"
#include "token.h"

#include "floatobj.h"
#include "intobj.h"
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

/// Read next character.
zis_static_force_inline int32_t stream_read(struct zis_stream_obj *restrict stream) {
    return zis_stream_obj_read_char(stream);
}

/// Ignore next character.
zis_static_force_inline void stream_ignore(struct zis_stream_obj *restrict stream) {
    const int32_t c = zis_stream_obj_read_char(stream);
    assert(c != -1), zis_unused_var(c);
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

zis_static_force_inline void token_set_pos0(
    struct zis_token *restrict tok, struct zis_lexer *restrict l
) {
    tok->line0 = l->line;
    tok->column0 = l->column;
}

zis_static_force_inline void token_set_pos1(
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

/* ----- scanning  ---------------------------------------------------------- */

zis_static_force_inline void pos_next_char(struct zis_lexer *restrict l) {
    l->column++;
}

zis_static_force_inline void pos_next_char_n(struct zis_lexer *restrict l, unsigned int n) {
    l->column += n;
}

zis_static_force_inline void pos_next_line(struct zis_lexer *restrict l) {
    l->column = 1;
    l->line++;
}

zis_static_force_inline void clear_temp_var(struct zis_lexer *restrict l) {
    l->temp_var = zis_smallint_to_ptr(0);
}

static void scan_number(
    struct zis_lexer *l, struct zis_token *restrict tok
) {
    // token_set_pos0(tok, l);
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
            if (!isdigit(c)) {
                if (isalpha(c))
                    error_unexpected_char(l, c);
                token_set_pos1(tok, l);
                tok->value = zis_smallint_to_ptr(0);
                pos_next_char(l);
                zis_debug_log(TRACE, "Lexer", "int: base=10, val=0");
                return;
            }
            break;
        }
        if (isalpha(c)) {
            stream_ignore_1(input);
            pos_next_char(l);
        }
    }

    l->temp_var = NULL;
    while (true) {
        size_t buf_sz;
        const char *buf = stream_buffer(input, &buf_sz);
        if (!buf) {
            if (!l->temp_var)
                error_unexpected_end_of(l, "number literal");
            break;
        }
        const char *buf_end = buf + buf_sz;
        struct zis_object *int_obj =
            zis_int_obj_or_smallint_s(z, buf, &buf_end, digit_base);
        const size_t consumed_size = (size_t)(buf_end - buf);
        assert(consumed_size <= buf_sz);
        pos_next_char_n(l, (unsigned int)consumed_size);
        stream_buffer_ignore(input, consumed_size);
        if (!int_obj)
            error_unexpected_end_of(l, "number literal");
        if (l->temp_var) {
            const size_t k = consumed_size * digit_base;
            assert(k <= ZIS_SMALLINT_MAX);
            int_obj = zis_int_obj_add_x(
                z, l->temp_var,
                zis_int_obj_mul_x(
                    z, int_obj,
                    zis_smallint_to_ptr((zis_smallint_t)k)
                )
            );
        }
        l->temp_var = int_obj;
        if (consumed_size < buf_sz || zis_char_digit((zis_wchar_t)stream_peek(input)) < digit_base)
            break;
    }
    tok->value = l->temp_var;
    clear_temp_var(l);

    if (stream_peek(input) != '.') {
        token_set_pos1(tok, l), tok->column1--;
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
    pos_next_char(l);
    unsigned int fractional_count = 0;
    for (double weight = 1.0 / digit_base; ; weight /= digit_base, fractional_count++) {
        c = stream_peek(input);
        const unsigned int x = zis_char_digit((zis_wchar_t)c);
        if (x >= digit_base) {
            if (!fractional_count)
                error_unexpected_end_of(l, "number literal");
            break;
        }
        stream_ignore_1(input);
        float_value += (double)x * weight;
    }
    pos_next_char_n(l, fractional_count);

    token_set_pos1(tok, l), tok->column1--;
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
    // token_set_pos0(tok, l);
    token_set_type(tok, ZIS_TOK_LIT_STRING);

    struct zis_context *const z = l->z;
    struct zis_stream_obj *const input = l->input;
    assert(stream_peek(input) == delimiter);
    stream_ignore_1(input);
    pos_next_char(l);

    l->temp_var = NULL;
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
        pos_next_char_n(l, (unsigned int)consumed_size);
        if (!str_obj)
            error(l, "illegal string literal");
        if (l->temp_var) {
            assert(zis_object_type(l->temp_var) == z->globals->type_String);
            struct zis_string_obj *s = zis_object_cast(l->temp_var, struct zis_string_obj);
            str_obj = zis_string_obj_concat(z, s, str_obj);
        }
        l->temp_var = zis_object_from(str_obj);
    }
    assert(zis_object_type(l->temp_var) == z->globals->type_String);
    tok->value_string = zis_object_cast(l->temp_var, struct zis_string_obj);
    clear_temp_var(l);

    assert(stream_peek(input) == delimiter);
    token_set_pos1(tok, l);
    stream_ignore_1(input);
    pos_next_char(l);

    zis_debug_log(
        TRACE, "Lexer", "string: ``%s''",
        zis_string_obj_data_utf8(tok->value_string) ? zis_string_obj_data_utf8(tok->value_string) : "..."
    );
}

static void scan_identifier(struct zis_lexer *restrict l, struct zis_token *restrict tok) {
    // token_set_pos0(tok, l);
    token_set_type(tok, ZIS_TOK_IDENTIFIER);

    struct zis_context *const z = l->z;
    struct zis_stream_obj *const input = l->input;

    l->temp_var = NULL;
    for (bool end_reached = false; !end_reached; ) {
        size_t buf_sz;
        const char *buf = stream_buffer(input, &buf_sz);
        if (!buf) {
            assert(l->temp_var);
            break;
        }
        const char *end_p = buf;
        assert(buf_sz);
        for (const char *const buf_end = buf + buf_sz; ; ) {
            const char c = *end_p;
            const size_t n = zis_u8char_len_1((zis_char8_t)c);
            if (!n) {
                pos_next_char_n(l, (unsigned int)(end_p - buf));
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
        pos_next_char_n(l, (unsigned int)consumed_size);
        struct zis_symbol_obj *sym_obj;
        if (l->temp_var) {
            assert(zis_object_type(l->temp_var) == z->globals->type_Symbol);
            sym_obj = zis_object_cast(l->temp_var, struct zis_symbol_obj);
            sym_obj = zis_symbol_registry_get2(
                z,
                zis_symbol_obj_data(sym_obj), zis_symbol_obj_data_size(sym_obj),
                buf, consumed_size
            );
        } else {
            sym_obj = zis_symbol_registry_get(z, buf, consumed_size);
        }
        l->temp_var = zis_object_from(sym_obj);
        stream_buffer_ignore(input, consumed_size);
    }
    assert(zis_object_type(l->temp_var) == z->globals->type_Symbol);
    tok->value_identifier = zis_object_cast(l->temp_var, struct zis_symbol_obj);
    clear_temp_var(l);

    token_set_pos1(tok, l), tok->column1--;

    zis_debug_log(
        TRACE, "Lexer", "identifier: %.*s",
        (int)zis_symbol_obj_data_size(tok->value_identifier),
        zis_symbol_obj_data(tok->value_identifier)
    );
}

/// Scan for next token.
static void scan_next(struct zis_lexer *restrict l, struct zis_token *restrict tok) {
    struct zis_stream_obj *const input = l->input;
    int32_t first_char;

scan_next_char:
    token_set_pos0(tok, l);
    switch ((first_char = stream_peek(input))) {

// For 'C': C.
#define CASE_OPERATOR_1(C, TOK_TYPE_C) \
    do {                          \
        token_set_type(tok, TOK_TYPE_C); \
        goto input_ignore1__token_set_pos1__pos_next__return; \
    } while(0)

// For 'C': C, C=.
#define CASE_OPERATOR_2(C, TOK_TYPE_C, TOK_TYPE_C_EQL) \
    do {                                               \
        stream_ignore_1(input);                        \
        if (stream_peek(input) == '=') {               \
            token_set_type(tok, TOK_TYPE_C_EQL);       \
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return; \
        } else {                                       \
            token_set_type(tok, TOK_TYPE_C);           \
            goto token_set_pos1__pos_next__return;     \
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
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return; \
        } else if (second_char == '=') {                                \
            token_set_type(tok, TOK_TYPE_C_EQL);                        \
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return; \
        } else {                                                        \
            token_set_type(tok, TOK_TYPE_C);                            \
            goto token_set_pos1__pos_next__return;                      \
        }                                                               \
    } while(0)

// For 'C': C, C=, CC, CX.
#define CASE_OPERATOR_4X(C, TOK_TYPE_C, TOK_TYPE_C_EQL, TOK_TYPE_CC, X, TOK_TYPE_CX) \
    do {                                                                             \
        stream_ignore_1(input);                                                      \
        const int32_t second_char = stream_peek(input);                              \
        if (second_char == X) {                                                      \
            token_set_type(tok, TOK_TYPE_CX);                                        \
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return;          \
        } else if (second_char == C) {                                               \
            token_set_type(tok, TOK_TYPE_CC);                                        \
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return;          \
        } else if (second_char == '=') {                                             \
            token_set_type(tok, TOK_TYPE_C_EQL);                                     \
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return;          \
        } else {                                                                     \
            token_set_type(tok, TOK_TYPE_C);                                         \
            goto token_set_pos1__pos_next__return;                                   \
        }                                                                            \
    } while(0)

    case '\t':
    case ' ':
        stream_ignore_1(input);
        pos_next_char(l);
        goto scan_next_char;

    case '\n':
        token_set_type(tok, ZIS_TOK_EOS);
        pos_next_line(l), l->column--;
        goto input_ignore1__token_set_pos1__pos_next__return;

    case ';':
        token_set_type(tok, ZIS_TOK_EOS);
        goto input_ignore1__token_set_pos1__pos_next__return;

    case '#':
        stream_ignore_until(input, '\n');
        pos_next_line(l);
        goto scan_next_char;

    case '\\':
        stream_ignore_1(input);
        pos_next_char(l);
        first_char = stream_peek(input);
        if (first_char != '\n')
            error_unexpected_char(l, first_char);
        stream_ignore_1(input);
        pos_next_line(l);
        goto scan_next_char;

    case '!': // "!", "!="
        CASE_OPERATOR_2('!', ZIS_TOK_OP_NOT, ZIS_TOK_OP_NE);

    case '$': // "$"
        CASE_OPERATOR_1('$', ZIS_TOK_DOLLAR);

    case '%': // "%", "%="
        CASE_OPERATOR_2('%', ZIS_TOK_OP_REM, ZIS_TOK_OP_REM_EQL);

    case '&': // "&", "&=", "&&"
        CASE_OPERATOR_3('&', ZIS_TOK_OP_BIT_AND, ZIS_TOK_OP_BIT_AND_EQL, ZIS_TOK_OP_AND);

    case '*': // "*", "*="
        CASE_OPERATOR_2('*', ZIS_TOK_OP_MUL, ZIS_TOK_OP_MUL_EQL);

    case '+': // "+", "+="
        CASE_OPERATOR_2('*', ZIS_TOK_OP_ADD, ZIS_TOK_OP_ADD_EQL);

    case ',': // ","
        CASE_OPERATOR_1(',', ZIS_TOK_COMMA);

    case '-': // "-", "-=", "->"
        CASE_OPERATOR_3X('-', ZIS_TOK_OP_SUB, ZIS_TOK_OP_SUB_EQL, '>', ZIS_TOK_R_ARROW);

    case '.': {
        stream_ignore_1(input);
        if (stream_peek(input) == '.') {
            stream_ignore_1(input);
            if (stream_peek(input) == '.') {
                pos_next_char(l);
                token_set_type(tok, ZIS_TOK_ELLIPSIS);
                goto pos_next__input_ignore1__token_set_pos1__pos_next__return;
            }
            token_set_type(tok, ZIS_TOK_DOTDOT);
            goto pos_next__input_ignore1__token_set_pos1__pos_next__return;
        }
        token_set_type(tok, ZIS_TOK_OP_PERIOD);
        goto token_set_pos1__pos_next__return;
    }

    case '/': // "/", "/="
        CASE_OPERATOR_2('/', ZIS_TOK_OP_DIV, ZIS_TOK_OP_DIV_EQL);

    case ':': // ":"
        CASE_OPERATOR_1(':', ZIS_TOK_OP_COLON);

    case '<': // "<", "<=", "<<", "<-
        CASE_OPERATOR_4X('<', ZIS_TOK_OP_LT, ZIS_TOK_OP_LE, ZIS_TOK_OP_SHL, '-', ZIS_TOK_L_ARROW);

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
        goto token_set_pos1__pos_next__return;

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
        scan_identifier(l, tok);
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
            token_set_type(tok, ZIS_TOK_EOF);
            goto token_set_pos1__pos_next__return;
        }
        error_unexpected_char(l, first_char);

#undef CASE_OPERATOR_1
#undef CASE_OPERATOR_2
#undef CASE_OPERATOR_3
#undef CASE_OPERATOR_3X
#undef CASE_OPERATOR_4X

    }
    return;

pos_next__input_ignore1__token_set_pos1__pos_next__return:
    pos_next_char(l);

input_ignore1__token_set_pos1__pos_next__return:
    stream_ignore_1(input);

token_set_pos1__pos_next__return:
    token_set_pos1(tok, l);
    pos_next_char(l);
    // return;
}

/* ----- public functions --------------------------------------------------- */

void zis_lexer_init(struct zis_lexer *restrict l, struct zis_context *z) {
    zis_lexer_finish(l);
    l->z = z;
}

void zis_lexer_start(
    struct zis_lexer *restrict l,
    struct zis_stream_obj *input_stream, zis_lexer_error_handler_t error_handler
) {
    l->line = 1, l->column = 1;
    l->input = input_stream;
    l->temp_var = zis_smallint_to_ptr(0);
    l->error_handler = error_handler;
}

void zis_lexer_finish(struct zis_lexer *restrict l) {
    l->input = zis_object_cast(zis_smallint_to_ptr(0), struct zis_stream_obj);
    l->temp_var = zis_smallint_to_ptr(0);
    l->error_handler = NULL;
}

void zis_lexer_next(struct zis_lexer *restrict l, struct zis_token *restrict tok) {
    scan_next(l, tok);
    assert(zis_object_is_smallint(l->temp_var));
    zis_debug_log(
        TRACE, "Lexer", "token: pos=(%u,%u-%u,%u), type=%i",
        tok->line0, tok->column0, tok->line1, tok->column1, tok->type
    );
}

void _zis_lexer_gc_visit(struct zis_lexer *restrict l, int op) {
    struct zis_object **begin = (struct zis_object **)&l->input;
    struct zis_object **end = begin + 2;
    assert((void *)begin[0] == (void *)l->input);
    assert((void *)begin[1] == (void *)l->temp_var);
    zis_objmem_visit_object_vec(begin, end, (enum zis_objmem_obj_visit_op)op);
}

#endif // ZIS_FEATURE_SRC
