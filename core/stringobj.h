/// The `String` type.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "attributes.h"
#include "objvec.h" // struct zis_object_vec_view

struct zis_context;
struct zis_object;
struct zis_stream_obj;

typedef uint32_t zis_string_obj_wchar_t;

/* ----- string ------------------------------------------------------------- */

/// `String` object. Unicode strings.
struct zis_string_obj;

/// Create a `String` object from UTF-8 string.
/// Set size `n` to `-1` to take char NUL as the end of the string.
/// Return NULL if an error occurs.
struct zis_string_obj *zis_string_obj_new(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */
);

/// Create a `String` like `zis_string_obj_new()`, allowing escape sequences.
/// The translator function shall return the translated character and update the `s_end` pointer;
/// or return -1 to report an error.
/// Return NULL if an error occurs (exception in REG-0).
struct zis_string_obj *zis_string_obj_new_esc(
    struct zis_context *z,
    const char *s, size_t n /* = -1 */,
    char escape_beginning,
    zis_string_obj_wchar_t (*escape_translator)(const char *restrict s, const char **restrict s_end)
);

struct zis_string_obj *_zis_string_obj_new_empty(struct zis_context *z);

/// Create a `String` object from a character (Unicode code point).
/// Return NULL if an error occurs (exception in REG-0).
struct zis_string_obj *zis_string_obj_from_char(
    struct zis_context *z, zis_string_obj_wchar_t ch
);

/// Return the number of characters in the string.
size_t zis_string_obj_length(const struct zis_string_obj *self);

/// Get character at the specified position. Returns `-1` if the index is out of range.
zis_string_obj_wchar_t zis_string_obj_get(const struct zis_string_obj *str, size_t index);

/// Get substring. Returns NULL if the index or length is invalid.
struct zis_string_obj *zis_string_obj_slice(
    struct zis_context *z,
    const struct zis_string_obj *str, size_t begin_index, size_t length
);

/// Copy `String` data to buffer `buf` as UTF-8 string and return the size (bytes).
/// Return -1 if the buffer is not big enough.
/// Set `buf` to NULL to get the minimum size of buffer.
zis_nodiscard size_t zis_string_obj_to_u8str(
    const struct zis_string_obj *self, char *buf, size_t buf_sz
);

/// Try to get the raw data in ASCII (UTF-8) format. `len` is optional.
/// Returns NULL if the data is not stored as ASCII.
const char *zis_string_obj_as_ascii(const struct zis_string_obj *self, size_t *len /* = NULL */);

/// Concatenate strings and characters, using the specified separator between them.
/// The `separator` is optional.
/// Return NULL if an error occurs (exception in REG-0).
struct zis_string_obj *zis_string_obj_join(
    struct zis_context *z,
    struct zis_string_obj *separator /*=NULL*/, struct zis_object_vec_view items
);

/// Concatenate strings and characters.
/// Equivalent to `zis_string_obj_join(NULL, items)`.
/// Return NULL if an error occurs (exception in REG-0).
struct zis_string_obj *zis_string_obj_concat(
    struct zis_context *z,
    struct zis_object_vec_view items
);

/// Concatenate two strings.
/// Return NULL if an error occurs (exception in REG-0).
struct zis_string_obj *zis_string_obj_concat2(
    struct zis_context *z,
    struct zis_string_obj *str1, struct zis_string_obj *str2
);

/// Compare two strings.
bool zis_string_obj_equals(struct zis_string_obj *lhs, struct zis_string_obj *rhs);

/// Compare two strings.
int zis_string_obj_compare(struct zis_string_obj *lhs, struct zis_string_obj *rhs);

/// Write the string to an output stream.
void zis_string_obj_write_to_stream(struct zis_string_obj *self, struct zis_stream_obj *stream);

/* ----- string builder ----------------------------------------------------- */

/// `String.Builder` object.
struct zis_string_builder_obj;

/// Create a string builder.
struct zis_string_builder_obj *zis_string_builder_obj_new(struct zis_context *z);

/// Append a string to the string builder.
void zis_string_builder_obj_append(
    struct zis_context *z,
    struct zis_string_builder_obj *sb, struct zis_string_obj *s
);

/// Append a charater to the string builder.
bool zis_string_builder_obj_append_char(
    struct zis_context *z,
    struct zis_string_builder_obj *sb, zis_string_obj_wchar_t c
);

/// Get the result as a string.
struct zis_string_obj *zis_string_builder_obj_string(
    struct zis_context *z, struct zis_string_builder_obj *sb
);

/// Remove all characters.
void zis_string_builder_obj_clear(struct zis_string_builder_obj *sb);
