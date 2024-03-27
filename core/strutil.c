#include "strutil.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "attributes.h"
#include "platform.h"

#if ZIS_SYSTEM_POSIX
#    include <strings.h> // strcasecmp()
#endif // ZIS_SYSTEM_POSIX

static_assert(sizeof(zis_char8_t) == 1, "");
static_assert(sizeof(zis_wchar_t) == 4, "");

int zis_str_icmp(const char *s1, const char *s2) {
#if ZIS_SYSTEM_WINDOWS
    return _stricmp(s1, s2);
#elif ZIS_SYSTEM_POSIX
    return strcasecmp(s1, s2);
#else
    while (true) {
        const int c1 = (int)*s1++, c2 = (int)*s2++;
        const int d = tolower(c1) - tolower(c2);
        if (d || !c1)
            return d;
    }
#endif
}

void zis_str_toupper(char *restrict s, size_t n) {
    for (size_t i = 0; i < n; i++)
        s[i] = (char)toupper(s[i]);
}

size_t zis_u8char_from_code(
    zis_wchar_t code, zis_char8_t utf8_char_buf[ZIS_PARAMARRAY_STATIC 4]
) {
    if (code < 0x80) {
        utf8_char_buf[0] = (zis_char8_t)code;
        return 1;
    }
    if (code <= 0x7ff) {
        utf8_char_buf[0] = 0xc0 | (zis_char8_t)(code >> 6);
        utf8_char_buf[1] = 0x80 | (zis_char8_t)(code & 0x3f);
        return 2;
    }
    if (code <= 0xffff) {
        utf8_char_buf[0] = 0xe0 | (zis_char8_t)(code >> 12);
        utf8_char_buf[1] = 0x80 | (zis_char8_t)((code >> 6) & 0x3f);
        utf8_char_buf[2] = 0x80 | (zis_char8_t)(code & 0x3f);
        return 3;
    }
    if (code <= 0x1fffff) {
        utf8_char_buf[0] = 0xf0 | (zis_char8_t)(code >> 18);
        utf8_char_buf[1] = 0x80 | (zis_char8_t)((code >> 12) & 0x3f);
        utf8_char_buf[2] = 0x80 | (zis_char8_t)((code >> 6) & 0x3f);
        utf8_char_buf[3] = 0x80 | (zis_char8_t)(code & 0x3f);
        return 4;
    }
    return 0;
}

size_t zis_u8char_to_code(
    zis_wchar_t *restrict code,
    const zis_char8_t *restrict utf8_char, const zis_char8_t *restrict utf8_char_end
) {
    const size_t n = zis_u8char_len_1(*utf8_char); // Or `zis_u8char_len_s()`?
    if (zis_unlikely(utf8_char + n > utf8_char_end))
        return 0;
    switch (n) {
    case 0:
        break;
    case 1:
        *code = utf8_char[0];
        break;
    case 2:
        *code =
            ((utf8_char[0] & 0x1f) << 6) |
            ((utf8_char[1] & 0x3f)     ) ;
        break;
    case 3:
        *code =
            ((utf8_char[0] & 0x0f) << 12) |
            ((utf8_char[1] & 0x3f) <<  6) |
            ((utf8_char[2] & 0x3f)      ) ;
        break;
    case 4:
        *code =
            ((utf8_char[0] & 0x07) << 18) |
            ((utf8_char[1] & 0x3f) << 12) |
            ((utf8_char[2] & 0x3f) <<  6) |
            ((utf8_char[3] & 0x3f)      ) ;
        break;
    default:
        zis_unreachable();
    }
    return n;
}

size_t zis_u8char_len_from_code(zis_wchar_t code) {
    if (code < 0x80)
        return 1;
    if (code <= 0x7ff)
        return 2;
    if (code <= 0xffff)
        return 3;
    if (code <= 0x1fffff)
        return 4;
    return 0;
}

size_t _zis_u8char_len_1_mb(zis_char8_t u8_first_byte) {
    assert((u8_first_byte & 0x80) != 0);
    if ((u8_first_byte & 0xe0) == 0xc0)
        return 2;
    if ((u8_first_byte & 0xf0) == 0xe0)
        return 3;
    if ((u8_first_byte & 0xf8) == 0xf0)
        return 4;
    return 0;
}

