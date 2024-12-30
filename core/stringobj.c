#include "stringobj.h"

#include <assert.h>
#include <string.h>

#include "context.h"
#include "globals.h"
#include "locals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "strutil.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "rangeobj.h"
#include "streamobj.h"
#include "tupleobj.h"

/* ----- string object -------------------------------------------- */

struct zis_string_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size; // !!
    size_t _length_info; // [3:0] -> padding count, [N:4] -> length
    zis_char8_t _text_bytes[]; // UTF-8 bytes
};

#define STR_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_string_obj, _bytes_size)

#define STR_OBJ_LENGTH_MAX  (SIZE_MAX >> 4)

/// Number of bytes in the string.
zis_force_inline static size_t string_obj_size(const struct zis_string_obj *s) {
    return s->_bytes_size - (s->_length_info & 0xf) - STR_OBJ_BYTES_FIXED_SIZE;
}

/// Number of characters in the string.
zis_force_inline static size_t string_obj_length(const struct zis_string_obj *s) {
    return s->_length_info >> 4;
}

/// Get string data.
zis_force_inline static zis_char8_t *string_obj_data(struct zis_string_obj *s) {
    return s->_text_bytes;
}

/// Get string data.
zis_force_inline static const zis_char8_t *string_obj_as_u8str(const struct zis_string_obj *s) {
    return s->_text_bytes;
}

/// Get string data as ASCII string.
zis_force_inline static const char *string_obj_as_ascii(const struct zis_string_obj *s) {
    assert(string_obj_size(s) == string_obj_length(s));
    return (const char *)s->_text_bytes;
}

/// Allocate but do not initialize the text data.
static struct zis_string_obj *string_obj_alloc(
    struct zis_context *z,
    size_t size, size_t length
) {
    assert(length <= STR_OBJ_LENGTH_MAX);
    assert(size >= length);
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_String,
        0, STR_OBJ_BYTES_FIXED_SIZE + size
    );
    struct zis_string_obj *const str = zis_object_cast(obj, struct zis_string_obj);
    assert(!(length & ~(SIZE_MAX >> 4)));
    assert(str->_bytes_size - STR_OBJ_BYTES_FIXED_SIZE - size <= 0xf);
    str->_length_info = (length << 4) | (str->_bytes_size - STR_OBJ_BYTES_FIXED_SIZE - size);
    assert(string_obj_size(str) == size);
    assert(string_obj_length(str) == length);
    return str;
}

zis_cold_fn zis_noinline
static int string_obj_illegal_codepoint_error(struct zis_context *z, zis_wchar_t c) {
    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, NULL, NULL, "illegal code point %#04x", c
    )));
    return ZIS_THR;
}

zis_cold_fn zis_noinline
static int string_obj_invalid_bytes_error(struct zis_context *z) {
    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, NULL, NULL, "invalid byte sequence for a UTF-8 string"
    )));
    return ZIS_THR;
}

zis_cold_fn zis_noinline
static int string_obj_invalid_escape_sequence_error(struct zis_context *z, const char *seq) {
    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, NULL, NULL, "invalid string escape sequence: %s", seq
    )));
    return ZIS_THR;
}

zis_cold_fn zis_noinline
static int string_obj_too_long_error(struct zis_context *z) {
    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, "value", NULL, "the string is too long"
    )));
    return ZIS_THR;
}

struct zis_string_obj *zis_string_obj_new(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */
) {
    if (zis_unlikely(n == (size_t)-1))
        n = strlen(s);

    if (zis_unlikely(!n))
        return z->globals->val_empty_string;

    const size_t len = zis_u8str_len((const zis_char8_t *)s, n);
    if (zis_unlikely(len == (size_t)-1)) {
        string_obj_invalid_bytes_error(z);
        return NULL;
    }
    if (zis_unlikely(len > STR_OBJ_LENGTH_MAX)) {
        string_obj_too_long_error(z);
        return NULL;
    }

    struct zis_string_obj *str = string_obj_alloc(z, n, len);
    memcpy(string_obj_data(str), s, n);
    return str;
}

