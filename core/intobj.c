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
#include "locals.h"
#include "memory.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"
#include "strutil.h"

#include "exceptobj.h"
#include "floatobj.h"
#include "stringobj.h"

// Use GCC's built-in arithmetic functions with overflow checking if possible
#define ZIS_USE_GNUC_OVERFLOW_ARITH 1

/* ----- big int arithmetics ------------------------------------------------ */

typedef uint32_t bigint_cell_t;
typedef uint64_t bigint_2cell_t;

#define BIGINT_CELL_MAX    UINT32_MAX
#define BIGINT_CELL_C(X)   UINT32_C(X)
#define BIGINT_CELL_WIDTH  32
#define BIGINT_2CELL_MAX    UINT64_MAX
#define BIGINT_2CELL_C(X)   UINT64_C(X)
#define BIGINT_2CELL_WIDTH  64

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
static bigint_cell_t bigint_self_div_1(
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

/// a_vec[a_len] <=> b_vec[b_len]. Returns <0 for <, 0 for =, >0 for >.
/// Assume that (a_vec[a_len - 1] != 0) and (b_vec[b_len - 1] != 0).
zis_nodiscard static int bigint_cmp(
    const bigint_cell_t *restrict a_vec, unsigned int a_len,
    const bigint_cell_t *restrict b_vec, unsigned int b_len
) {
    assert(a_len > 0 && b_len > 0);
    assert(a_vec[a_len - 1] != 0);
    assert(b_vec[b_len - 1] != 0);

    if (a_len != b_len)
        return a_len < b_len ? -1 : 1;

    unsigned int i = a_len;
    do {
        const bigint_cell_t a_i = a_vec[i], b_i = b_vec[i];
        if (a_i != b_i)
            return a_i < b_i ? -1 : 1;
    } while (--i);

    return 0;
}

/// y_vec[y_len] = a_vec[a_len] * b_vec[b_len].
/// Assume that (y_len > max(a_len, b_len)).
static void bigint_add(
    const bigint_cell_t *restrict a_vec, unsigned int a_len,
    const bigint_cell_t *restrict b_vec, unsigned int b_len,
    bigint_cell_t *restrict y_vec, unsigned int y_len
) {
    assert(y_len > a_len && y_len > b_len);

    bigint_cell_t carry = 0;
    for (unsigned int i = 0; i < y_len; i++) {
        bigint_cell_t a, b, s;
        a = i < a_len ? a_vec[i] : 0;
        b = i < b_len ? b_vec[i] : 0;
#if ZIS_USE_GNUC_OVERFLOW_ARITH && defined __GNUC__
        bigint_cell_t c1 = __builtin_add_overflow(a, b, &s);
        bigint_cell_t c2 = __builtin_add_overflow(s, carry, &s);
        carry = c1 | c2;
#else
        bigint_2cell_t s_and_c = (bigint_2cell_t)a + (bigint_2cell_t)b + carry;
        s = (bigint_cell_t)s_and_c;
        carry = (bigint_cell_t)(s_and_c >> BIGINT_CELL_WIDTH);
#endif
        y_vec[i] = s;
    }
    assert(!carry);
}

/// y_vec[y_len] = a_vec[a_len] - b_vec[b_len]. Returns whether negative.
/// Assume that (y_len >= max(a_len, b_len)) and (a_vec[a_len - 1] != 0) and (b_vec[b_len - 1] != 0).
zis_nodiscard static bool bigint_sub(
    const bigint_cell_t *restrict a_vec, unsigned int a_len,
    const bigint_cell_t *restrict b_vec, unsigned int b_len,
    bigint_cell_t *restrict y_vec, unsigned int y_len
) {
    assert(y_len >= a_len && y_len >= b_len);
    assert(a_len > 0 && b_len > 0);
    assert(a_vec[a_len - 1] != 0);
    assert(b_vec[b_len - 1] != 0);

    bool result_neg;
    {
        // Make sure a is not less than b.
        const int a_cmp_b = bigint_cmp(a_vec, a_len, b_vec, b_len);
        if (a_cmp_b > 0) {
            result_neg = false;
        } else if (a_cmp_b < 0) {
            const bigint_cell_t *tmp_vec = a_vec;
            unsigned int tmp_len = a_len;
            a_vec = b_vec;
            a_len = b_len;
            b_vec = tmp_vec;
            b_len = tmp_len;
            result_neg = true;
        } else {
            memset(y_vec, 0, y_len * sizeof y_vec[0]);
            return false;
        }
    }

    bigint_cell_t borrow = 0;
    for (unsigned int i = 0; i < y_len; i++) {
        bigint_cell_t a, b, s;
        a = i < a_len ? a_vec[i] : 0;
        b = i < b_len ? b_vec[i] : 0;
#if ZIS_USE_GNUC_OVERFLOW_ARITH && defined __GNUC__
        bigint_cell_t c1 = __builtin_sub_overflow(a, b, &s);
        bigint_cell_t c2 = __builtin_sub_overflow(s, borrow, &s);
        borrow = c1 | c2;
#else
        bigint_2cell_t s_and_b =
            (((bigint_2cell_t)BIGINT_CELL_MAX << BIGINT_CELL_WIDTH) | a) - b - borrow;
        s = (bigint_cell_t)s_and_b;
        borrow = BIGINT_CELL_MAX - (bigint_cell_t)(s_and_b >> BIGINT_CELL_WIDTH);
#endif
        y_vec[i] = s;
    }
    assert(!borrow);
    return result_neg;
}

zis_cold_fn zis_noinline static void _bigint_mul_unexpected_overflow(void) {
    // FIXME: Unfortunately it overflows. Don't know how to handle it right now.
    zis_context_panic(NULL, ZIS_CONTEXT_PANIC_IMPL);
}

/// y_vec[y_len] = a_vec[a_len] * b_vec[b_len].
/// Assume that (y_len >= a_len + b_len).
static void bigint_mul(
    const bigint_cell_t *restrict a_vec, unsigned int a_len,
    const bigint_cell_t *restrict b_vec, unsigned int b_len,
    bigint_cell_t *restrict y_vec, unsigned int y_len
) {
    assert(y_len >= a_len + b_len);

    /*
     *               a2    a1    a0
     *  x                  b1    b0
     *  ---------------------------
     *             a2b0  a1b0  a0b0
     *  +    a2b0  a1b0  a0b0
     *  ---------------------------
     *   y4    y3    y2    y1    y0
     */

    memset(y_vec, 0, sizeof y_vec[0] * y_len);

    for (unsigned int k = 0; k < b_len; k++) {
        const bigint_cell_t b = b_vec[k];
        bigint_cell_t carry = 0;
        for (unsigned int j = 0, i = k; j < a_len; j++, i++) {
            const bigint_cell_t a = a_vec[j];
            bigint_cell_t y = y_vec[i];
            const bigint_2cell_t yw1 = (bigint_2cell_t)a * (bigint_2cell_t)b;
            const bigint_2cell_t yw2 = (bigint_2cell_t)y + (bigint_2cell_t)carry;
            bigint_2cell_t yw;
            bool overflow;
#if ZIS_USE_GNUC_OVERFLOW_ARITH && defined __GNUC__
            overflow = __builtin_add_overflow(yw1, yw2, &yw);
#else
            yw = yw1 + yw2;
            overflow = yw2 > BIGINT_2CELL_MAX - yw1;
#endif
            if (overflow)
                _bigint_mul_unexpected_overflow();
            y = (bigint_cell_t)yw;
            carry  = (bigint_cell_t)(yw >> BIGINT_CELL_WIDTH);
            y_vec[i] = y;
        }
        if (carry) {
            const unsigned int i = k + a_len;
            assert(i < y_len);
            bigint_cell_t y = y_vec[i];
            bool overflow;
#if ZIS_USE_GNUC_OVERFLOW_ARITH && defined __GNUC__
            overflow = __builtin_add_overflow(carry, y, &y);
#else
            y += carry;
            overflow = carry > BIGINT_CELL_MAX - y;
#endif
            if (overflow)
                _bigint_mul_unexpected_overflow();
            y_vec[i] = y;
        }
    }
}

/* ----- int object --------------------------------------------------------- */

typedef uint16_t int_obj_cell_count_t;
#define INT_OBJ_CELL_COUNT_MAX  UINT16_MAX

struct zis_int_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size; // !
    int_obj_cell_count_t cell_count;
    bool     negative;
    bigint_cell_t cells[];
};

