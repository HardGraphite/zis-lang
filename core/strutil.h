/// Characters and strings utilities.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "attributes.h"
#include "compat.h"

/* ----- character types ---------------------------------------------------- */

/// UTF-8 byte.
typedef uint8_t zis_char8_t;
/// UTF-32 char.
typedef uint32_t zis_wchar_t;

/* ----- C-style string utilities ------------------------------------------- */

/// Compare two string, ignoring the letter cases.
int zis_str_icmp(const char *s1, const char *s2);

/// Convert characters to uppercase.
void zis_str_toupper(char *restrict s, size_t n);

/* ----- UTF-8 support ------------------------------------------------------ */

/// Convert Unicode code point to UTF-8 character. Return character length.
/// If the code point is illegal, return 0.
size_t zis_u8char_from_code(
    zis_wchar_t code, zis_char8_t utf8_char_buf[ZIS_PARAMARRAY_STATIC 4]
);

/// Convert UTF-8 character to Unicode code point. Return character length.
/// If the code point is illegal, return 0.
size_t zis_u8char_to_code(
    zis_wchar_t *restrict code,
    const zis_char8_t *restrict utf8_char, const zis_char8_t *restrict utf8_char_end
);

/// Calculate expected UTF-8 character length (number of bytes) from code point.
size_t zis_u8char_len_from_code(zis_wchar_t code);

size_t _zis_u8char_len_1_mb(zis_char8_t u8_first_byte);

/// Calculate UTF-8 character length (number of bytes). Return 0 if it is illegal.
zis_static_force_inline size_t zis_u8char_len_1(zis_char8_t u8_first_byte) {
    return (u8_first_byte & 0x80) == 0 ? 1 : _zis_u8char_len_1_mb(u8_first_byte);
}

/// Calculate length of next UTF-8 character. Return 0 if it is illegal.
size_t zis_u8char_len_s(const zis_char8_t *u8_str, size_t n_bytes);

/// Calculate length of next UTF-8 character, checking each byte.
/// If illegal byte is found, return `-1 - off`, where `off` is the offset to the byte that is illegal.
int zis_u8char_len_checked(const zis_char8_t *u8_str, size_t n_bytes);

/// Get number of UTF-8 characters.
size_t zis_u8str_len(const zis_char8_t *u8_str);

/// Check each byte and get number of UTF-8 characters.
size_t zis_u8str_len_s(const zis_char8_t *u8_str, size_t n_bytes);

/// Check each byte and get number of UTF-8 characters. If illegal byte is found,
/// return `-1 - off` where `off` is the offset to the byte that is illegal.
intptr_t zis_u8str_len_checked(const zis_char8_t *u8_str, size_t n_bytes);

/// Get the pointer to the `n_chars`-th UTF-8 character. If error occurs, return NULL.
/// If out of range, return the pointer to the ending NUL char.
zis_char8_t *zis_u8str_find_pos(const zis_char8_t *u8_str, size_t n_chars);

/// Find the last valid UTF-8 character. Returns the pointer to 1 byte past the last character.
/// Returns NULL if no valid character is found.
zis_char8_t *zis_u8str_find_end(const zis_char8_t *u8_str, size_t max_bytes);

/* ----- char and string info ----------------------------------------------- */

/// Get number of columns needed to display the given character.
size_t zis_char_width(zis_wchar_t c);

/// Convert a character to a number. `c` must be one of [0-9a-zA-Z].
/// Returns `-1` if the character `c` is not a valid digit.
unsigned int zis_char_digit(zis_wchar_t c);

unsigned int zis_char_digit_1(char c);
