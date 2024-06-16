/// The `String` type.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "attributes.h"

struct zis_context;
struct zis_object;
struct zis_stream_obj;

typedef uint32_t zis_string_obj_wchar_t;

/// `String` object. Unicode strings.
struct zis_string_obj;

/// Create a `String` object from UTF-8 string.
/// Set size `n` to `-1` to take char NUL as the end of the string.
/// Return NULL if `s` is not a valid UTF-8 string.
struct zis_string_obj *zis_string_obj_new(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */
);

/// Create a `String` like `zis_string_obj_new()`, allowing escape sequence ("\\...").
/// The translator function shall return the translated character and update the `s_end` pointer;
/// or return -1 to report an error.
struct zis_string_obj *zis_string_obj_new_esc(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */,
    zis_string_obj_wchar_t (*escape_translator)(const char *restrict s, const char **restrict s_end)
);

struct zis_string_obj *_zis_string_obj_new_empty(struct zis_context *z);

/// Create a `String` object from a character (Unicode code point).
struct zis_string_obj *zis_string_obj_from_char(
    struct zis_context *z, zis_string_obj_wchar_t ch
);

/// Return the number of characters in the string.
size_t zis_string_obj_length(const struct zis_string_obj *self);

/// Copy `String` data to buffer `buf` as UTF-8 string and return the size (bytes).
/// Return -1 if the buffer is not big enough.
/// Set `buf` to NULL to get the minimum size of buffer.
zis_nodiscard size_t zis_string_obj_value(
    const struct zis_string_obj *self, char *buf, size_t buf_sz
);

/// Get the UTF-8 data. Returns NULL if the data is not stored in UTF-8 format.
const char *zis_string_obj_data_utf8(const struct zis_string_obj *self);

/// Concatenate two strings.
struct zis_string_obj *zis_string_obj_concat(
    struct zis_context *z,
    struct zis_string_obj *str1, struct zis_string_obj *str2
);

/// Compare two strings.
bool zis_string_obj_equals(struct zis_string_obj *lhs, struct zis_string_obj *rhs);

/// Compare two strings.
int zis_string_obj_compare(struct zis_string_obj *lhs, struct zis_string_obj *rhs);

/// Write the string to an output stream.
void zis_string_obj_write_to_stream(struct zis_string_obj *self, struct zis_stream_obj *stream);