struct zis_string_obj *zis_string_obj_new_esc(
    struct zis_context *z,
    const char *string, size_t string_size /* = -1 */,
    char escape_beginning,
    zis_string_obj_wchar_t (*escape_translator)(const char *restrict s, const char **restrict s_end)
) {
    // Adapted from `zis_string_obj_new()`.

    if (zis_unlikely(string_size == (size_t)-1))
        string_size = strlen(string);

    if (zis_unlikely(!string_size))
        return z->globals->val_empty_string;

    size_t len = 0, size = 0;
    bool has_esc_seq = false;
    for (const char *p = string, *const p_end = p + string_size;;) {
        const char *p_esc = memchr(p, escape_beginning, (size_t)(p_end - p));
        if (!p_esc)
            p_esc = p_end;
        {
            const size_t slice_size = (size_t)(p_esc - p);
            const size_t slice_len = zis_u8str_len((const zis_char8_t *)p, slice_size);
            if (zis_unlikely(slice_len == (size_t)-1)) {
                string_obj_invalid_bytes_error(z);
                return NULL;
            }
            len += slice_len;
            size += slice_size;
        }
        if (p_esc == p_end)
            break;
        assert(p_esc < p_end);
        {
            has_esc_seq = true;
            const char *esc_end_p = p_end;
            zis_wchar_t translated_char = escape_translator(p_esc + 1, &esc_end_p);
            size_t translated_char_size = zis_u8char_len_from_code(translated_char);
            if (translated_char == (zis_wchar_t)-1 || translated_char_size == 0) {
                string_obj_invalid_escape_sequence_error(z, (char[]){ escape_beginning, p_esc[1], 0 });
                return NULL;
            }
            len++;
            size += translated_char_size;
            p = esc_end_p;
        }
        assert(p <= p_end);
    }

    if (zis_unlikely(len > STR_OBJ_LENGTH_MAX)) {
        string_obj_too_long_error(z);
        return NULL;
    }

    struct zis_string_obj *str = string_obj_alloc(z, size, len);

    if (!has_esc_seq) {
        assert(size == string_size);
        memcpy(string_obj_data(str), string, size);
        return str;
    }

    zis_char8_t *str_wr_p = string_obj_data(str);
    for (const char *p = string, *const p_end = p + string_size;;) {
        const char *p_esc = memchr(p, escape_beginning, (size_t)(p_end - p));
        if (!p_esc) {
            const size_t slice_size = (size_t)(p_end - p);
            memcpy(str_wr_p, p, slice_size);
            break;
        }
        const size_t slice_size = (size_t)(p_esc - p);
        memcpy(str_wr_p, p, slice_size);
        str_wr_p += slice_size;
        const char *esc_end_p = p_end;
        const zis_wchar_t translated_char = escape_translator(p_esc + 1, &esc_end_p);
        assert(translated_char != (zis_wchar_t)-1);
        const size_t translated_char_size = zis_u8char_from_code(translated_char, str_wr_p);
        assert(translated_char_size != 0);
        p = esc_end_p;
        assert(p <= p_end);
        str_wr_p += translated_char_size;
        assert(str_wr_p <= string_obj_data(str) + string_obj_size(str));
    }
    return str;
}

struct zis_string_obj *_zis_string_obj_new_empty(struct zis_context *z) {
    return string_obj_alloc(z, 0, 0);
}

struct zis_string_obj *zis_string_obj_from_char(
    struct zis_context *z, zis_string_obj_wchar_t ch
) {
    zis_char8_t buffer[8];
    const size_t n = zis_u8char_from_code(ch, buffer);
    assert(n <= sizeof buffer);
    if (zis_unlikely(n == 0)) {
        string_obj_illegal_codepoint_error(z, ch);
        return NULL;
    }
    return zis_string_obj_new(z, (const char *)buffer, n);
}

size_t zis_string_obj_length(const struct zis_string_obj *self) {
    return string_obj_length(self);
}

zis_wchar_t zis_string_obj_get(const struct zis_string_obj *str, size_t index) {
    const size_t str_len = string_obj_length(str);
    if (zis_unlikely(index >= str_len))
        return (zis_wchar_t)-1;
    const zis_char8_t *const str_data = string_obj_as_u8str(str);
    if (string_obj_size(str) == str_len)
        return str_data[index];
    const zis_char8_t *p = zis_u8str_find_pos(str_data, index);
    assert(p);
    zis_wchar_t c;
    zis_u8char_to_code(&c, p, p + 4);
    return c;
}

