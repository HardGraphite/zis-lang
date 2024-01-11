#include "streamobj.h"

#include "context.h"
#include "fsutil.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "strutil.h"

#include "exceptobj.h"
#include "pathobj.h"

/* ----- stream backend: file ----------------------------------------------- */

static void *sop_file_open(const zis_path_char_t *restrict path, int mode) {
    return zis_file_open(path, mode);
}

static const struct zis_stream_obj_operations sop_file = {
    .seek  = zis_file_seek,
    .read  = zis_file_read,
    .write = zis_file_write,
    .close = zis_file_close,
};

/* ----- stream object ------------------------------------------------------ */

static void stream_obj_zero(struct zis_stream_obj *self) {
    self->_ops = NULL;
    self->_ops_data = NULL;
    self->_flags = 0;
    self->_c_buf = NULL,
    self->_c_end = NULL;
    self->_c_cur = NULL;
    self->_b_cur = NULL;
}

struct zis_stream_obj *zis_stream_obj_new(struct zis_context *z) {
    struct zis_stream_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_HUGE, z->globals->type_Stream, 0, 0),
        struct zis_stream_obj
    );
    stream_obj_zero(self);
    return self;
}

void zis_stream_obj_bind(
    struct zis_stream_obj *self,
    const struct zis_stream_obj_operations *restrict ops,
    void *restrict ops_data,
    int flags
) {
    if (self->_ops)
        zis_stream_obj_close(self);

    self->_flags = flags;
    self->_ops = ops;
    self->_ops_data = ops_data;

    self->_b_end = zis_stream_obj_flag_readable(self) ? self->_b_buf : self->_b_buf + ZIS_STREAM_OBJ_BUF_SZ;
    self->_b_cur = self->_b_buf;

    if (flags & ZIS_STREAM_OBJ_TEXT) {
        if (flags & ZIS_STREAM_OBJ_UTF8) {
            self->_c_buf = self->_b_buf;
            self->_c_cur = self->_b_cur;
            self->_c_end = self->_b_end;
        } else {
            zis_context_panic(NULL, ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
        }
    }
}

struct zis_stream_obj *zis_stream_obj_new_file(
    struct zis_context *z,
    const zis_path_char_t *restrict file, int flags
) {
    struct zis_stream_obj *self = zis_stream_obj_new(z);
    void *data = sop_file_open(file, flags & ZIS_STREAM_OBJ_MODE_MASK);
    if (!data) {
        struct zis_path_obj *path_obj = zis_path_obj_new(z, file, (size_t)-1);
        struct zis_exception_obj *exc = zis_exception_obj_format(
            z, "sys", zis_object_from(path_obj),
            "cannot open this file"
        );
        z->callstack->frame[0] = zis_object_from(exc);
        return NULL;
    }
    zis_stream_obj_bind(self, &sop_file, data, flags);
    return self;
}

void zis_stream_obj_close(struct zis_stream_obj *self) {
    if (self->_ops)
        self->_ops->close(self);
    stream_obj_zero(self);
}

#define assert_stream_valid(obj) \
    (assert(                     \
        obj->_ops &&             \
        obj->_b_end >= obj->_b_buf && obj->_b_end <= &obj->_b_buf[ZIS_STREAM_OBJ_BUF_SZ] && \
        obj->_b_cur >= obj->_b_buf && obj->_b_cur <= obj->_b_end                        \
    ))

size_t zis_stream_obj_read_bytes(
    struct zis_stream_obj *restrict self,
    void *restrict buffer, size_t size
) {
    assert_stream_valid(self);
    assert(zis_stream_obj_flag_readable(self) && !zis_stream_obj_flag_text(self));

    const size_t rest_size = self->_b_end - self->_b_cur;
    if (rest_size >= size) {
        memcpy(buffer, self->_b_cur, size);
        self->_b_cur += size;
        return size;
    }

    if (rest_size) {
        memcpy(buffer, self->_b_cur, rest_size);
        buffer = (char *)buffer + rest_size;
        size -= rest_size;
        self->_b_cur = self->_b_end;
    }
    const size_t newly_read_size = self->_ops->read(self->_ops_data, buffer, size);
    if (zis_unlikely(newly_read_size == (size_t)-1))
        return rest_size ? rest_size : (size_t)-1;
    return rest_size + newly_read_size;
}

bool zis_stream_obj_write_bytes(
    struct zis_stream_obj *restrict self,
    const void *restrict data, size_t size
) {
    assert_stream_valid(self);
    assert(zis_stream_obj_flag_writeable(self) && !zis_stream_obj_flag_text(self));

    const size_t rest_size = self->_b_end - self->_b_cur;
    if (rest_size >= size) {
        memcpy(self->_b_cur, data, size);
        self->_b_cur += size;
        return true;
    }

    if (rest_size) {
        memcpy(self->_b_cur, data, rest_size);
        data = (const char *)data + rest_size;
        size -= rest_size;
        // self->_b_cur = self->_b_end;
    }
    if (self->_ops->write(self->_ops_data, self->_b_buf, ZIS_STREAM_OBJ_BUF_SZ) != 0)
        return false;
    self->_b_cur = self->_b_buf;
    if (self->_ops->write(self->_ops_data, data, size) != 0)
        return false;
    return true;
}

bool zis_stream_obj_flush_chars(struct zis_stream_obj *restrict self) {
    assert_stream_valid(self);
    assert(zis_stream_obj_flag_writeable(self) && zis_stream_obj_flag_text(self));

    if (zis_stream_obj_flag_utf8(self)) {
        assert(self->_b_buf == self->_c_buf && self->_b_end == self->_c_end);
        assert(self->_b_cur == self->_b_buf && self->_b_end == self->_b_buf + ZIS_STREAM_OBJ_BUF_SZ);
        const size_t size = (size_t)(self->_b_end - self->_c_cur);
        const int r = self->_ops->write(self->_ops_data, self->_b_buf, size);
        if (r != 0)
            return false;
        self->_b_cur = self->_b_buf;
        self->_c_cur = self->_b_buf;
        self->_c_end = self->_b_end;
    } else {
        zis_context_panic(NULL, ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
    }

    return true;
}

static int32_t _stream_obj_peek_char_slow_impl(
    struct zis_stream_obj *restrict self, size_t *char_len
) {
    assert_stream_valid(self);
    assert(zis_stream_obj_flag_readable(self) && zis_stream_obj_flag_text(self));

    if (self->_c_cur + 4 >= self->_c_end) {
        if (zis_stream_obj_flag_utf8(self)) {
            assert(self->_b_buf == self->_c_buf && self->_b_end == self->_c_end);
            assert(self->_c_cur <= self->_b_cur);
            const size_t rest_size = (size_t)(self->_b_end - self->_c_cur);
            if (rest_size)
                memmove(self->_b_buf, self->_c_cur, rest_size);
            const size_t n = self->_ops->read(
                self->_ops_data, self->_b_buf + rest_size,
                ZIS_STREAM_OBJ_BUF_SZ - rest_size
            );
            if (n == (size_t)-1)
                return -1;
            self->_b_cur = self->_b_buf;
            self->_b_end = self->_b_buf + rest_size + n;
            self->_c_cur = self->_b_buf;
            self->_c_end = self->_b_end;
        } else {
            zis_context_panic(NULL, ZIS_CONTEXT_PANIC_ABORT); // Not implemented.
        }
    }

    zis_wchar_t c;
    if (
        *self->_c_cur == '\r' && zis_stream_obj_flag_crlf(self) &&
        self->_c_cur + 1 < self->_c_end && self->_c_cur[1] == '\n'
    ) {
        self->_c_cur++;
    }
    const size_t n = zis_u8char_to_code(&c, (const zis_char8_t *)self->_c_cur, (const zis_char8_t *)self->_c_end);
    if (n == 0)
        return -1; // TODO: use a different status code from IO error.
    if (char_len)
        *char_len = n;
    return (int32_t)c;
}

int32_t _zis_stream_obj_peek_char_slow(struct zis_stream_obj *restrict self) {
    return _stream_obj_peek_char_slow_impl(self, NULL);
}

int32_t _zis_stream_obj_read_char_slow(struct zis_stream_obj *restrict self) {
    size_t n;
    const int32_t c = _stream_obj_peek_char_slow_impl(self, &n);
    self->_c_cur += n;
    assert(self->_c_cur <= self->_c_end);
    return c;
}

bool _zis_stream_obj_write_char_slow(struct zis_stream_obj *restrict self, int32_t c) {
    assert_stream_valid(self);
    assert(zis_stream_obj_flag_writeable(self) && zis_stream_obj_flag_text(self));

    if (self->_c_cur + 4 >= self->_c_end) {
        if (!zis_stream_obj_flush_chars(self))
            return false;
        assert(self->_c_cur == self->_c_buf);
    }

    if (c == '\n' && zis_stream_obj_flag_crlf(self)) {
        *self->_c_cur++ = '\r';
        *self->_c_cur++ = '\n';
        return true;
    }

    const size_t n = zis_u8char_from_code(c, (zis_char8_t *)self->_c_cur);
    if (zis_unlikely(n == 0))
        return false;
    self->_c_cur += n;
    return true;
}

ZIS_NATIVE_TYPE_DEF(
    Stream,
    struct zis_stream_obj, _bytes_size,
    NULL, NULL, NULL
);
