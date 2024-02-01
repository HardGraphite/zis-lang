/// The `Stream` type.

#pragma once

#include <stdint.h>
#include <stdio.h> // BUFSIZ

#include "attributes.h"
#include "fsutil.h"
#include "object.h"

struct zis_context;
struct zis_string_obj;

/* ----- stream object ------------------------------------------------------ */

/// Stream buffer size.
#define ZIS_STREAM_OBJ_BUF_SZ BUFSIZ

/// The `Stream` object. A byte or text stream.
/// A stream is either read-only or write-only.
/// This object will not be moved by the GC system.
struct zis_stream_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    size_t _bytes_size;
    int _flags;
    const struct zis_stream_obj_operations *_ops;
    void *_ops_data;
    char *_c_buf, *_c_end, *_c_cur; // Characters: buffer, buffer-end, current.
    char *_b_end, *_b_cur; // Bytes (raw data): buffer-end, current.
    char _b_buf[ZIS_STREAM_OBJ_BUF_SZ];
};

/// Stream operation functions. See `zis_file_*()` functions.
struct zis_stream_obj_operations {
    intptr_t (*seek)(void *, intptr_t, int); ///< seek(self, offset, whence) -> position
    size_t (*read)(void *, char *restrict, size_t); ///< read(self, buffer, size) -> size
    int (*write)(void *, const char *restrict, size_t); ///< write(self, data, size)
    void (*close)(void *); ///< close(self)
};

#define ZIS_STREAM_OBJ_MODE_MASK  0x0f
#define ZIS_STREAM_OBJ_MODE_IN    ZIS_FILE_MODE_RD
#define ZIS_STREAM_OBJ_MODE_OUT   ZIS_FILE_MODE_WR
#define ZIS_STREAM_OBJ_MODE_APP   (ZIS_FILE_MODE_WR | ZIS_FILE_MODE_APP)

#define ZIS_STREAM_OBJ_TEXT       0x10  ///< Open stream in text mode. Binary otherwise.
#define ZIS_STREAM_OBJ_CRLF       0x20  ///< Use CRLF as the end of line. LF otherwise.
#define ZIS_STREAM_OBJ_UTF8       0x40  ///< The backend uses UTF-8 encoding.

/// Create an empty `Stream` object without a backend bound.
struct zis_stream_obj *zis_stream_obj_new(struct zis_context *z);

/// Bind the stream to a backend.
void zis_stream_obj_bind(
    struct zis_stream_obj *self,
    const struct zis_stream_obj_operations *restrict ops,
    void *restrict ops_data,
    int flags
);

/// Open a file. On failure, throws an exception (REG-0) and returns NULL.
struct zis_stream_obj *zis_stream_obj_new_file(
    struct zis_context *z,
    const zis_path_char_t *restrict file, int flags
);

/// Open a stream associated with a file.
struct zis_stream_obj *zis_stream_obj_new_file_native(
    struct zis_context *z,
    zis_file_handle_t file, int flags
);

/// Open a read-only stream for string reading. `string_size` can be -1.
struct zis_stream_obj *zis_stream_obj_new_str(
    struct zis_context *z,
    const char *restrict string, size_t string_size, bool static_string
);

/// Open a read-only stream for string object reading.
struct zis_stream_obj *zis_stream_obj_new_strob(
    struct zis_context *z, struct zis_string_obj *str_obj
);

/// Close a stream.
void zis_stream_obj_close(struct zis_stream_obj *self);

zis_static_force_inline bool zis_stream_obj_flag_readable(const struct zis_stream_obj *self) {
    return !(self->_flags & ZIS_STREAM_OBJ_MODE_OUT);
}

zis_static_force_inline bool zis_stream_obj_flag_writeable(const struct zis_stream_obj *self) {
    return self->_flags & ZIS_STREAM_OBJ_MODE_OUT;
}

zis_static_force_inline bool zis_stream_obj_flag_text(const struct zis_stream_obj *self) {
    return self->_flags & ZIS_STREAM_OBJ_TEXT;
}