struct zis_string_obj *zis_string_obj_slice(
    struct zis_context *z,
    struct zis_string_obj *_str, size_t begin_index, size_t length
) {
    const size_t str_len = string_obj_length(_str);
    if (zis_unlikely(begin_index >= str_len || begin_index + length > str_len))
        return NULL;
    if (zis_unlikely(!length))
        return z->globals->val_empty_string;
    if (zis_unlikely(str_len == length)) {
        assert(begin_index == 0);
        return _str;
    }

    size_t size;
    if (str_len == string_obj_size(_str)) {
        size = length; // ASCII
    } else {
        const zis_char8_t *s = string_obj_as_u8str(_str);
        const zis_char8_t *p = zis_u8str_find_pos(s, length);
        assert(p);
        size = (size_t)(p - s);
    }

    zis_locals_decl_1(z, var, const struct zis_string_obj *str);
    var.str = _str;
    struct zis_string_obj *const res_str = string_obj_alloc(z, size, length);
    const zis_char8_t *const s = zis_u8str_find_pos(string_obj_as_u8str(var.str), begin_index);
    memcpy(string_obj_data(res_str), s, size);
    zis_locals_drop(z, var);
    return res_str;
}

size_t zis_string_obj_to_u8str(const struct zis_string_obj *self, char *buf, size_t buf_sz) {
    const size_t size = string_obj_size(self);
    if (!buf)
        return size;
    if (zis_unlikely(buf_sz < size))
        return (size_t)-1;
    memcpy(buf, string_obj_as_u8str(self), size);
    return size;
}

const char *zis_string_obj_as_ascii(const struct zis_string_obj *self, size_t *len_ret) {
    const size_t len = string_obj_length(self);
    if (len == string_obj_size(self)) {
        if (len_ret)
            *len_ret = len;
        return string_obj_as_ascii(self);
    }
    return NULL;
}

struct zis_string_obj *zis_string_obj_join(
    struct zis_context *z,
    struct zis_string_obj *separator /*=NULL*/, struct zis_object_vec_view items
) {
    if (zis_unlikely(zis_object_vec_view_length(items) == 0))
        return z->globals->val_empty_string;

    size_t res_size, res_len;
    if (separator) {
        const size_t n = zis_object_vec_view_length(items) - 1;
        res_size = string_obj_size(separator) * n;
        res_len  = string_obj_length(separator) * n;
    } else {
        res_size = 0;
        res_len  = 0;
    }

    {
        struct zis_type_obj *type_String = z->globals->type_String;
        zis_object_vec_view_foreach_unchanged(items, item, {
            const size_t old_res_size = res_size;
            if (zis_object_is_smallint(item)) {
                zis_smallint_t item_smi = zis_smallint_from_ptr(item);
                zis_wchar_t c = (zis_wchar_t)(zis_smallint_unsigned_t)item_smi;
                const size_t c_size = zis_u8char_len_from_code(c);
                if (zis_unlikely(item_smi < 0 || c_size == 0)) {
                    string_obj_illegal_codepoint_error(z, c);
                    return NULL;
                }
                res_size += c_size;
                res_len++;
            } else if (zis_likely(zis_object_type(item) == type_String)) {
                struct zis_string_obj *str = zis_object_cast(item, struct zis_string_obj);
                res_size += string_obj_size(str);
                res_len  += string_obj_length(str);
            } else {
                zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
                    z, "type", item, "item is neither a string nor a character"
                )));
                return NULL;
            }
            if (zis_unlikely(res_size < old_res_size)) {
                string_obj_too_long_error(z);
                return NULL;
            }
        });
    }

    if (zis_unlikely(res_len > STR_OBJ_LENGTH_MAX)) {
        string_obj_too_long_error(z);
        return NULL;
    }

    struct zis_string_obj *res_str;
    if (separator) {
        zis_locals_decl_1(z, var, struct zis_string_obj *sep);
        var.sep = separator;
        res_str = string_obj_alloc(z, res_size, res_len);
        separator = var.sep;
        zis_locals_drop(z, var);
    } else {
        res_str = string_obj_alloc(z, res_size, res_len);
    }

    {
        bool is_first_item = true;
        zis_char8_t *p = string_obj_data(res_str);
        zis_object_vec_view_foreach_unchanged(items, item, {
            if (is_first_item) {
                is_first_item = false;
            } else if (separator) {
                const size_t n = string_obj_size(separator);
                memcpy(p, string_obj_as_u8str(separator), n);
                p += n;
            }
            if (zis_object_is_smallint(item)) {
                zis_smallint_t item_smi = zis_smallint_from_ptr(item);
                zis_wchar_t c = (zis_wchar_t)(zis_smallint_unsigned_t)item_smi;
                p += zis_u8char_from_code(c, p);
            } else {
                assert(zis_object_type(item) == z->globals->type_String);
                struct zis_string_obj *str = zis_object_cast(item, struct zis_string_obj);
                const size_t n = string_obj_size(str);
                memcpy(p, string_obj_as_u8str(str), n);
                p += n;
            }
        });
        assert(p == string_obj_as_u8str(res_str) + string_obj_size(res_str));
    }

    return res_str;
}