#define INT_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_int_obj, _bytes_size)

/// Dummy int object that can be allocated on stack for representing a small-int.
typedef union dummy_int_obj_for_smi {
    struct zis_int_obj int_obj;
    char _data[sizeof(struct zis_int_obj) + sizeof(zis_smallint_t)];
} dummy_int_obj_for_smi;

#define DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT \
    ((sizeof(dummy_int_obj_for_smi) - sizeof(struct zis_int_obj)) / sizeof(bigint_cell_t))

static_assert(DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT > 0, "");

/// Initialize a dummy int object on stack.
static void dummy_int_obj_for_smi_init(dummy_int_obj_for_smi *restrict di, zis_smallint_t val) {
    // See `zis_int_obj_or_smallint()`.

    const bool val_neg = val < 0;
    const uint64_t val_abs = val_neg ? (uint64_t)-val : (uint64_t)val;

    struct zis_int_obj *self = &di->int_obj;
    if (val_abs <= BIGINT_CELL_MAX) {
        static_assert(DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT >= 1, "");
        self->negative = val_neg;
        self->cells[0] = (bigint_cell_t)val_abs;
        self->cell_count = 1;
    } else {
        static_assert(sizeof(bigint_cell_t) * 2 == sizeof val_abs, "");
        static_assert(DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT >= 2, "");
        self->negative = val_neg;
        self->cells[0] = (bigint_cell_t)val_abs;
        self->cells[1] = (bigint_cell_t)(val_abs >> BIGINT_CELL_WIDTH);
        self->cell_count = 2;
    }
}

