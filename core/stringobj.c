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

#include "arrayobj.h"
#include "exceptobj.h"
#include "streamobj.h"
#include "tupleobj.h"

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

/// Minimum char type for a character.
static enum string_obj_char_type char_min_char_type(zis_wchar_t c) {
    if (c <= STR_OBJ_C1_CODE_MAX)
        return STR_OBJ_C1;
    if (c <= STR_OBJ_C2_CODE_MAX)
        return STR_OBJ_C2;
    return  STR_OBJ_C4;
}

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

/// Dummy string object that can be allocated on stack for representing a character.
typedef union dummy_string_obj_for_char {
    struct zis_string_obj string_obj;
    char _data[sizeof(struct zis_string_obj) + sizeof(zis_wchar_t)];
} dummy_string_obj_for_char;

/// Initialize a dummy_string_obj_for_char.
static void dummy_string_obj_for_char_init(dummy_string_obj_for_char *restrict ds, zis_wchar_t c) {
    enum string_obj_char_type ct = char_min_char_type(c);
    ds->string_obj._type_and_length = (1 << 2) | (size_t)ct; // See `string_obj_alloc()`.
    switch (ct) {
    case STR_OBJ_C1:
        *(string_obj_c1_t *)string_obj_data(&ds->string_obj) = (string_obj_c1_t)c;
        break;
    case STR_OBJ_C2:
        *(string_obj_c2_t *)string_obj_data(&ds->string_obj) = (string_obj_c2_t)c;
        break;
    case STR_OBJ_C4:
        *(string_obj_c4_t *)string_obj_data(&ds->string_obj) = (string_obj_c4_t)c;
        break;
    default:
        zis_unreachable();
    }
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

zis_nodiscard static string_obj_c1_t *
_zis_string_obj_join_copy_to_c1(string_obj_c1_t *dest, struct zis_string_obj *str) {
    void *str_data = string_obj_data(str);
    size_t str_len = string_obj_length(str);
    assert(string_obj_char_type(str) == STR_OBJ_C1);
    memcpy(dest, str_data, str_len * sizeof *dest);
    return dest + str_len;
}

zis_nodiscard static string_obj_c2_t *
_zis_string_obj_join_copy_to_c2(string_obj_c2_t *dest, struct zis_string_obj *str) {
    void *str_data = string_obj_data(str);
    size_t str_len = string_obj_length(str);
    switch (string_obj_char_type(str)) {
    case STR_OBJ_C1:
        for (size_t i = 0; i < str_len; i++)
            dest[i] = ((string_obj_c1_t *)str_data)[i];
        break;
    case STR_OBJ_C2:
        memcpy(dest, str_data, str_len * sizeof *dest);
        break;
    default:
        zis_unreachable();
    }
    return dest + str_len;
}

zis_nodiscard static string_obj_c4_t *
_zis_string_obj_join_copy_to_c4(string_obj_c4_t *dest, struct zis_string_obj *str) {
    void *str_data = string_obj_data(str);
    size_t str_len = string_obj_length(str);
    switch (string_obj_char_type(str)) {
    case STR_OBJ_C1:
        for (size_t i = 0; i < str_len; i++)
            dest[i] = ((string_obj_c1_t *)str_data)[i];
        break;
    case STR_OBJ_C2:
        for (size_t i = 0; i < str_len; i++)
            dest[i] = ((string_obj_c2_t *)str_data)[i];
        break;
    case STR_OBJ_C4:
        memcpy(dest, str_data, str_len * sizeof *dest);
        break;
    default:
        zis_unreachable();
    }
    return dest + str_len;
}

struct zis_string_obj *zis_string_obj_join(
    struct zis_context *z,
    struct zis_string_obj *separator /*=NULL*/, struct zis_object_vec_view items
) {
    if (zis_unlikely(zis_object_vec_view_length(items) == 0))
        return z->globals->val_empty_string;

    enum string_obj_char_type res_char_type;
    size_t res_char_count;
    if (separator) {
        res_char_type  = string_obj_char_type(separator);
        res_char_count = (zis_object_vec_view_length(items) - 1) * string_obj_length(separator);
    } else {
        res_char_type  = STR_OBJ_C1;
        res_char_count = 0;
    }

    {
        struct zis_type_obj *type_String = z->globals->type_String;
        zis_object_vec_view_foreach_unchanged(items, item, {
            enum string_obj_char_type item_char_type;
            size_t item_char_count;
            if (zis_object_is_smallint(item)) {
                zis_smallint_t item_smi = zis_smallint_from_ptr(item);
                if (zis_unlikely(item_smi < 0))
                    zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL); // TODO: handles bad code point.
                zis_wchar_t c = (zis_wchar_t)(zis_smallint_unsigned_t)item_smi;
                item_char_type = char_min_char_type(c);
                item_char_count = 1;
            } else {
                if (zis_unlikely(zis_object_type(item) != type_String))
                    zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL); // TODO: handles wrong type.
                struct zis_string_obj *str = zis_object_cast(item, struct zis_string_obj);
                item_char_type = string_obj_char_type(str);
                item_char_count = string_obj_length(str);
            }
            if (zis_unlikely((int)item_char_type > (int)res_char_type))
                res_char_type = item_char_type;
            res_char_count += item_char_count;
        });
    }

    struct zis_string_obj *res_str;
    if (separator) {
        zis_locals_decl_1(z, var, struct zis_string_obj *sep);
        var.sep = separator;
        res_str = string_obj_alloc(z, res_char_type, res_char_count);
        separator = var.sep;
        zis_locals_drop(z, var);
    } else {
        res_str = string_obj_alloc(z, res_char_type, res_char_count);
    }

    void *res_str_data_p = string_obj_data(res_str);
    switch (res_char_type) {
        dummy_string_obj_for_char dummy_str;

#define COPY_STR_DATA(C_X) \
        case STR_OBJ_C##C_X: \
            zis_object_vec_view_foreach_unchanged(items, item, { \
            if (separator && __vec_view_i) /* has separator and is not the first item */ \
                res_str_data_p = _zis_string_obj_join_copy_to_c##C_X(res_str_data_p, separator); \
            struct zis_string_obj *str; \
            if (zis_object_is_smallint(item)) { \
                zis_wchar_t c = (zis_wchar_t)(zis_smallint_unsigned_t)zis_smallint_from_ptr(item); \
                dummy_string_obj_for_char_init(&dummy_str, c); \
                str = &dummy_str.string_obj; \
            } else { \
                assert(zis_object_type_is(item, z->globals->type_String)); \
                str = zis_object_cast(item, struct zis_string_obj); \
            } \
            res_str_data_p = _zis_string_obj_join_copy_to_c##C_X(res_str_data_p, str); \
        }); \
        break; \

    COPY_STR_DATA(1);
    COPY_STR_DATA(2);
    COPY_STR_DATA(4);

#undef COPY_STR_DATA

    default:
        zis_unreachable();
    }
    assert((char *)res_str_data_p ==
        (char *)string_obj_data(res_str) + string_obj_length(res_str) * string_obj_char_size(res_char_type));

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
    struct zis_string_obj *str1, struct zis_string_obj *str2
) {
    zis_locals_decl_1(z, var, struct zis_object *items[2]);
    var.items[0] = zis_object_from(str1), var.items[1] = zis_object_from(str2);
    struct zis_object **items = var.items;
    struct zis_string_obj *res =
        zis_string_obj_join(z, NULL, zis_object_vec_view_from_frame(items, 0, 2));
    zis_locals_drop(z, var);
    return res;
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
    struct zis_string_obj *result =
        zis_string_obj_join(z, NULL, zis_object_vec_view_from_frame(frame, 1, 2));
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