struct zis_string_obj *zis_string_obj_concat(
    struct zis_context *z,
    struct zis_object_vec_view items
) {
    return zis_string_obj_join(z, NULL, items);
}

struct zis_string_obj *zis_string_obj_concat2(
    struct zis_context *z,
    struct zis_string_obj *_str1, struct zis_string_obj *_str2
) {
    zis_locals_decl(
        z, var,
        struct zis_string_obj *str1, *str2;
    );
    var.str1 = _str1, var.str2 = _str2;
    const size_t str1_size = string_obj_size(var.str1), str2_size = string_obj_size(var.str2);
    const size_t res_size = str1_size + str2_size;
    const size_t res_len = string_obj_length(var.str1) + string_obj_length(var.str2);
    if (zis_unlikely(res_size < str1_size || res_len > STR_OBJ_LENGTH_MAX)) {
        string_obj_too_long_error(z);
        return NULL;
    }
    struct zis_string_obj *res = string_obj_alloc(z, res_size, res_len);
    memcpy(string_obj_data(res), string_obj_as_u8str(var.str1), str1_size);
    memcpy(string_obj_data(res) + str1_size, string_obj_as_u8str(var.str2), str2_size);
    zis_locals_drop(z, var);
    return res;
}

bool zis_string_obj_equals(struct zis_string_obj *lhs, struct zis_string_obj *rhs) {
    if (string_obj_length(lhs) != string_obj_length(rhs))
        return false;
    const size_t lhs_size = string_obj_size(lhs);
    if (lhs_size != string_obj_size(rhs))
        return false;
    return memcmp(string_obj_as_u8str(lhs), string_obj_as_u8str(rhs), lhs_size) == 0;
}

int zis_string_obj_compare(struct zis_string_obj *lhs, struct zis_string_obj *rhs) {
    const size_t lhs_size = string_obj_size(lhs), rhs_size = string_obj_size(rhs);
    if (lhs_size <= rhs_size) {
        const int x = memcmp(string_obj_as_u8str(lhs), string_obj_as_u8str(rhs), lhs_size);
        return x != 0 ? x : -1;
    } else {
        const int x = memcmp(string_obj_as_u8str(lhs), string_obj_as_u8str(rhs), rhs_size);
        return x != 0 ? x : 1;
    }
}

void zis_string_obj_write_to_stream(struct zis_string_obj *self, struct zis_stream_obj *stream) {
    const char *str_data = (const char *)string_obj_as_u8str(self);
    const size_t str_size = string_obj_size(self);
    size_t buffer_size;
    char *buffer = zis_stream_obj_char_buf_ptr(stream, 0, &buffer_size);
    if (str_size <= buffer_size) {
        memcpy(buffer, str_data, str_size);
        zis_stream_obj_char_buf_ptr(stream, str_size, NULL);
    } else {
        zis_stream_obj_write_chars(stream, str_data, str_size);
    }
}

/* ----- type definition ---------------------------------------------------- */

#define assert_arg1_String(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_String)))