/// Allocate but do not initialize. If `cell_count` is too large, returns NULL.
struct zis_int_obj *int_obj_alloc(struct zis_context *z, size_t cell_count) {
    if (zis_unlikely(cell_count > INT_OBJ_CELL_COUNT_MAX))
        return NULL;

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

/// Trim leading zero cells. The object `x` itself may be modified.
/// Reallocate a new int object or use small-int if ther are too many unused cells.
static struct zis_object *int_obj_shrink(struct zis_context *z, struct zis_int_obj *x) {
    int_obj_cell_count_t cell_count = x->cell_count;
    bigint_cell_t *cells = x->cells;

    assert(cell_count > 0);
    while (!cells[cell_count - 1]) {
        if (cell_count == 1)
            return zis_smallint_to_ptr(0);
        cell_count--;
    }
    x->cell_count = cell_count;

    // use small-int if small enough
    if (cell_count == 1) {
#if ZIS_SMALLINT_MAX > BIGINT_CELL_MAX
        zis_smallint_t v = cells[0];
        if (x->negative)
            v = -v;
        return zis_smallint_to_ptr(v);
#else
        assert(sizeof(bigint_cell_t)  == sizeof(zis_smallint_t), "");
        bigint_cell_t c0 = cells[0];
        bool neg = x->negative;
        if (c0 <= (!neg ? (bigint_cell_t)ZIS_SMALLINT_MAX : -(bigint_cell_t)ZIS_SMALLINT_MIN)) {
            zis_smallint_t v = (zis_smallint_t)c0;
            if (x->negative)
                v = -v;
            return zis_smallint_to_ptr(v);
        }
#endif
    }
#if ZIS_SMALLINT_MAX > BIGINT_CELL_MAX
    else if (cell_count == 2) {
        static_assert(sizeof(bigint_cell_t) * 2 == sizeof(int64_t), "");
        uint64_t c01 = ((uint64_t)cells[1] << BIGINT_CELL_WIDTH) | (uint64_t)cells[0];
        bool neg = x->negative;
        if (c01 <= (!neg ? (bigint_cell_t)ZIS_SMALLINT_MAX : -(bigint_cell_t)ZIS_SMALLINT_MIN)) {
            zis_smallint_t v = (zis_smallint_t)c01;
            if (x->negative)
                v = -v;
            return zis_smallint_to_ptr(v);
        }
    }
#endif

