#include "intobj.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "algorithm.h"
#include "bits.h"
#include "context.h"
#include "globals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "strutil.h"

#include "exceptobj.h"
#include "stringobj.h"

/* ----- big int arithmetics ------------------------------------------------ */

typedef uint32_t bigint_cell_t;
typedef uint64_t bigint_2cell_t;

#define BIGINT_CELL_MAX    UINT32_MAX
#define BIGINT_CELL_C(X)   UINT32_C((X))
#define BIGINT_CELL_WIDTH  32

static_assert(BIGINT_CELL_WIDTH == sizeof(bigint_cell_t) * 8, "");

/// a_v[a_len] = 0
static void bigint_zero(bigint_cell_t *restrict a_v, unsigned int a_len) {
    memset(a_v, 0, sizeof a_v[0] * a_len);
}

/// a_v[a_len] = a_v[a_len] * b + c ... carry
static bigint_cell_t bigint_self_mul_add_1(
    bigint_cell_t *restrict a_v, unsigned int a_len,
    bigint_cell_t b,
    bigint_cell_t c
) {
    bigint_cell_t carry = c;
    for (unsigned int i = 0; i < a_len; i++) {
        const bigint_2cell_t p = (bigint_2cell_t)a_v[i] * (bigint_2cell_t)b + carry;
        a_v[i] = (bigint_cell_t)p;
        carry  = (bigint_cell_t)(p >> BIGINT_CELL_WIDTH);
    }
    return carry;
}

/// a_v[a_len] = a_v[a_len] / b ... rem
static bigint_cell_t bigint_self_dev_1(
    bigint_cell_t *restrict a_v, unsigned int a_len,
    bigint_cell_t b
) {
    assert(b);
    bigint_cell_t rem = 0;
    for (unsigned int i = a_len; i > 0; i--) {
        const bigint_2cell_t a = a_v[i - 1] + ((bigint_2cell_t)rem << BIGINT_CELL_WIDTH);
        const bigint_cell_t q = (bigint_cell_t)(a / b);
        const bigint_cell_t r = (bigint_cell_t)(a % b);
        a_v[i - 1] = q;
        rem = r;
    }
    assert(rem < b);
    return rem;
}

/* ----- int object --------------------------------------------------------- */

struct zis_int_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size; // !
    uint16_t cell_count;
    bool     negative;
    bigint_cell_t cells[];
};

#define INT_OBJ_CELL_COUNT_MAX  UINT16_MAX

#define INT_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_int_obj, _bytes_size)

/// Allocate but do not initialize.
struct zis_int_obj *int_obj_alloc(struct zis_context *z, size_t cell_count) {
    assert(cell_count > 0);
    assert(cell_count <= INT_OBJ_CELL_COUNT_MAX);
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Int,
        0, INT_OBJ_BYTES_FIXED_SIZE + cell_count * sizeof(bigint_cell_t)
    );
    struct zis_int_obj *const self = zis_object_cast(obj, struct zis_int_obj);
    self->cell_count = (uint16_t)cell_count;
    return self;
}

/// Maximum number of cells in this object.
zis_unused_fn static size_t int_obj_cells_capacity(const struct zis_int_obj *self) {
    assert(self->_bytes_size >= INT_OBJ_BYTES_FIXED_SIZE);
    return (self->_bytes_size - INT_OBJ_BYTES_FIXED_SIZE) / sizeof(bigint_cell_t);
}

struct zis_object *zis_int_obj_or_smallint(
    struct zis_context *z, int64_t val
) {
    if (ZIS_SMALLINT_MIN <= val && val <= ZIS_SMALLINT_MAX)
        return zis_smallint_to_ptr((zis_smallint_t)val);

    const bool val_neg = val < 0;
    const uint64_t val_abs = val_neg ? (uint64_t)-val : (uint64_t)val;

    struct zis_int_obj *self;
    if (val_abs <= BIGINT_CELL_MAX) {
        self = int_obj_alloc(z, 1);
        self->negative = val_neg;
        self->cells[0] = (bigint_cell_t)val_abs;
    } else {
        static_assert(sizeof(bigint_cell_t) * 2 == sizeof val_abs, "");
        self = int_obj_alloc(z, 2);
        self->negative = val_neg;
        self->cells[0] = (bigint_cell_t)val_abs;
        self->cells[1] = (bigint_cell_t)(val_abs >> BIGINT_CELL_WIDTH);
    }
    return zis_object_from(self);
}