ZIS_NATIVE_FUNC_DEF(T_String_M_operator_add, z, {2, 0, 2}) {
    /*#DOCSTR# func String:\'+'(s :: String) :: String
    Concatenates two strings. */
    assert_arg1_String(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_String))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "+", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    struct zis_string_obj *result =
        zis_string_obj_join(z, NULL, zis_object_vec_view_from_frame(frame, 1, 2));
    frame[0] = zis_object_from(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_M_operator_get_elem, z, {2, 0, 2}) {
    /*#DOCSTR# func String:\'[]'(position :: Int | Range) :: Int
    Gets the character at `position`. */
    assert_arg1_String(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_string_obj *self = zis_object_cast(frame[1], struct zis_string_obj);
    struct zis_object *position_obj = frame[2];

    if (zis_object_is_smallint(position_obj)) {
        size_t index = zis_object_index_convert(string_obj_length(self), zis_smallint_from_ptr(position_obj));
        if (index == (size_t)-1)
            goto index_out_of_range;
        const zis_wchar_t c = zis_string_obj_get(self, index);
        if (c == (zis_wchar_t)-1)
            goto index_out_of_range;
        frame[0] = zis_smallint_to_ptr((zis_smallint_t)c);
    return ZIS_OK;
    } else if (zis_object_type_is(position_obj, g->type_Range)) {
        struct zis_object_index_range_convert_args ca = {
            .range = zis_object_cast(position_obj, struct zis_range_obj),
            .length = string_obj_length(self),
        };
        if (!zis_object_index_range_convert(&ca))
            goto index_out_of_range;
        struct zis_string_obj *res = zis_string_obj_slice(z, self, ca.offset, ca.count);
        if (!res)
            goto index_out_of_range;
        frame[0] = zis_object_from(res);
        return ZIS_OK;
    } else if (zis_object_type_is(position_obj, g->type_Int)) {
    index_out_of_range:
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, position_obj
        ));
        return ZIS_THR;
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN, "[]", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
}

ZIS_NATIVE_FUNC_DEF(T_String_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func String:\'=='(other :: String) :: Bool
    Operator ==. */
    assert_arg1_String(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_String))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "==", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    const bool result = zis_string_obj_equals(
        zis_object_cast(frame[1], struct zis_string_obj),
        zis_object_cast(frame[2], struct zis_string_obj)
    );
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func String:\'<=>'(other :: String) :: Int
    Operator <=>. */
    assert_arg1_String(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    if (zis_unlikely(!zis_object_type_is(frame[2], g->type_String))) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
            "==", frame[1], frame[2]
        ));
        return ZIS_THR;
    }
    const int result = zis_string_obj_compare(
        zis_object_cast(frame[1], struct zis_string_obj),
        zis_object_cast(frame[2], struct zis_string_obj)
    );
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_M_length, z, {1, 0, 1}) {
    /*#DOCSTR# func String:length() :: Int
    Returns the number of characters in the string. */
    assert_arg1_String(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_string_obj *self = zis_object_cast(frame[1], struct zis_string_obj);
    const size_t len = string_obj_length(self);
    assert(len <= ZIS_SMALLINT_MAX);
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)len);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func String:hash() :: Int
    Generates hash code. */
    assert_arg1_String(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_string_obj *self = zis_object_cast(frame[1], struct zis_string_obj);
    const size_t h = zis_hash_bytes(string_obj_as_u8str(self), string_obj_size(self));
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)h);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func String:to_string(?fmt) :: String
    Returns string. */
    assert_arg1_String(z);
    struct zis_object **frame = z->callstack->frame;
    // TODO: formatting.
    frame[0] = frame[1];
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_F_join, z, {1, -1, 2}) {
    /*#DOCSTR# func String.join(separator :: String, *items :: String|Int) :: String
    Concatenates strings and characters, using the specified separator between them. */
    /*#DOCSTR# func String.join(separator :: String, items :: Tuple[String|Int]) :: String
    Concatenates an array of strings and characters, using the specified separator between them. */
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_string_obj *separator;
    if (zis_object_type_is(frame[1], g->type_String)) {
        separator = zis_object_cast(frame[1], struct zis_string_obj);
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE, "separator", frame[1]
        ));
        return ZIS_THR;
    }
    struct zis_object_vec_view items;
    {
        assert(zis_object_type_is(frame[2], g->type_Tuple));
        struct zis_tuple_obj *items_tuple = zis_object_cast(frame[2], struct zis_tuple_obj);
        size_t item_count = zis_tuple_obj_length(items_tuple);
        if (item_count == 1 && zis_object_type_is(zis_tuple_obj_get(items_tuple, 0), g->type_Array)) {
            struct zis_array_slots_obj *item_slots =
                zis_object_cast(zis_tuple_obj_get(items_tuple, 0), struct zis_array_obj)->_data;
            item_count = zis_array_slots_obj_length(item_slots);
            frame[2] = zis_object_from(item_slots);
            items = zis_object_vec_view_from_fields(frame[2], struct zis_array_slots_obj, _data, 0, item_count);
        } else {
            items = zis_object_vec_view_from_fields(frame[2], struct zis_tuple_obj, _data, 0, item_count);
        }
    }
    struct zis_string_obj *new_str = zis_string_obj_join(z, separator, items);
    frame[0] = zis_object_from(new_str);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_F_concat, z, {0, -1, 1}) {
    /*#DOCSTR# func String.concat(*items :: String|Int) :: String
    Concatenates strings and characters. */
    /*#DOCSTR# func String.concat(items :: Tuple[String|Int]) :: String
    Concatenates an array of strings and characters. */
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object_vec_view items;
    {
        assert(zis_object_type_is(frame[1], g->type_Tuple));
        struct zis_tuple_obj *items_tuple = zis_object_cast(frame[1], struct zis_tuple_obj);
        size_t item_count = zis_tuple_obj_length(items_tuple);
        if (item_count == 1 && zis_object_type_is(zis_tuple_obj_get(items_tuple, 0), g->type_Array)) {
            struct zis_array_slots_obj *item_slots =
                zis_object_cast(zis_tuple_obj_get(items_tuple, 0), struct zis_array_obj)->_data;
            item_count = zis_array_slots_obj_length(item_slots);
            frame[1] = zis_object_from(item_slots);
            items = zis_object_vec_view_from_fields(frame[1], struct zis_array_slots_obj, _data, 0, item_count);
        } else {
            items = zis_object_vec_view_from_fields(frame[1], struct zis_tuple_obj, _data, 0, item_count);
        }
    }
    struct zis_string_obj *new_str = zis_string_obj_join(z, NULL, items);
    frame[0] = zis_object_from(new_str);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_String_D_methods,
    { "+"           , &T_String_M_operator_add      },
    { "[]"          , &T_String_M_operator_get_elem },
    { "=="          , &T_String_M_operator_equ      },
    { "<=>"         , &T_String_M_operator_cmp      },
    { "length"      , &T_String_M_length            },
    { "hash"        , &T_String_M_hash              },
    { "to_string"   , &T_String_M_to_string         },
);