    // too many unused cells
    if (int_obj_cells_capacity(x) - cell_count >= 4) {
        struct zis_int_obj *obj = int_obj_alloc(z, cell_count);
        assert(obj);
        obj->negative = x->negative;
        memcpy(obj->cells, x->cells, cell_count * sizeof(bigint_cell_t));
        return zis_object_from(obj);
    }

    // use the original
    return zis_object_from(x);
}

zis_noinline struct zis_object *zis_int_obj_or_smallint(
    struct zis_context *z, int64_t val
) {
    if (ZIS_SMALLINT_MIN <= val && val <= ZIS_SMALLINT_MAX)
        return zis_smallint_to_ptr((zis_smallint_t)val);

    const bool val_neg = val < 0;
    const uint64_t val_abs = val_neg ? (uint64_t)-val : (uint64_t)val;

    struct zis_int_obj *self;
    if (val_abs <= BIGINT_CELL_MAX) {
        self = int_obj_alloc(z, 1);
        assert(self);
        self->negative = val_neg;
        self->cells[0] = (bigint_cell_t)val_abs;
    } else {
        static_assert(sizeof(bigint_cell_t) * 2 == sizeof val_abs, "");
        self = int_obj_alloc(z, 2);
        assert(self);
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
    if (!digit_count) {
        *str_end_p = str;
        return NULL; // No valid character.
    }
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
        if (zis_unlikely(!self))
            return NULL; // Too large.
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
            self->cell_count * BIGINT_CELL_WIDTH
            - zis_bits_count_lz(self->cells[self->cell_count - 1]);
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
        if (p <= buf) {
            zis_mem_free(cell_dup);
            return (size_t)-1;
        }
        const bigint_cell_t r = bigint_self_div_1(cell_dup, cell_count, base);
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

/// Try to do simple small-int multiplication.
/// On failure, returns NULL, and `zis_int_obj_or_smallint_mul()` should be used instead.
struct zis_object *_smallint_try_mul(
    struct zis_context *z, zis_smallint_t lhs, zis_smallint_t rhs
) {
    bool overflow;
    int64_t result;
#if ZIS_USE_GNUC_OVERFLOW_ARITH && defined __GNUC__
    overflow = __builtin_mul_overflow(lhs, rhs, &result);
#else
    result = lhs * rhs;
    overflow = rhs && result / rhs != lhs;
#endif
    if (!overflow)
        return zis_int_obj_or_smallint(z, result);
    return NULL;
}

struct zis_object *zis_smallint_mul(
    struct zis_context *z, zis_smallint_t lhs, zis_smallint_t rhs
) {
    struct zis_object *res = _smallint_try_mul(z, lhs, rhs);
    if (res)
        return res;
    return zis_int_obj_or_smallint_mul(z, zis_smallint_to_ptr(lhs), zis_smallint_to_ptr(rhs));
}

/// Do small-int or int-obj addition or subtraction. Two operands cannot be both small-ints.
/// If the integer is too large, returns NULL.
static struct zis_object *_int_obj_or_smallint_add_or_sub_slow(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs,
    bool do_sub /* false = add, true = sub */
) {
    assert(!(zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)));

    dummy_int_obj_for_smi _dummy_int;
    zis_locals_decl(
        z, var,
        struct zis_int_obj *lhs_int_obj, *rhs_int_obj;
    );

    if (zis_object_is_smallint(lhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int, zis_smallint_from_ptr(lhs));
        var.lhs_int_obj = &_dummy_int.int_obj;
        var.rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    } else if (zis_object_is_smallint(rhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int, zis_smallint_from_ptr(rhs));
        var.lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
        var.rhs_int_obj = &_dummy_int.int_obj;
    } else {
        var.lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
        var.rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    }

    struct zis_int_obj *res_int_obj;
    const unsigned int lhs_rhs_max_cell_count =
        var.lhs_int_obj->cell_count >= var.rhs_int_obj->cell_count ?
        var.lhs_int_obj->cell_count : var.rhs_int_obj->cell_count;
    const bool lhs_rhs_sign_same =
        var.lhs_int_obj->negative == var.rhs_int_obj->negative;
    if (!do_sub ? lhs_rhs_sign_same : !lhs_rhs_sign_same) {
        res_int_obj = int_obj_alloc(z, lhs_rhs_max_cell_count + 1);
        if (zis_unlikely(!res_int_obj))
            goto too_large;
        res_int_obj->negative = var.lhs_int_obj->negative;
        bigint_add(
            var.lhs_int_obj->cells, var.lhs_int_obj->cell_count,
            var.rhs_int_obj->cells, var.rhs_int_obj->cell_count,
            res_int_obj->cells, res_int_obj->cell_count
        );
    } else {
        res_int_obj = int_obj_alloc(z, lhs_rhs_max_cell_count);
        if (zis_unlikely(!res_int_obj))
            goto too_large;
        const bool neg = bigint_sub(
            var.lhs_int_obj->cells, var.lhs_int_obj->cell_count,
            var.rhs_int_obj->cells, var.rhs_int_obj->cell_count,
            res_int_obj->cells, res_int_obj->cell_count
        );
        bool res_neg = var.lhs_int_obj->negative;
        if (neg)
            res_neg = !res_neg;
        res_int_obj->negative = res_neg;
    }

    zis_locals_drop(z, var);
    return int_obj_shrink(z, res_int_obj);

too_large:
    zis_locals_drop(z, var);
    return NULL;
}

struct zis_object *zis_int_obj_or_smallint_add(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        return zis_smallint_add(z, lhs_v, rhs_v);
    }

    return _int_obj_or_smallint_add_or_sub_slow(z, lhs, rhs, false);
}

struct zis_object *zis_int_obj_or_smallint_sub(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        return zis_smallint_sub(z, lhs_v, rhs_v);
    }