struct zis_object *zis_int_obj_or_smallint_s(
    struct zis_context *z,
    const char *restrict str, const char **restrict str_end_p,
    unsigned int base
) {
    assert(str && str_end_p && str <= *str_end_p);
    assert(2 <= base && base <= 36);

    bool negative = false;
    if (str < *str_end_p && str[0] == '-') {
        negative = true;
        str++;
    }

    const char *str_end = str;
    size_t digit_count = 0;
    for (const char *const str_end_max = *str_end_p; str_end < str_end_max; str_end++) {
        const char c = *str_end;
        if (zis_char_digit(c) < base)
            digit_count++;
        else if (c != '_')
            break;
    }
    if (!digit_count)
        return NULL;
    *str_end_p = str_end;
    const unsigned int num_width = (unsigned int)ceil((double)digit_count * log2(base));

    if (num_width < ZIS_SMALLINT_WIDTH) {
        zis_smallint_t num = 0;
        for (const char *p = str; p < str_end; p++) {
            const char c = *p;
            if (zis_unlikely(c == '_'))
                continue;
            num = num * base + zis_char_digit(c);
            assert(0 <= num && num <= ZIS_SMALLINT_MAX);
        }
        if (negative)
            num = -num;
        return zis_smallint_to_ptr(num);
    } else {
        const unsigned int cell_count =
            zis_round_up_to_n_pow2(BIGINT_CELL_WIDTH, num_width) / BIGINT_CELL_WIDTH;
        struct zis_int_obj *self = int_obj_alloc(z, cell_count);
        self->negative = negative;
        bigint_cell_t *cells = self->cells;
        bigint_zero(cells, cell_count);
        for (const char *p = str; p < str_end; p++) {
            const char c = *p;
            if (zis_unlikely(c == '_'))
                continue;
            const bigint_cell_t carry =
                bigint_self_mul_add_1(cells, cell_count, base, zis_char_digit(c));
            assert(!carry), zis_unused_var(carry);
        }
        return zis_object_from(self);
    }
}

bool zis_int_obj_sign(const struct zis_int_obj *self) {
    return self->negative;
}

int64_t zis_int_obj_value_i(const struct zis_int_obj *self) {
    const size_t cell_count = self->cell_count;
    assert(cell_count);
    if (cell_count == 1) {
        static_assert(sizeof(bigint_cell_t) < sizeof(int64_t), "");
        const int64_t v = self->cells[0];
        return self->negative ? -v : v;
    } else if (cell_count == 2) {
        static_assert(sizeof(bigint_cell_t) * 2 == sizeof(int64_t), "");
        if (self->cells[1] <= UINT32_MAX / 2) {
            const int64_t v = ((int64_t)self->cells[1] << BIGINT_CELL_WIDTH) | (int64_t)self->cells[0];
            return self->negative ? -v : v;
        }
    }
    errno = ERANGE;
    return INT64_MIN;
}

int64_t zis_int_obj_value_trunc(const struct zis_int_obj *self) {
    const size_t cell_count = self->cell_count;
    assert(cell_count);
    if (cell_count == 1) {
        static_assert(sizeof(bigint_cell_t) < sizeof(int64_t), "");
        const int64_t v = self->cells[0];
        return self->negative ? -v : v;
    } else {
        static_assert(sizeof(bigint_cell_t) * 2 == sizeof(int64_t), "");
        const int64_t v = ((int64_t)(self->cells[1] & 0x7fffffff) << BIGINT_CELL_WIDTH) | (int64_t)self->cells[0];
        return self->negative ? -v : v;
    }
}

double zis_int_obj_value_f(const struct zis_int_obj *self) {
    double val_flt = 0.0;
    const bigint_cell_t *const cells = self->cells;
    const double cell_max_p1 = (double)BIGINT_CELL_MAX + 1.0;
    for (unsigned int i = 0, n = self->cell_count; i < n; i++)
        val_flt = val_flt * cell_max_p1 + cells[i];
    if (self->negative)
        val_flt = -val_flt;
    return val_flt;
}

static const char digits_lower[] = "0123456789abcdefghijklmnopqrstuvwxyz";
static const char digits_upper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