ZIS_NATIVE_VAR_DEF_LIST(
    T_String_D_statics,
    { "join"       , { '^', .F = &T_String_F_join   } },
    { "concat"     , { '^', .F = &T_String_F_concat } },
);

ZIS_NATIVE_TYPE_DEF_XB(
    String,
    struct zis_string_obj, _bytes_size,
    NULL, T_String_D_methods, T_String_D_statics
);

/* ----- string builder ----------------------------------------------------- */

struct zis_string_builder_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *appended_item_count;
    struct zis_array_slots_obj *appended_items;
    struct zis_array_obj *concatted_strings;
};

#define STRING_BUILDER_BUFFER_SIZE 64

struct zis_string_builder_obj *zis_string_builder_obj_new(struct zis_context *z) {
    zis_locals_decl(
        z, var,
        struct zis_array_slots_obj *appended_items;
        struct zis_array_obj *concatted_strings;
    );
    zis_locals_zero(var);
    var.appended_items = zis_array_slots_obj_new(z, NULL, STRING_BUILDER_BUFFER_SIZE);
    var.concatted_strings = zis_array_obj_new2(z, 2, NULL, 0);
    struct zis_object *const _obj = zis_objmem_alloc(z, z->globals->type_String_Builder);
    struct zis_string_builder_obj *sb = zis_object_cast(_obj, struct zis_string_builder_obj);
    sb->appended_item_count = zis_smallint_to_ptr(0);
    sb->appended_items = var.appended_items;
    sb->concatted_strings = var.concatted_strings;
    zis_locals_drop(z, var);
    return sb;
}

