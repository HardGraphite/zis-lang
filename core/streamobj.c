#include "streamobj.h"

#include "context.h"
#include "fsutil.h"
#include "globals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "strutil.h"

#include "exceptobj.h"
#include "pathobj.h"
#include "stringobj.h"

/* ----- stream backend: none ----------------------------------------------- */

static intptr_t sop_none_seek(void *_data, intptr_t offset, int whence) {
    zis_unused_var(_data), zis_unused_var(offset), zis_unused_var(whence);
    return 0;
}

static size_t sop_none_read(void *_data, char *restrict buffer, size_t size) {
    zis_unused_var(_data), zis_unused_var(buffer), zis_unused_var(size);
    return 0;
}

static int sop_none_write(void *_data, const char *restrict data, size_t size) {
    zis_unused_var(_data), zis_unused_var(data), zis_unused_var(size);
    return 0;
}

static void sop_none_close(void *data) {
    zis_unused_var(data);
}

static const struct zis_stream_obj_operations sop_none = {
    .seek  = sop_none_seek,
    .read  = sop_none_read,
    .write = sop_none_write,
    .close = sop_none_close,
};

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

/* ----- stream backend: immutable string ----------------------------------- */

struct sop_str_state {
    const char *current;
    const char *data_ptr;
    const char *data_end;
    char        _data[];
};

static void *sop_str_alloc_state(size_t data_size) {
    struct sop_str_state *state =
        zis_mem_alloc(sizeof(struct sop_str_state) + data_size);
    state->data_ptr = state->_data;
    state->current  = state->data_ptr;
    state->data_end = state->_data + data_size;
    return state;
}

static void sop_str_use_state_for_static_str(
    struct sop_str_state *restrict state, const char *restrict str, size_t sz
) {
    state->data_ptr = str;
    state->current  = str;
    state->data_end = str + sz;
}

static intptr_t sop_str_seek(void *_data, intptr_t offset, int whence) {
    struct sop_str_state *const restrict state = _data;
    const char *new_cur;
    switch (whence) {
    case SEEK_SET:
        new_cur = state->data_ptr;
        break;
    case SEEK_CUR:
        new_cur = state->current;
        break;
    case SEEK_END:
        new_cur = state->data_end;
        break;
    default:
        return 0;
    }
    new_cur += offset;
    if (new_cur < state->data_ptr)
        new_cur = state->data_ptr;
    else if (new_cur > state->data_end)
        new_cur = state->data_end;
    state->current = new_cur;
    return new_cur - state->data_ptr;
}

static size_t sop_str_read(void *_data, char *restrict buffer, size_t size) {
    struct sop_str_state *const state = _data;
    const size_t rest_size = (size_t)(state->data_end - state->current);
    if (rest_size < size)
        size = rest_size;
    memcpy(buffer, state->current, size);
    state->current += size;
    return size;
}

static void sop_str_close(void *_data) {
    struct sop_str_state *const state = _data;
    zis_mem_free(state);
}

static const struct zis_stream_obj_operations sop_str = {
    .seek  = sop_str_seek,
    .read  = sop_str_read,
    .write = NULL,
    .close = sop_str_close,
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
        zis_objmem_alloc_ex(z, ZIS_OBJMEM_ALLOC_NOMV, z->globals->type_Stream, 0, 0),
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
            zis_context_panic(NULL, ZIS_CONTEXT_PANIC_IMPL);
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
        zis_context_set_reg0(z, zis_object_from(exc));
        return NULL;
    }
    zis_stream_obj_bind(self, &sop_file, data, flags);
    return self;
}

struct zis_stream_obj *zis_stream_obj_new_file_native(
    struct zis_context *z,
    zis_file_handle_t file, int flags
) {
    struct zis_stream_obj *self = zis_stream_obj_new(z);
    zis_stream_obj_bind(self, &sop_file, file, flags);
    return self;
}

struct zis_stream_obj *zis_stream_obj_new_str(
    struct zis_context *z,
    const char *restrict string, size_t string_size, bool static_string
) {
    if (string_size == (size_t)-1)
        string_size = strlen(string);
    struct zis_stream_obj *self = zis_stream_obj_new(z);
    const int flags = ZIS_STREAM_OBJ_MODE_IN | ZIS_STREAM_OBJ_TEXT | ZIS_STREAM_OBJ_UTF8;
    if (string_size <= ZIS_STREAM_OBJ_BUF_SZ) {
        struct sop_str_state dummy_state;
        sop_str_use_state_for_static_str(&dummy_state, string, string_size);
        zis_stream_obj_bind(self, &sop_str, &dummy_state, flags);
        zis_stream_obj_peek_char(self);
        self->_ops = &sop_none, self->_ops_data = NULL;
    } else {
        struct sop_str_state *state;
        if (static_string) {
            state = sop_str_alloc_state(0);
            sop_str_use_state_for_static_str(state, string, string_size);
        } else {
            state = sop_str_alloc_state(string_size);
            memcpy(state->_data, string, string_size);
        }
        zis_stream_obj_bind(self, &sop_str, state, flags);
    }
    return self;
}