size_t zis_int_obj_value_s(const struct zis_int_obj *self, char *restrict buf, size_t buf_sz, int _base) {
    const bool uppercase = _base < 0;
    const unsigned int base = (unsigned int)abs(_base);
    assert(2 <= base && base <= 36);
    assert(self->cell_count);
    assert(self->cells[self->cell_count - 1]);

    if (!buf) {
        const unsigned int num_width =
            self->cell_count - 1 +
            BIGINT_CELL_WIDTH - zis_bits_count_lz(self->cells[self->cell_count - 1]);
        assert(num_width);
        const unsigned int n_digits = (unsigned int)((double)num_width / log2(base)) + 1;
        assert(n_digits);
        return self->negative ? n_digits + 1 : n_digits;
    }

    const char *const digits = uppercase ? digits_upper : digits_lower;
    const unsigned int cell_count = self->cell_count;
    bigint_cell_t *cell_dup = zis_mem_alloc(sizeof(bigint_cell_t) * cell_count);
    memcpy(cell_dup, self->cells, sizeof(bigint_cell_t) * cell_count);
    char *p = buf + buf_sz;
    for (unsigned int reset_cell_count = cell_count; reset_cell_count; ) {
        if (p <= buf)
            return (size_t)-1;
        const bigint_cell_t r = bigint_self_dev_1(cell_dup, cell_count, base);
        assert(r <= 36);
        *--p = digits[r];
        while (reset_cell_count && !cell_dup[reset_cell_count - 1])
            reset_cell_count--;
    }
    zis_mem_free(cell_dup);
    if (self->negative) {
        if (p <= buf)
            return (size_t)-1;
        *--p = '-';
    }
    const size_t written_size = (size_t)(buf + buf_sz - p);
    if (p != buf)
        memmove(buf, p, written_size);
    return written_size;
}

size_t zis_smallint_to_str(zis_smallint_t i, char *restrict buf, size_t buf_sz, int _base) {
    const bool negative = i < 0;
    zis_smallint_unsigned_t num =
        negative ? (zis_smallint_unsigned_t)-i : (zis_smallint_unsigned_t)i;

    const bool uppercase = _base < 0;
    const unsigned int base = (unsigned int)abs(_base);
    assert(2 <= base && base <= 36);

    if (!num) {
        if (buf) {
            if (!buf_sz)
                return (size_t)-1;
            buf[0] = '0';
        }
        return 1;
    }

    if (!buf) {
        const unsigned int num_width = sizeof(zis_smallint_t) * 8 - zis_bits_count_lz(num);
        const unsigned int n_digits = (unsigned int)((double)num_width / log2(base)) + 1;
        assert(n_digits);
        return negative ? n_digits + 1 : n_digits;
    }

    const char *const digits = uppercase ? digits_upper : digits_lower;
    char *p = buf + buf_sz;
    while (num) {
        if (p <= buf)
            return (size_t)-1;
        const zis_smallint_unsigned_t q = num / base;
        const zis_smallint_unsigned_t r = num % base;
        num = q;
        assert(r <= 36);
        *--p = digits[r];
    }
    if (negative) {
        if (p <= buf)
            return (size_t)-1;
        *--p = '-';
    }
    const size_t written_size = (size_t)(buf + buf_sz - p);
    if (p != buf)
        memmove(buf, p, written_size);
    return written_size;
}

struct zis_object *zis_int_obj_add_x(struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        const zis_smallint_t res_v = lhs_v + rhs_v; // FIXME: overflow check.
        return zis_smallint_to_ptr(res_v);
    }
    zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL);
    zis_unused_var(lhs), zis_unused_var(rhs);
}

struct zis_object *zis_int_obj_mul_x(struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        const zis_smallint_t res_v = lhs_v * rhs_v; // FIXME: overflow check.
        return zis_smallint_to_ptr(res_v);
    }
    zis_context_panic(z, ZIS_CONTEXT_PANIC_IMPL);
    zis_unused_var(lhs), zis_unused_var(rhs);
}

#define assert_arg1_smi_or_Int(__z) \
do {                                \
    struct zis_object *x = (__z)->callstack->frame[1]; \
    zis_unused_var(x);              \
    assert(zis_object_is_smallint(x) || zis_object_type_is(x, (__z)->globals->type_Int)); \
} while (0)

