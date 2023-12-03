/// The `String` type.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "attributes.h"

struct zis_context;
struct zis_object;

typedef uint32_t zis_string_obj_wchar_t;

/// `String` object. Unicode strings.
struct zis_string_obj;

/// Create a `String` object from UTF-8 string.
/// Set size `n` to `-1` to take char NUL as the end of the string.
/// If `s` is not a valid UTF-8 string, return the offset where error occurs;
/// otherwise return -1.
zis_nodiscard size_t zis_string_obj_new(
    struct zis_context *z, struct zis_object **ret,
    const char *s, size_t n /* = -1 */
);

struct zis_string_obj *_zis_string_obj_new_empty(struct zis_context *z);

/// Create a `String` object from a character (Unicode code point).
void zis_string_obj_from_char(
    struct zis_context *z, struct zis_object **ret,
    zis_string_obj_wchar_t ch
);

/// Return the number of characters in the string.
size_t zis_string_obj_length(const struct zis_string_obj *self);

/// Copy `String` data to buffer `buf` as UTF-8 string and return the size (bytes).
/// Return -1 if the buffer is not big enough.
/// Set `buf` to NULL to get the minimum size of buffer.
zis_nodiscard size_t zis_string_obj_value(
    const struct zis_string_obj *self, char *buf, size_t buf_sz
);