int zis_u8char_len_s(const zis_char8_t *u8_str, size_t n_bytes) {
    if (zis_unlikely(n_bytes < 1))
        return -1 - 0;
    const zis_char8_t c1 = u8_str[0];
    if ((c1 & 0x80) == 0x00)
        return 1;
    if (zis_unlikely(n_bytes < 2))
        return -1 - 0;
    const zis_char8_t c2 = u8_str[1];
    if (zis_unlikely((c2 & 0xc0) != 0x80))
        return -1 - 1;
    if ((c1 & 0xe0) == 0xc0)
        return 2;
    if (zis_unlikely(n_bytes < 3))
        return -1 - 0;
    const zis_char8_t c3 = u8_str[2];
    if (zis_unlikely((c3 & 0xc0) != 0x80))
        return -1 - 2;
    if ((c1 & 0xf0) == 0xe0)
        return 3;
    if (zis_unlikely(n_bytes < 4))
        return -1 - 0;
    const zis_char8_t c4 = u8_str[3];
    if (zis_unlikely((c4 & 0xc0) != 0x80))
        return -1 - 3;
    if ((c1 & 0xf8) == 0xf0)
        return 4;
    return -1 - 0;
}

size_t zis_u8str_len(const zis_char8_t *u8_str) {
    size_t len = 0;
    while (true) {
        const zis_char8_t c = *u8_str;
        if (zis_unlikely(!c))
            return len;
        const size_t n = zis_u8char_len_1(c);
        if (zis_unlikely(!n))
            goto error;
        u8_str += n;
        len++;
    }
    zis_unreachable();
error:
    return len;
}

int zis_u8str_len_s(const zis_char8_t *u8_str, size_t n_bytes) {
    size_t len = 0;
    const zis_char8_t *p = u8_str, *const p_end = p + n_bytes;
    if (zis_unlikely(!n_bytes))
        return 0;
    while (true) {
        if (zis_unlikely(!*p))
            goto ret_len;
        const size_t n = zis_u8char_len_s(p, 4);
        if (zis_unlikely(!n))
            goto error;
        p += n;
        len++;
        if (zis_unlikely(p >= p_end)) {
            if (zis_unlikely(p > p_end)) {
                p -= n;
                goto error;
            }
        ret_len:
            assert((size_t)(int)len == len);
            return (int)len;
        }
    }
    zis_unreachable();
error:
    return -1 - (int)(p - u8_str);
}

zis_char8_t *zis_u8str_find_pos(const zis_char8_t *u8_str, size_t n_chars) {
    while (n_chars-- > 0) {
        const zis_char8_t c = *u8_str;
        if (zis_unlikely(!c))
            break;
        const size_t n = zis_u8char_len_1(c);
        if (zis_unlikely(!n))
            return NULL;
        u8_str += n;
    }
    return (zis_char8_t *)u8_str;
}

zis_char8_t *zis_u8str_find_end(const zis_char8_t *u8_str, size_t max_bytes) {
    const zis_char8_t *const u8_str_end = u8_str + max_bytes;
    for (const zis_char8_t *p = u8_str_end; p >= u8_str; p--) {
        const zis_char8_t c = *p;
        if ((c & 0xc0) != 0x80) {
            const size_t n = zis_u8char_len_1(c);
            if (n && p + n <= u8_str_end)
                return (zis_char8_t *)(p + n);
        }
    }
    return NULL;
}

static const zis_wchar_t char_width_table[] = {
    0x01100, 0x01160,
    0x02329, 0x0232B,
    0x02E80, 0x0303F,
    0x03040, 0x0A4D0,
    0x0AC00, 0x0D7A4,
    0x0F900, 0x0FB00,
    0x0FE10, 0x0FE1A,
    0x0FE30, 0x0FE70,
    0x0FF00, 0x0FF61,
    0x0FFE0, 0x0FFE7,
    0x1F300, 0x1F650,
    0x1F900, 0x1FA00,
    0x20000, 0x2FFFE,
    0x30000, 0x3FFFE,
};

size_t zis_char_width(zis_wchar_t code_point) {
    if (code_point < 0x80) {
        if (zis_likely(isprint((int)code_point) || isspace((int)code_point)))
            return 1;
        return 0;
    }

    const size_t table_size = sizeof char_width_table / sizeof char_width_table[0];
    for (size_t i = 0; i < table_size; i += 2) {
        if (code_point < char_width_table[i])
            return 1;
        if (code_point < char_width_table[i + 1])
            return 2;
    }

    return 0;
}

unsigned int zis_char_digit(zis_wchar_t c) {
    return c < 0x80 ? zis_char_digit_1((char)c) : (unsigned int)-1;
}

unsigned int zis_char_digit_1(char c) {
    if (isdigit(c))
        return c - '0';
    if (isalpha(c))
        return tolower(c) - 'a' + 10;
    return (unsigned int)-1;
}