    return _int_obj_or_smallint_add_or_sub_slow(z, lhs, rhs, true);
}

struct zis_object *zis_int_obj_or_smallint_mul(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        struct zis_object *res = _smallint_try_mul(z, lhs_v, rhs_v);
        if (res)
            return res;
    }

    dummy_int_obj_for_smi _dummy_int_l, _dummy_int_r;
    zis_locals_decl(
        z, var,
        struct zis_int_obj *lhs_int_obj, *rhs_int_obj;
    );

    if (zis_object_is_smallint(lhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int_l, zis_smallint_from_ptr(lhs));
        var.lhs_int_obj = &_dummy_int_l.int_obj;
    } else {
        var.lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
    }
    if (zis_object_is_smallint(rhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int_r, zis_smallint_from_ptr(rhs));
        var.rhs_int_obj = &_dummy_int_r.int_obj;
    } else {
        var.rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    }

    struct zis_int_obj *res_int_obj =
        int_obj_alloc(z, var.lhs_int_obj->cell_count + var.rhs_int_obj->cell_count);
    if (zis_unlikely(!res_int_obj)) {
        zis_locals_drop(z, var);
        return NULL; // Too large.
    }
    res_int_obj->negative = var.lhs_int_obj->negative != var.rhs_int_obj->negative;
    bigint_mul(
        var.lhs_int_obj->cells, var.lhs_int_obj->cell_count,
        var.rhs_int_obj->cells, var.rhs_int_obj->cell_count,
        res_int_obj->cells, res_int_obj->cell_count
    );

    zis_locals_drop(z, var);
    return int_obj_shrink(z, res_int_obj);
}

struct zis_float_obj *zis_int_obj_or_smallint_div(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
) {
    double lhs_as_f, rhs_as_f;
    if (zis_object_is_smallint(rhs)) {
        const zis_smallint_t rhs_v = zis_smallint_from_ptr(rhs);
        rhs_as_f = (double)rhs_v;
    } else {
        assert(zis_object_type_is(rhs, z->globals->type_Int));
        rhs_as_f = zis_int_obj_value_f(zis_object_cast(rhs, struct zis_int_obj));
    }
    if (zis_object_is_smallint(lhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs);
        lhs_as_f = (double)lhs_v;
    } else {
        assert(zis_object_type_is(lhs, z->globals->type_Int));
        lhs_as_f = zis_int_obj_value_f(zis_object_cast(lhs, struct zis_int_obj));
    }
    // WARNING: division by zero is not specially handled.
    return zis_float_obj_new(z, lhs_as_f / rhs_as_f);
}