zis_static_force_inline bool zis_stream_obj_flag_crlf(const struct zis_stream_obj *self) {
    return self->_flags & ZIS_STREAM_OBJ_CRLF;
}

zis_static_force_inline bool zis_stream_obj_flag_utf8(const struct zis_stream_obj *self) {
    return self->_flags & ZIS_STREAM_OBJ_UTF8;
}

/// Read bytes from the stream. No mode check.
/// Returns read size, or -1 on error.
size_t zis_stream_obj_read_bytes(
    struct zis_stream_obj *restrict self,
    void *restrict buffer, size_t size
);

/// Write bytes to the stream. No mode check.
/// Returns whether successful.
bool zis_stream_obj_write_bytes(
    struct zis_stream_obj *restrict self,
    const void *restrict data, size_t size
);

/// Write data from output buffer to the associated backend.
bool zis_stream_obj_flush_chars(struct zis_stream_obj *restrict self);

int32_t _zis_stream_obj_peek_char_slow(struct zis_stream_obj *restrict self);

/// Peek the next character (Unicode point) from the stream. No mode check.
/// Returns -1 on EOF.
zis_static_force_inline int32_t zis_stream_obj_peek_char(
    struct zis_stream_obj *restrict self
) {
    assert(self->_ops && zis_stream_obj_flag_readable(self) && zis_stream_obj_flag_text(self));
    assert(self->_c_cur <= self->_c_end);

    if (zis_likely(self->_c_cur < self->_c_end)) {
        const char c = *self->_c_cur;
        if (zis_likely(!(c & 0x80 || c == '\r')))
            return (int32_t)(uint32_t)(unsigned char)c;
    }

    return _zis_stream_obj_peek_char_slow(self);
}

int32_t _zis_stream_obj_read_char_slow(struct zis_stream_obj *restrict self);

/// Read a character (Unicode point) from the stream. No mode check.
/// Returns -1 on EOF.
zis_static_force_inline int32_t zis_stream_obj_read_char(
    struct zis_stream_obj *restrict self
) {
    assert(self->_ops && zis_stream_obj_flag_readable(self) && zis_stream_obj_flag_text(self));
    assert(self->_c_cur <= self->_c_end);

    if (zis_likely(self->_c_cur < self->_c_end)) {
        const char c = *self->_c_cur;
        if (zis_likely(!(c & 0x80 || (c == '\r' && zis_stream_obj_flag_crlf(self))))) {
            self->_c_cur++;
            return (int32_t)(uint32_t)(unsigned char)c;
        }
    }

    return _zis_stream_obj_read_char_slow(self);
}

bool _zis_stream_obj_write_char_slow(struct zis_stream_obj *restrict self, int32_t c);

/// Write a character (Unicode point) to the stream. No mode check.
/// Returns whether successful.
zis_static_force_inline bool zis_stream_obj_write_char(
    struct zis_stream_obj *restrict self, int32_t c
) {
    assert(self->_ops && zis_stream_obj_flag_writeable(self) && zis_stream_obj_flag_text(self));
    assert(self->_c_cur <= self->_c_end);
    assert(c >= 0);

    if (zis_likely(self->_c_cur < self->_c_end)) {
        if (zis_likely(!(c & 0x80 || (c == '\n' && zis_stream_obj_flag_crlf(self))))) {
            *self->_c_cur++ = (char)(unsigned char)(uint32_t)c;
            return true;
        }
    }

    return _zis_stream_obj_write_char_slow(self, c);
}

/// Read characters to the buffer until an end-of-line char (including) or end of buffer.
size_t zis_stream_obj_read_line(struct zis_stream_obj *restrict self, char *restrict buffer, size_t size);

/// Get or move the c-buffer pointer (_c_cur). Returns NULL if reaches the end of stream in the input mode.
char *zis_stream_obj_char_buf_ptr(struct zis_stream_obj *restrict self, size_t move_offset, size_t *restrict rest_size_p);