ZIS_NATIVE_FUNC_DEF(T_Int_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Int:hash() :: Int
    Generates hash code. */
    assert_arg1_smi_or_Int(z);
    struct zis_object **frame = z->callstack->frame;
    if (zis_object_is_smallint(frame[1])) {
        frame[0] = frame[1];
    } else {
        struct zis_int_obj *v = zis_object_cast(frame[1], struct zis_int_obj);
        size_t h = zis_hash_bytes(v->cells, sizeof v->cells[0] * v->cell_count);
        frame[0] = zis_smallint_to_ptr((zis_smallint_t)h);
    }
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_to_string, z, {1, 1, 2}) {
    /*#DOCSTR# func Int:to_string(?fmt) :: String
    Returns "nil". */
    assert_arg1_smi_or_Int(z);
    struct zis_object **frame = z->callstack->frame;
    char light_buffer[80];
    char *str; size_t str_sz;
    if (zis_object_is_smallint(frame[1])) {
        const zis_smallint_t x = zis_smallint_from_ptr(frame[1]);
        size_t n = zis_smallint_to_str(x, light_buffer, sizeof light_buffer, 10);
        assert(n != (size_t)-1);
        str = light_buffer;
        str_sz = n;
    } else {
        struct zis_int_obj *v = zis_object_cast(frame[1], struct zis_int_obj);
        size_t n = zis_int_obj_value_s(v, NULL, 0, 10);
        str = n <= sizeof light_buffer ? light_buffer : zis_mem_alloc(n);
        str_sz = zis_int_obj_value_s(v, str, n, 10);
        assert(str_sz != (size_t)-1);
    }
    frame[0] = zis_object_from(zis_string_obj_new(z, str, str_sz));
    if (str != light_buffer)
        zis_mem_free(str);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_F_parse, z, {1, 1, 2}) {
    /*#DOCSTR# func Int.parse(s :: String, ?base :: Int) :: Int
    Converts a string representation of a integer to its value.

    Argument `s` is the string representation to parse. If `base` is omitted,
    prefixes "0b", "0o", or "0x" are accepatable in `s` to specify the base.
    Argument `base` is the base of the integer in range [2,36]. */
    struct zis_object **frame = z->callstack->frame;
    const char *str_begin, *str_end;
    unsigned int base;
    bool make_result_neg = false;
    if (zis_object_type_is(frame[1], z->globals->type_String)) {
        struct zis_string_obj *str = zis_object_cast(frame[1], struct zis_string_obj);
        str_begin = zis_string_obj_data_utf8(str);
        if (!str_begin) {
        error_bad_str:
            frame[0] = zis_object_from(zis_exception_obj_format(
                z, "value", frame[1], "invalid %s literal", "integer"
            ));
            return ZIS_THR;
        }
        str_end = str_begin + zis_string_obj_length(str);
    } else {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE, "s", frame[1]
        ));
        return ZIS_THR;
    }
    if (zis_object_is_smallint(frame[2])) {
        const zis_smallint_t smi = zis_smallint_from_ptr(frame[2]);
        if (smi < 2 || smi > 36)
            goto error_bad_base;
        base = (unsigned int)smi;
    } else if (frame[2] == zis_object_from(z->globals->val_nil)) {
        base = 10;
        if (str_end - str_begin >= 3) {
            if (*str_begin == '-') {
                make_result_neg = true;
                str_begin++;
            }
            if (str_begin[0] == '0') {
                switch (tolower(str_begin[1])) {
                case 'b':
                    base = 2;
                    break;
                case 'o':
                    base = 8;
                    break;
                case 'x':
                    base = 16;
                    break;
                default:
                    break;
                }
                if (base != 10)
                    str_begin += 2;
            }
        }
    } else {
    error_bad_base:
        frame[0] = zis_object_from(zis_exception_obj_format(
            z, "value", frame[2], "invalid base"
        ));
        return ZIS_THR;
    }
    const char *str_parse_end = str_end;
    struct zis_object *result = zis_int_obj_or_smallint_s(z, str_begin, &str_parse_end, base);
    if (!result || str_parse_end != str_end)
        goto error_bad_str;
    if (make_result_neg) {
        if (zis_object_is_smallint(result)) {
            result = zis_smallint_to_ptr(-zis_smallint_from_ptr(result));
        } else {
            assert(zis_object_type_is(result, z->globals->type_Int));
            struct zis_int_obj *v = zis_object_cast(result, struct zis_int_obj);
            v->negative = true;
        }
    }
    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_Int_D_methods,
    { "hash"        , &T_Int_M_hash           },
    { "to_string"   , &T_Int_M_to_string      },
);

ZIS_NATIVE_VAR_DEF_LIST(
    T_int_D_statics,
    { "parse"       , { '^', .F = &T_Int_F_parse    } },
);

ZIS_NATIVE_TYPE_DEF_XB(
    Int,
    struct zis_int_obj, _bytes_size,
    NULL, T_Int_D_methods, T_int_D_statics
);