int zis_int_obj_or_smallint_compare(struct zis_object *lhs, struct zis_object *rhs) {
    if (lhs == rhs)
        return 0;
    if (zis_object_is_smallint(lhs)) {
        zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs);
        if (zis_object_is_smallint(rhs)) {
            zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs);
            assert(lhs_smi != rhs_smi);
            return lhs_smi < rhs_smi ? -1 : 1;
        } else {
            struct zis_int_obj *rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
            return rhs_int_obj->negative ? 1 : -1;
        }
    } else {
        struct zis_int_obj *lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
        if (zis_object_is_smallint(rhs)) {
            return lhs_int_obj->negative ? -1 : 1;
        } else {
            struct zis_int_obj *rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
            return bigint_cmp(lhs_int_obj->cells, lhs_int_obj->cell_count, rhs_int_obj->cells, rhs_int_obj->cell_count);
        }
    }
}

bool zis_int_obj_or_smallint_equals(struct zis_object *lhs, struct zis_object *rhs) {
    if (lhs == rhs)
        return true;
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs))
        return false;
    const struct zis_int_obj *lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
    const struct zis_int_obj *rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    if (lhs_int_obj->cell_count != rhs_int_obj->cell_count)
        return false;
    return memcmp(lhs_int_obj->cells, rhs_int_obj->cells, lhs_int_obj->cell_count * sizeof(bigint_cell_t)) == 0;
}

#define assert_arg1_smi_or_Int(__z) \
do {                                \
    struct zis_object *x = (__z)->callstack->frame[1]; \
    zis_unused_var(x);              \
    assert(zis_object_is_smallint(x) || zis_object_type_is(x, (__z)->globals->type_Int)); \
} while (0)

/// Convert an int-obj or small-int to double.
static double int_obj_or_smallint_to_double(struct zis_object *x) {
    if (zis_object_is_smallint(x))
        return (double)zis_smallint_from_ptr(x);
    else
        return zis_int_obj_value_f(zis_object_cast(x, struct zis_int_obj));
}

zis_cold_fn zis_noinline
static int int_obj_bin_op_unsupported_error(struct zis_context *z, const char *restrict op) {
    struct zis_object **frame = z->callstack->frame;
    frame[0] = zis_object_from(zis_exception_obj_format_common(
        z, ZIS_EXC_FMT_UNSUPPORTED_OPERATION_BIN,
        op, frame[1], frame[2]
    ));
    return ZIS_THR;
}

