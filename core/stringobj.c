#include "stringobj.h"

#include <assert.h>
#include <string.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "strutil.h"

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
    STR_OBJ_C1 = 0, // U+0000 ~ U+00FF
    STR_OBJ_C2 = 1, // U+0000 ~ U+FFFF
    STR_OBJ_C4 = 3, // U+0000 ~ U+10FFFF
};

#define STR_OBJ_C1_CODE_MAX  0x00ff
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
    if (n != (size_t)-1) {
        const zis_char8_t *src_p = (const zis_char8_t *)s, *const src_end = src_p + n;
        while (src_p < src_end) {
            zis_wchar_t  codepoint;
            const size_t char_len = zis_u8char_to_code(&codepoint, src_p);
            if (zis_unlikely(!char_len)) // error at (src_p - s)
                return NULL;
            if (codepoint > codepoint_max)
                codepoint_max = codepoint;
            src_p += char_len;
            char_count++;
        }
        if (src_p != src_end) // error at (src_end - 1 - s)
            return NULL;
    } else {
        // `n` is unknown
        const zis_char8_t *src_p = (const zis_char8_t *)s;
        while (true) {
            zis_wchar_t  codepoint;
            const size_t char_len = zis_u8char_to_code(&codepoint, src_p);
            if (zis_unlikely(!char_len)) // error at (src_p - s)
                return NULL;
            if (codepoint > codepoint_max)
                codepoint_max = codepoint;
            else if (zis_unlikely(codepoint == 0))
                break;
            src_p += char_len;
            char_count++;
        }
    }

#define COPY_STR_DATA(C_X) \
    do {                   \
        const zis_char8_t *src_p = (const zis_char8_t *)s; \
        string_obj_c##C_X##_t *dst_p = string_obj_data(self); \
        for (size_t i = 0; i < char_count; i++) {          \
            zis_wchar_t ch;\
            src_p   += zis_u8char_to_code(&ch, src_p);     \
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

size_t zis_string_obj_value(const struct zis_string_obj *self, char *buf, size_t buf_sz) {
    const enum string_obj_char_type char_type  = string_obj_char_type(self);
    const size_t char_count = string_obj_length(self);

    switch (char_type) {

#define COPY_STR_DATA(C_X) \
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
// ^^^ COPY_STR_DATA() ^^^

    case STR_OBJ_C1:
        COPY_STR_DATA(1);
    case STR_OBJ_C2:
        COPY_STR_DATA(2);
    case STR_OBJ_C4:
        COPY_STR_DATA(4);
    default:
        zis_unreachable();

#undef COPY_STR_DATA

    }
}

/* ----- type definition ---------------------------------------------------- */

ZIS_NATIVE_FUNC_LIST_DEF(
    string_methods,
);

ZIS_NATIVE_FUNC_LIST_DEF(
    string_statics,
);

ZIS_NATIVE_TYPE_DEF_XB(
    String,
    struct zis_string_obj, _bytes_size,
    NULL,
    ZIS_NATIVE_FUNC_LIST_VAR(string_methods),
    ZIS_NATIVE_FUNC_LIST_VAR(string_statics)
);
