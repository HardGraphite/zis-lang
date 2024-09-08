#include "stringobj.h"

#include <assert.h>
#include <string.h>

#include "context.h"
#include "globals.h"
#include "locals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "strutil.h"

#include "exceptobj.h"
#include "streamobj.h"

struct zis_string_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size; // !!
    size_t _type_and_length; // [1:0] -> char type, [N:2] -> length
    char _data[];
};

#define STR_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_string_obj, _bytes_size)

/* ----- internal implementation -------------------------------------------- */

enum string_obj_char_type {
    STR_OBJ_C1 = 0, // U+0000 ~ U+007F
    STR_OBJ_C2 = 1, // U+0000 ~ U+FFFF
    STR_OBJ_C4 = 3, // U+0000 ~ U+10FFFF
};

#define STR_OBJ_C1_CODE_MAX  0x007f
#define STR_OBJ_C2_CODE_MAX  0xffff

typedef uint8_t  string_obj_c1_t;
typedef uint16_t string_obj_c2_t;
typedef uint32_t string_obj_c4_t;

/// Get size of a char.
zis_static_force_inline size_t string_obj_char_size(enum string_obj_char_type ct) {
    return (size_t)ct + 1;
}

/// Get string char type.
zis_static_force_inline enum string_obj_char_type
string_obj_char_type(const struct zis_string_obj *self) {
    enum string_obj_char_type ct = (enum string_obj_char_type)(self->_type_and_length & 3);
    assert(ct == STR_OBJ_C1 || ct == STR_OBJ_C2 || ct == STR_OBJ_C4);
    return ct;
}

/// Get string length (number of characters).
zis_static_force_inline size_t string_obj_length(const struct zis_string_obj *self) {
    return self->_type_and_length >> 2;
}

/// Get string data.
zis_static_force_inline void *string_obj_data(struct zis_string_obj *self) {
    return self->_data;
}

/// Allocate but do not initialize the data.
static struct zis_string_obj *string_obj_alloc(
    struct zis_context *z,
    enum string_obj_char_type ct, size_t len
) {
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_String,
        0, STR_OBJ_BYTES_FIXED_SIZE + len * string_obj_char_size(ct)
    );
    struct zis_string_obj *const self = zis_object_cast(obj, struct zis_string_obj);
    assert(!(len & ~(SIZE_MAX >> 2)));
    self->_type_and_length = (len << 2) | (size_t)ct;
    return self;
}

/* ----- public functions --------------------------------------------------- */