zis_cold_fn zis_noinline
static int int_obj_too_large_error(struct zis_context *z) {
    struct zis_object **frame = z->callstack->frame;
    frame[0] = zis_object_from(zis_exception_obj_format(
        z, "value", NULL, "the integer constant is too large"
    ));
    return ZIS_THR;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_pos, z, {1, 0, 1}) {
    /*#DOCSTR# func Int:\'+#'() :: Int
    Returns `+ self`. */
    assert_arg1_smi_or_Int(z);
    struct zis_object **frame = z->callstack->frame;
    frame[0] = frame[1];
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_neg, z, {1, 0, 1}) {
    /*#DOCSTR# func Int:\'-#'() :: Int
    Returns `- self`. */
    assert_arg1_smi_or_Int(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self = frame[1];
    struct zis_object *result;
    if (zis_object_is_smallint(self)) {
        const zis_smallint_t result_value = -zis_smallint_from_ptr(self);
        result = zis_smallint_try_to_ptr(result_value);
        if (!result) {
            result = zis_int_obj_or_smallint(z, result_value);
            assert(result);
        }
    } else {
        struct zis_int_obj *self_int_obj = zis_object_cast(self, struct zis_int_obj);
        struct zis_int_obj *res_int_obj = int_obj_alloc(z, self_int_obj->cell_count);
        assert(res_int_obj);
        res_int_obj->negative = !self_int_obj->negative;
        memcpy(res_int_obj->cells, self_int_obj->cells, self_int_obj->cell_count * sizeof(bigint_cell_t));
        result = zis_object_from(res_int_obj);
    }
    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_add, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'+'(other :: Int|Float) :: Int|Float
    Operator +. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_object *result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type || other_type == g->type_Int) {
        result = zis_int_obj_or_smallint_add(z, self_v, other_v);
        if (zis_unlikely(!result))
            return int_obj_too_large_error(z);
    } else if (other_type == g->type_Float) {
        result = zis_object_from(zis_float_obj_new(
            z,
            int_obj_or_smallint_to_double(self_v) +
                zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj))
        ));
    } else {
        return int_obj_bin_op_unsupported_error(z, "+");
    }
    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_sub, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'-'(other :: Int|Float) :: Int|Float
    Operator -. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_object *result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type || other_type == g->type_Int) {
        result = zis_int_obj_or_smallint_sub(z, self_v, other_v);
        if (zis_unlikely(!result))
            return int_obj_too_large_error(z);
    } else if (other_type == g->type_Float) {
        result = zis_object_from(zis_float_obj_new(
            z,
            int_obj_or_smallint_to_double(self_v) -
                zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj))
        ));
    } else {
        return int_obj_bin_op_unsupported_error(z, "-");
    }
    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_mul, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'*'(other :: Int|Float) :: Int|Float
    Operator *. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_object *result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type || other_type == g->type_Int) {
        result = zis_int_obj_or_smallint_mul(z, self_v, other_v);
        if (zis_unlikely(!result))
            return int_obj_too_large_error(z);
    } else if (other_type == g->type_Float) {
        result = zis_object_from(zis_float_obj_new(
            z,
            int_obj_or_smallint_to_double(self_v) *
                zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj))
        ));
    } else {
        return int_obj_bin_op_unsupported_error(z, "*");
    }
    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_div, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'/'(other :: Int|Float) :: Float
    Operator ==. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_float_obj *result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type || other_type == g->type_Int)
        result = zis_int_obj_or_smallint_div(z, self_v, other_v);
    else if (other_type == g->type_Float)
        result = zis_float_obj_new(
            z,
            int_obj_or_smallint_to_double(self_v) /
                zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj))
        );
    else
        return int_obj_bin_op_unsupported_error(z, "+");
    frame[0] = zis_object_from(result);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_equ, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'=='(other :: Int|Float) :: Bool
    Operator ==. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    if (frame[1] == frame[2]) {
        frame[0] = zis_object_from(g->val_true);
        return ZIS_OK;
    }

    bool result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (other_type == g->type_Int)
        result = zis_int_obj_or_smallint_equals(self_v, other_v);
    else if (other_type == g->type_Float)
        result =
            int_obj_or_smallint_to_double(self_v) ==
            zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj));
    else
        result = false;
    frame[0] = zis_object_from(result ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_cmp, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'<=>'(other :: Int|Float) :: int
    Operator <=>. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    int result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type || other_type == g->type_Int) {
        result = zis_int_obj_or_smallint_compare(self_v, other_v);
    } else if (other_type == g->type_Float) {
        double self_as_f = int_obj_or_smallint_to_double(self_v);
        double other_f = zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj));
        result = self_as_f == other_f ? 0 : self_as_f < other_f ? -1 : 1;
    } else {
        return int_obj_bin_op_unsupported_error(z, "<=>");
    }
    frame[0] = zis_smallint_to_ptr(result);
    return ZIS_OK;
}

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
    { "+#"          , &T_Int_M_operator_pos   },
    { "-#"          , &T_Int_M_operator_neg   },
    { "+"           , &T_Int_M_operator_add   },
    { "-"           , &T_Int_M_operator_sub   },
    { "*"           , &T_Int_M_operator_mul   },
    { "/"           , &T_Int_M_operator_div   },
    { "=="          , &T_Int_M_operator_equ   },
    { "<=>"         , &T_Int_M_operator_cmp   },
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