struct zis_stream_obj *zis_stream_obj_new_strob(
    struct zis_context *z, struct zis_string_obj *str_obj
) {
    const size_t data_size = zis_string_obj_value(str_obj, NULL, 0);
    struct sop_str_state *state = sop_str_alloc_state(data_size);
    const size_t n = zis_string_obj_value(str_obj, state->_data, data_size);
    assert(n == data_size), zis_unused_var(n);
    struct zis_stream_obj *self = zis_stream_obj_new(z);
    const int flags = ZIS_STREAM_OBJ_MODE_IN | ZIS_STREAM_OBJ_TEXT | ZIS_STREAM_OBJ_UTF8;
    zis_stream_obj_bind(self, &sop_str, state, flags);
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
        const size_t size = (size_t)(self->_c_cur - self->_c_buf);
        const int r = self->_ops->write(self->_ops_data, self->_c_buf, size);
        if (r != 0)
            return false;
        self->_c_cur = self->_c_buf;
    } else {
        zis_context_panic(NULL, ZIS_CONTEXT_PANIC_IMPL);
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
            if (n == (size_t)-1) {
                if (char_len)
                    *char_len = 0;
                return -1;
            }
            self->_b_end = self->_b_buf + rest_size + n;
            self->_b_cur = self->_b_end;
            self->_c_cur = self->_b_buf;
            self->_c_end = self->_b_end;
        } else {
            zis_context_panic(NULL, ZIS_CONTEXT_PANIC_IMPL);
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
    if (char_len)
        *char_len = n;
    if (n == 0)
        return -1; // TODO: use a different status code from IO error.
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

size_t zis_stream_obj_read_line(
    struct zis_stream_obj *restrict self, char *restrict buffer, size_t size
) {
    // TODO: read the buffer directly instead of reading characters one by one.

    assert(size >= 4);
    size_t i = 0;
    while (i < size - 3) {
        const int32_t c = zis_stream_obj_read_char(self);
        if (!(c & 0x80)) {
            buffer[i] = (char)c;
            i++;
            if (zis_unlikely(c == '\n'))
                break;
        } else {
            if (zis_unlikely(c == -1))
                break;
            zis_char8_t b[4];
            const size_t n = zis_u8char_from_code(c, b);
            assert(n);
            memcpy(buffer, b, n);
            i += n;
        }
    }
    return i;
}

bool zis_stream_obj_write_chars(
    struct zis_stream_obj *restrict self, const char *restrict _str, size_t size
) {
    // TODO: write to the buffer directly instead of one by one.

    for (
        const zis_char8_t *str = (const zis_char8_t *)_str, *const str_end = str + size;
        str < str_end;
    ) {
        zis_wchar_t c;
        const size_t n = zis_u8char_to_code(&c, str, str_end);
        if (zis_unlikely(!n))
            return false;
        str += n;
        if (zis_unlikely(!zis_stream_obj_write_char(self, c)))
            return false;
    }
    return true;
}

char *zis_stream_obj_char_buf_ptr(
    struct zis_stream_obj *restrict self,
    size_t move_offset, size_t *restrict rest_size_p
) {
    assert_stream_valid(self);
    assert(zis_stream_obj_flag_text(self));
    if (self->_c_end == self->_c_cur) {
        if (zis_stream_obj_flag_readable(self)) {
            if (zis_stream_obj_peek_char(self) == -1)
                return NULL;
        } else {
            assert(zis_stream_obj_flag_writeable(self));
            zis_stream_obj_flush_chars(self);
        }
    }
    size_t rest_size = (size_t)(self->_c_end - self->_c_cur);
    assert(rest_size);
    if (move_offset) {
        if (move_offset > rest_size)
            zis_context_panic(NULL, ZIS_CONTEXT_PANIC_ABORT);
        self->_c_cur += move_offset;
        rest_size -= move_offset;
    }
    if (rest_size_p)
        *rest_size_p = rest_size;
    return self->_c_cur;
}

ZIS_NATIVE_TYPE_DEF(
    Stream,
    struct zis_stream_obj, _bytes_size,
    NULL, NULL, NULL
);