struct zis_string_obj *zis_string_obj_new(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */
) {
    if (zis_unlikely(n == (size_t)-1))
        n = strlen(s);

    if (zis_unlikely(!n))
        return z->globals->val_empty_string;

    zis_wchar_t codepoint_max = 0;
    size_t      char_count    = 0;
    assert(n != (size_t)-1);
    {
        const zis_char8_t *src_p = (const zis_char8_t *)s, *const src_end = src_p + n;
        while (src_p < src_end) {
            zis_wchar_t  codepoint;
            const size_t char_len = zis_u8char_to_code(&codepoint, src_p, src_end);
            if (zis_unlikely(!char_len)) // error at (src_p - s)
                return NULL;
            if (codepoint > codepoint_max)
                codepoint_max = codepoint;
            src_p += char_len;
            char_count++;
        }
        if (src_p != src_end) // error at (src_end - 1 - s)
            return NULL;
    }

#define COPY_STR_DATA(C_X) \
    do {                   \
        const zis_char8_t *src_p = (const zis_char8_t *)s; \
        string_obj_c##C_X##_t *dst_p = string_obj_data(self); \
        for (size_t i = 0; i < char_count; i++) {          \
            zis_wchar_t ch;\
            src_p   += zis_u8char_to_code(&ch, src_p, (void *)UINTPTR_MAX); \
            *dst_p++ = (string_obj_c##C_X##_t)ch;          \
        }                  \
    } while (0)            \
// ^^^ COPY_STR_DATA() ^^^

    struct zis_string_obj *self;
    if (codepoint_max <= STR_OBJ_C1_CODE_MAX) {
        self = string_obj_alloc(z, STR_OBJ_C1, char_count);
        if (codepoint_max < 0x80)
            memcpy(string_obj_data(self), s, char_count);
        else
            COPY_STR_DATA(1);
    } else if (codepoint_max <= STR_OBJ_C2_CODE_MAX) {
        self = string_obj_alloc(z, STR_OBJ_C2, char_count);
        COPY_STR_DATA(2);
    } else {
        self = string_obj_alloc(z, STR_OBJ_C4, char_count);
        COPY_STR_DATA(4);
    }
    return self;

#undef COPY_STR_DATA
}

struct zis_string_obj *zis_string_obj_new_esc(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */,
    zis_string_obj_wchar_t (*escape_translator)(const char *restrict s, const char **restrict s_end)
) {
    // Adapted from `zis_string_obj_new()`.

    if (zis_unlikely(n == (size_t)-1))
        n = strlen(s);

    if (zis_unlikely(!n))
        return z->globals->val_empty_string;

    zis_wchar_t codepoint_max = 0;
    size_t      char_count    = 0;
    bool        has_esc_seq   = false;
    assert(n != (size_t)-1);
    {
        const zis_char8_t *src_p = (const zis_char8_t *)s, *const src_end = src_p + n;
        while (src_p < src_end) {
            zis_wchar_t  codepoint;
            const size_t char_len = zis_u8char_to_code(&codepoint, src_p, src_end);
            if (zis_unlikely(!char_len)) // error at (src_p - s)
                return NULL;
            if (codepoint == '\\') {
                has_esc_seq = true;
                const char *esc_end_p = s + n;
                codepoint = escape_translator((const char *)src_p + 1, &esc_end_p);
                if (codepoint == (zis_wchar_t)-1)
                    return NULL;
                assert(esc_end_p <= s + n);
                src_p = (const zis_char8_t *)esc_end_p - 1;
            }
            if (codepoint > codepoint_max)
                codepoint_max = codepoint;
            src_p += char_len;
            char_count++;
        }
        if (src_p != src_end) // error at (src_end - 1 - s)
            return NULL;
    }

#define COPY_STR_DATA(C_X) \
    do {                   \
        const zis_char8_t *src_p = (const zis_char8_t *)s; \
        string_obj_c##C_X##_t *dst_p = string_obj_data(self); \
        for (size_t i = 0; i < char_count; i++) {          \
            zis_wchar_t ch;\
            src_p   += zis_u8char_to_code(&ch, src_p, (void *)UINTPTR_MAX); \
            if (ch == '\\') {                              \
                const char *esc_end_p = s + n;             \
                ch = escape_translator((const char *)src_p, &esc_end_p);    \
                assert(ch != (zis_wchar_t)-1);             \
                assert(esc_end_p <= s + n);                \
                src_p = (const zis_char8_t *)esc_end_p;    \
            }              \
            *dst_p++ = (string_obj_c##C_X##_t)ch;          \
        }                  \
    } while (0) // ^^^ COPY_STR_DATA() ^^^

    struct zis_string_obj *self;
    if (codepoint_max <= STR_OBJ_C1_CODE_MAX) {
        self = string_obj_alloc(z, STR_OBJ_C1, char_count);
        if (codepoint_max < 0x80 && !has_esc_seq)
            memcpy(string_obj_data(self), s, char_count);
        else
            COPY_STR_DATA(1);
    } else if (codepoint_max <= STR_OBJ_C2_CODE_MAX) {
        self = string_obj_alloc(z, STR_OBJ_C2, char_count);
        COPY_STR_DATA(2);
    } else {
        self = string_obj_alloc(z, STR_OBJ_C4, char_count);
        COPY_STR_DATA(4);
    }
    return self;

#undef COPY_STR_DATA
}

struct zis_string_obj *_zis_string_obj_new_empty(struct zis_context *z) {
    return string_obj_alloc(z, STR_OBJ_C1, 0U);
}

struct zis_string_obj *zis_string_obj_from_char(
    struct zis_context *z, zis_string_obj_wchar_t ch
) {
    struct zis_string_obj *self;
    if (ch <= STR_OBJ_C1_CODE_MAX) {
        self = string_obj_alloc(z, STR_OBJ_C1, 1);
        string_obj_c1_t *const data = string_obj_data(self);
        data[0] = (string_obj_c1_t)ch;
    } else if (ch <= STR_OBJ_C2_CODE_MAX) {
        self = string_obj_alloc(z, STR_OBJ_C2, 1);
        string_obj_c2_t *const data = string_obj_data(self);
        data[0] = (string_obj_c2_t)ch;
    } else {
        self = string_obj_alloc(z, STR_OBJ_C4, 1);
        string_obj_c4_t *const data = string_obj_data(self);
        data[0] = (string_obj_c4_t)ch;
    }
    return self;
}

size_t zis_string_obj_length(const struct zis_string_obj *self) {
    return string_obj_length(self);
}

size_t zis_string_obj_to_u8str(const struct zis_string_obj *self, char *buf, size_t buf_sz) {
    const enum string_obj_char_type char_type  = string_obj_char_type(self);
    const size_t char_count = string_obj_length(self);

    switch (char_type) {

#define CONV_STR_DATA(C_X) \
    do {                   \
        const string_obj_c##C_X##_t *const data = string_obj_data((void *)self); \
        if (buf) {         \
            zis_char8_t *buf_p = (zis_char8_t *)buf;                             \
            zis_char8_t *const buf_end = buf_p + buf_sz;                         \
            zis_char8_t *const buf_near_end = buf_end - 4;                       \
            for (size_t i = 0; i < char_count; i++) {                            \
                if (zis_likely(buf_p <= buf_near_end)) {                         \
                    const size_t n = zis_u8char_from_code(data[i], buf_p);       \
                    assert(n && n <= 4);                                         \
                    buf_p += n;                                                  \
                } else {   \
                    zis_char8_t u8c[4];                                          \
                    const size_t n = zis_u8char_from_code(data[i], u8c);         \
                    assert(n && n <= 4);                                         \
                    if (zis_unlikely(buf_p + n > buf_end))                       \
                        return (size_t)-1;                                       \
                    memcpy(buf_p, u8c, n);                                       \
                    buf_p += n;                                                  \
                }          \
            }              \
            return (size_t)((char *)buf_p - buf);                                \
        } else {           \
            size_t n_bytes = 0;                                                  \
            for (size_t i = 0; i < char_count; i++) {                            \
                const size_t n = zis_u8char_len_from_code(data[i]);              \
                assert(n); \
                n_bytes += n;                                                    \
            }              \
            return n_bytes;\
        }                  \
    } while (0)            \
// ^^^ CONV_STR_DATA() ^^^

    case STR_OBJ_C1:
        if (buf) {
            static_assert(STR_OBJ_C1_CODE_MAX <= 0x7f, "");
            string_obj_c1_t *const data = string_obj_data((void *)self);
            if (buf_sz < char_count)
                return (size_t)-1;
            memcpy(buf, data, char_count);
        }
        return char_count;
    case STR_OBJ_C2:
        CONV_STR_DATA(2);
    case STR_OBJ_C4:
        CONV_STR_DATA(4);
    default:
        zis_unreachable();

#undef CONV_STR_DATA

    }
}

const char *zis_string_obj_as_ascii(const struct zis_string_obj *self, size_t *len) {
    static_assert(STR_OBJ_C1_CODE_MAX <= 0x7f, "");
    if (string_obj_char_type(self) == STR_OBJ_C1) {
        if (len)
            *len = string_obj_length(self);
        return self->_data;
    }
    return NULL;
}

struct zis_string_obj *zis_string_obj_concat(
    struct zis_context *z,
    struct zis_string_obj *_str1, struct zis_string_obj *_str2
) {
    zis_locals_decl(
        z, var,
        struct zis_string_obj *str1, *str2, *new_str;
    );
    var.str1 = _str1, var.str2 = _str2, var.new_str = _str2;

    const enum string_obj_char_type
    str1t = string_obj_char_type(var.str1), str2t = string_obj_char_type(var.str2);
    const size_t str1n = string_obj_length(var.str1), str2n = string_obj_length(var.str2);

    if (str1t == str2t) {
        struct zis_string_obj *const new_str = string_obj_alloc(z, str1t, str1n + str2n);
        void *const new_str_data = string_obj_data(new_str);
        const size_t cn = string_obj_char_size(str1t);
        const size_t str1_data_size = str1n * cn, str2_data_size = str2n * cn;
        memcpy(new_str_data, string_obj_data(var.str1), str1_data_size);
        memcpy((char *)new_str_data + str1_data_size, string_obj_data(var.str2), str2_data_size);
        var.new_str = new_str;
    } else {
        zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL);
    }

    zis_locals_drop(z, var);
    return var.new_str;
}

bool zis_string_obj_equals(struct zis_string_obj *lhs, struct zis_string_obj *rhs) {
    const enum string_obj_char_type lhs_char_type  = string_obj_char_type(lhs);
    const size_t lhs_char_count = string_obj_length(lhs);
    const enum string_obj_char_type rhs_char_type  = string_obj_char_type(rhs);
    const size_t rhs_char_count = string_obj_length(rhs);

    if (lhs_char_type != rhs_char_type || lhs_char_count != rhs_char_count)
        return false;

    return memcmp(
        string_obj_data(lhs), string_obj_data(rhs),
        lhs_char_count * string_obj_char_size(lhs_char_type)
    ) == 0;
}

int zis_string_obj_compare(struct zis_string_obj *lhs, struct zis_string_obj *rhs) {
    const enum string_obj_char_type lhs_char_type = string_obj_char_type(lhs);
    const size_t lhs_char_count = string_obj_length(lhs);
    const void *lhs_data_raw = string_obj_data(lhs);
    const enum string_obj_char_type rhs_char_type = string_obj_char_type(rhs);
    const size_t rhs_char_count = string_obj_length(rhs);
    const void *rhs_data_raw = string_obj_data(rhs);

    if (lhs_char_type == rhs_char_type) {
        switch (lhs_char_type) {
        case STR_OBJ_C1:
            return memcmp(lhs_data_raw, rhs_data_raw, lhs_char_count);
        case STR_OBJ_C2:
        case STR_OBJ_C4:
            zis_unused_var(rhs_char_count);
            break;
        default:
            zis_unreachable();
        }
    }

    zis_context_panic(NULL, ZIS_CONTEXT_PANIC_IMPL);
}

void zis_string_obj_write_to_stream(struct zis_string_obj *self, struct zis_stream_obj *stream) {
    char *buffer;
    size_t size;
    buffer = zis_stream_obj_char_buf_ptr(stream, 0, &size);
    size = zis_string_obj_to_u8str(self, buffer, size);
    if (size != (size_t)-1) {
        zis_stream_obj_char_buf_ptr(stream, size, NULL);
    } else {
        // TODO: make `zis_string_obj_to_u8str()` support copying part of a string,
        // so that long strings can be copied to stream buffer separately.
        size = zis_string_obj_to_u8str(self, NULL, 0);
        buffer = zis_mem_alloc(size);
        size = zis_string_obj_to_u8str(self, buffer, size);
        assert(size != (size_t)-1);
        zis_stream_obj_write_chars(stream, buffer, size);
        zis_mem_free(buffer);
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
    struct zis_string_obj *lhs = zis_object_cast(frame[1], struct zis_string_obj);
    struct zis_string_obj *rhs = zis_object_cast(frame[2], struct zis_string_obj);
    struct zis_string_obj *result;
    if (zis_unlikely(!string_obj_length(lhs)))
        result = rhs;
    else if (zis_unlikely(!string_obj_length(rhs)))
        result = lhs;
    else
        result = zis_string_obj_concat(z, lhs, rhs);
    frame[0] = zis_object_from(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_String_M_operator_get_elem, z, {2, 0, 2}) {
    /*#DOCSTR# func String:\'[]'(position :: Int) :: Int
    Gets the character at `position`. */
    assert_arg1_String(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_string_obj *self = zis_object_cast(frame[1], struct zis_string_obj);

    size_t index;
    if (zis_object_is_smallint(frame[2])) {
        index = zis_object_index_convert(string_obj_length(self), zis_smallint_from_ptr(frame[2]));
        if (index == (size_t)-1)
            goto index_out_of_range;

    } else if (zis_object_type_is(frame[2], g->type_Int)) {
    index_out_of_range:
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_INDEX_OUT_OF_RANGE, frame[2]
        ));
        return ZIS_THR;
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN, "[]", frame[1], frame[2]
        ));
        return ZIS_THR;
    }

    zis_wchar_t ch;
    void *raw_data = string_obj_data(self);
    switch (string_obj_char_type(self)) {
    case STR_OBJ_C1:
        ch = ((const uint8_t *)raw_data)[index];
        break;
    case STR_OBJ_C2:
        ch = ((const uint16_t *)raw_data)[index];
        break;
    case STR_OBJ_C4:
        ch = ((const uint32_t *)raw_data)[index];
        break;
    default:
        zis_unreachable();
    }

    frame[0] = zis_smallint_to_ptr((zis_smallint_t)ch);
    return ZIS_OK;
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

ZIS_NATIVE_FUNC_DEF(T_String_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func String:hash() :: Int
    Generates hash code. */
    assert_arg1_String(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_string_obj *self = zis_object_cast(frame[1], struct zis_string_obj);
    const enum string_obj_char_type char_type  = string_obj_char_type(self);
    const size_t char_count = string_obj_length(self);
    const void *const str_data = string_obj_data(self);
    const size_t h = zis_hash_bytes(str_data, char_type * char_count);
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

ZIS_NATIVE_FUNC_DEF_LIST(
    T_String_D_methods,
    { "+"           , &T_String_M_operator_add      },
    { "[]"          , &T_String_M_operator_get_elem },
    { "=="          , &T_String_M_operator_equ      },
    { "<=>"         , &T_String_M_operator_cmp      },
    { "hash"        , &T_String_M_hash              },
    { "to_string"   , &T_String_M_to_string         },
);

ZIS_NATIVE_TYPE_DEF_XB(
    String,
    struct zis_string_obj, _bytes_size,
    NULL, T_String_D_methods, NULL
);