static void _string_builder_obj_append(
    struct zis_context *z,
    struct zis_string_builder_obj *sb, struct zis_object *item
) {
    assert(zis_object_is_smallint(item) || zis_object_type_is(item, z->globals->type_String));
    assert(zis_object_is_smallint(sb->appended_item_count));
    assert(zis_smallint_from_ptr(sb->appended_item_count) >= 0);
    size_t appended_item_count = (zis_smallint_unsigned_t)zis_smallint_from_ptr(sb->appended_item_count);
    assert(appended_item_count <= zis_array_slots_obj_length(sb->appended_items));
    if (appended_item_count == zis_array_slots_obj_length(sb->appended_items)) {
        zis_locals_decl(
            z, var,
            struct zis_string_builder_obj *sb;
            struct zis_object *item;
            struct zis_array_slots_obj *appended_items;
        );
        var.sb = sb, var.item = item, var.appended_items = sb->appended_items;
        struct zis_string_obj *cs = zis_string_obj_join(
            z, NULL, zis_object_vec_view_from_fields(
                var.appended_items, struct zis_array_slots_obj, _data,
                0, appended_item_count
            )
        );
        assert(cs);
        zis_array_obj_append(z, var.sb->concatted_strings, zis_object_from(cs));
        sb = var.sb, item = var.item;
        assert(sb->appended_items == var.appended_items);
        zis_locals_drop(z, var);
        appended_item_count = 0;
        zis_object_vec_zero(sb->appended_items->_data, zis_array_slots_obj_length(sb->appended_items));
    }
    zis_array_slots_obj_set(sb->appended_items, appended_item_count++, item);
    sb->appended_item_count = zis_smallint_to_ptr((zis_smallint_t)(zis_smallint_unsigned_t)appended_item_count);
}

void zis_string_builder_obj_append(
    struct zis_context *z,
    struct zis_string_builder_obj *sb, struct zis_string_obj *s
) {
    if (string_obj_length(s)) {
        _string_builder_obj_append(z, sb, zis_object_from(s));
    }
}

bool zis_string_builder_obj_append_char(
    struct zis_context *z,
    struct zis_string_builder_obj *sb, zis_string_obj_wchar_t c
) {
    if (c > 0x10ffff)
        return false;
    _string_builder_obj_append(z, sb, zis_smallint_to_ptr((zis_smallint_t)(zis_smallint_unsigned_t)c));
    return true;
}

struct zis_string_obj *zis_string_builder_obj_string(
    struct zis_context *z, struct zis_string_builder_obj *_sb
) {
    zis_locals_decl(
        z, var,
        struct zis_string_builder_obj *sb;
        struct zis_array_slots_obj *items;
    );
    var.sb = _sb, var.items = z->globals->val_empty_array_slots;

    assert(zis_object_is_smallint(var.sb->appended_item_count));
    assert(zis_smallint_from_ptr(var.sb->appended_item_count) >= 0);
    if (zis_smallint_from_ptr(var.sb->appended_item_count)) {
        size_t item_count =
            (zis_smallint_unsigned_t)zis_smallint_from_ptr(var.sb->appended_item_count);
        var.items = var.sb->appended_items;
        struct zis_string_obj *cs = zis_string_obj_join(
            z, NULL, zis_object_vec_view_from_fields(
                var.items, struct zis_array_slots_obj, _data,
                0, item_count
            )
        );
        assert(cs);
        zis_array_obj_append(z, var.sb->concatted_strings, zis_object_from(cs));
        var.sb->appended_item_count = zis_smallint_to_ptr(0);
        zis_object_vec_zero(var.sb->appended_items->_data, item_count);
    }

    size_t concatted_strings_n = zis_array_obj_length(var.sb->concatted_strings);
    struct zis_string_obj *result;
    if (concatted_strings_n == 1) {
        struct zis_object *x = zis_array_obj_get(var.sb->concatted_strings, 0);
        assert(zis_object_type_is(x, z->globals->type_String));
        result = zis_object_cast(x, struct zis_string_obj);
    } else if (concatted_strings_n > 1) {
        var.items = var.sb->concatted_strings->_data;
        result = zis_string_obj_join(
            z, NULL, zis_object_vec_view_from_fields(
                var.items, struct zis_array_slots_obj, _data,
                0, concatted_strings_n
            )
        );
        assert(result);
        zis_array_obj_clear(var.sb->concatted_strings);
        zis_array_obj_append(z, var.sb->concatted_strings, zis_object_from(result));
    } else {
        result = z->globals->val_empty_string;
    }

    zis_locals_drop(z, var);
    return result;
}

void zis_string_builder_obj_clear(struct zis_string_builder_obj *sb) {
    sb->appended_item_count = zis_smallint_to_ptr(0);
    zis_object_vec_zero(sb->appended_items->_data, zis_array_slots_obj_length(sb->appended_items));
    zis_array_obj_clear(sb->concatted_strings);
}

ZIS_NATIVE_TYPE_DEF_NB(
    String_Builder,
    struct zis_string_builder_obj,
    NULL, NULL, NULL
);
