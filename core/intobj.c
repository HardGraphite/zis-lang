#include "intobj.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
#include "tupleobj.h"

#include "zis_config.h" // ZIS_USE_GNUC_OVERFLOW_ARITH

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

/// dst_v[len] = src_v[len]
static void bigint_copy(
    bigint_cell_t *restrict dst_v, const bigint_cell_t *restrict src_v, unsigned int len
) {
    memcpy(dst_v, src_v, sizeof src_v[0] * len);
}

/// Number of used bits, aka bit width.
static unsigned int bigint_width(const bigint_cell_t *restrict a_v, unsigned int a_len) {
    assert(a_len && a_v[a_len - 1]);
    return a_len * BIGINT_CELL_WIDTH - zis_bits_count_lz(a_v[a_len - 1]);
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

    for (unsigned int i = a_len - 1; i != (unsigned int)-1; i--) {
        const bigint_cell_t a_i = a_vec[i], b_i = b_vec[i];
        if (a_i != b_i)
            return a_i < b_i ? -1 : 1;
    }

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
#if ZIS_USE_GNUC_OVERFLOW_ARITH
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
            bigint_zero(y_vec, y_len);
            return false;
        }
    }

    bigint_cell_t borrow = 0;
    for (unsigned int i = 0; i < y_len; i++) {
        bigint_cell_t a, b, s;
        a = i < a_len ? a_vec[i] : 0;
        b = i < b_len ? b_vec[i] : 0;
#if ZIS_USE_GNUC_OVERFLOW_ARITH
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

/// a_vec[a_len] -= b_vec[b_len]. The result must be positive.
/// Assume that a >= b.
static void bigint_self_sub(
    bigint_cell_t *restrict a_vec, unsigned int a_len,
    const bigint_cell_t *restrict b_vec, unsigned int b_len
) {
    assert(a_len > 0 && b_len > 0);
    assert((a_len > b_len) || (a_len == b_len && a_vec[a_len - 1] >= b_vec[b_len - 1]));

    // Adapted from `bigint_sub()`.
    bigint_cell_t borrow = 0;
    for (unsigned int i = 0; i < a_len; i++) {
        bigint_cell_t a, b, s;
        a = a_vec[i];
        b = i < b_len ? b_vec[i] : 0;
#if ZIS_USE_GNUC_OVERFLOW_ARITH
        bigint_cell_t c1 = __builtin_sub_overflow(a, b, &s);
        bigint_cell_t c2 = __builtin_sub_overflow(s, borrow, &s);
        borrow = c1 | c2;
#else
        bigint_2cell_t s_and_b =
            (((bigint_2cell_t)BIGINT_CELL_MAX << BIGINT_CELL_WIDTH) | a) - b - borrow;
        s = (bigint_cell_t)s_and_b;
        borrow = BIGINT_CELL_MAX - (bigint_cell_t)(s_and_b >> BIGINT_CELL_WIDTH);
#endif
        a_vec[i] = s;
    }
    assert(!borrow);
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

    bigint_zero(y_vec, y_len);

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
#if ZIS_USE_GNUC_OVERFLOW_ARITH
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
#if ZIS_USE_GNUC_OVERFLOW_ARITH
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

#define _bigint_div_trim_leading_zeros(__vec, __len) \
do { \
    assert((__len)); \
    if (zis_likely((__vec)[(__len) - 1])) \
        break; \
    (__len)--; \
} while(0)

static void bigint_shl(const bigint_cell_t *restrict, unsigned int, unsigned int, bigint_cell_t *restrict, unsigned int);

/// q_vec[a_len] = a_vec[a_len] / b_vec[b_len] ... r_vec[a_len] .
/// Assume that b is not zero.
/// The lengths of `t_vec`, `q_vec`, and `r_vec` must not be less than `a_len`.
static void bigint_div(
    const bigint_cell_t *restrict a_vec, unsigned int a_len, // numerator
    const bigint_cell_t *restrict b_vec, unsigned int b_len, // denominator
    bigint_cell_t *restrict t_vec, // temporary
    bigint_cell_t *restrict q_vec, // quotient
    bigint_cell_t *restrict r_vec  // remainder
) {
    const unsigned int t_len = a_len, q_len = a_len, r_len = a_len;

    assert(a_len > 0 && b_len > 0);
    assert(!(b_len == 1 && b_vec[0] == 0)); // b != 0

    if (a_len < b_len) {
        bigint_copy(r_vec, a_vec, a_len);
        return;
    }

    /*
     *                   q1    q0
     *         -------------------
     *  b1  b0 )   a2    a1    a0
     *           b1q1  b0q1
     *          ------------------
     *            ..    ..     a0
     *                 b1q1  b0q1
     *          ------------------
     *                   r1    r0
     */

    bigint_zero(q_vec, q_len);
    bigint_copy(r_vec, a_vec, a_len);

    const unsigned int b_vec_width = bigint_width(b_vec, b_len);

    for (unsigned int i = a_len - b_len; i != (unsigned int)-1; i--) {
        bigint_cell_t q = 0;

        bigint_cell_t *const r_vec_x = r_vec + i;
        unsigned int r_len_x = r_len - i;
        _bigint_div_trim_leading_zeros(r_vec_x, r_len_x);

        while (true) {
            const unsigned int r_vec_x_width = bigint_width(r_vec_x, r_len_x);
            if (r_vec_x_width <= b_vec_width)
                break;

            unsigned int t_shift = r_vec_x_width - b_vec_width;
            bigint_shl(b_vec, b_len, t_shift, t_vec, t_len);
            if (bigint_cmp(r_vec_x, r_len_x, t_vec, t_len) < 0) {
                t_shift--;
                bigint_shl(b_vec, b_len, t_shift, t_vec, t_len);
            }

            bigint_self_sub(r_vec_x, r_len_x, t_vec, t_len);
            _bigint_div_trim_leading_zeros(r_vec_x, r_len_x);
            assert(t_shift < sizeof q * 8);
            assert(!(q & (BIGINT_CELL_C(1) << t_shift)));
            q |= BIGINT_CELL_C(1) << t_shift;
        }

        while (bigint_cmp(r_vec_x, r_len_x, b_vec, b_len) >= 0) {
            bigint_self_sub(r_vec_x, r_len_x, b_vec, b_len);
            _bigint_div_trim_leading_zeros(r_vec_x, r_len_x);
            assert(q != BIGINT_CELL_MAX);
            q++;
        }

        q_vec[i] = q;
    }
}

/// Compute two's complement of a_vec.
static void bigint_complement(
    const bigint_cell_t *a_vec, unsigned int a_len,
    bigint_cell_t *y_vec
) {
    bigint_2cell_t carry = 1;
    for (unsigned int i = 0; i < a_len; ++i) {
        carry += ~a_vec[i];
        y_vec[i] = (bigint_cell_t)carry;
        carry >>= BIGINT_CELL_WIDTH;
    }
    assert(!carry);
}

/// y_vec[y_len] = a_vec[a_len] << n.
/// Assume that y_len is big enough.
static void bigint_shl(
    const bigint_cell_t *restrict a_vec, unsigned int a_len,
    unsigned int n,
    bigint_cell_t *restrict y_vec, unsigned int y_len
) {
    assert(y_vec != a_vec);

    const unsigned int cell_offset = n / BIGINT_CELL_WIDTH;
    const unsigned int bit_offset  = n % BIGINT_CELL_WIDTH;

    unsigned int y_len_min;
    if (!bit_offset) {
        memcpy(y_vec + cell_offset, a_vec, a_len * sizeof y_vec[0]);
        y_len_min = a_len + cell_offset;
    } else {
        bigint_cell_t carry = 0;
        for (unsigned int i = 0; i < a_len; i++) {
            const bigint_2cell_t s = (bigint_2cell_t)a_vec[i] << bit_offset;
            y_vec[i + cell_offset] = (bigint_cell_t)s | carry;
            carry = (bigint_cell_t)(s >> BIGINT_CELL_WIDTH);
        }
        y_len_min = a_len + cell_offset;
        if (carry) {
            y_vec[a_len + cell_offset] = carry;
            y_len_min++;
        }
    }

    assert(y_len >= y_len_min);
    bigint_zero(y_vec, cell_offset);
    bigint_zero(y_vec + y_len_min, y_len - y_len_min);
}

/// y_vec[y_len] = a_vec[a_len] >> n.
/// Assume that y_len is big enough.
static void bigint_shr(
    const bigint_cell_t *restrict a_vec, unsigned int a_len,
    unsigned int n,
    bigint_cell_t *restrict y_vec, unsigned int y_len
) {
    const unsigned int cell_offset = n / BIGINT_CELL_WIDTH;
    const unsigned int bit_offset  = n % BIGINT_CELL_WIDTH;

    if (cell_offset >= a_len) {
        bigint_zero(y_vec, y_len);
        return;
    }

    unsigned int y_len_min = a_len - cell_offset;
    if (!bit_offset) {
        memmove(y_vec, a_vec + cell_offset, y_len_min * sizeof y_vec[0]);
    } else {
        bigint_cell_t carry = 0;
        unsigned int i = a_len - 1;
        if (!(a_vec[i] >> bit_offset)) {
            carry = a_vec[i] << (BIGINT_CELL_WIDTH - bit_offset);
            assert(y_len_min > 0 && i > 0);
            y_len_min--;
            i--;
        }
        for (; i != cell_offset - 1; i--) {
            const bigint_2cell_t s = (bigint_2cell_t)a_vec[i] << (BIGINT_CELL_WIDTH - bit_offset);
            y_vec[i - cell_offset] = (bigint_cell_t)(s >> BIGINT_CELL_WIDTH) | carry;
            carry = (bigint_cell_t)s;
        }
    }

    assert(y_len >= y_len_min);
    bigint_zero(y_vec + y_len_min, y_len - y_len_min);
}

/// Truncate bigint to n-bits. Assume that y_len is big enough.
static void bigint_trunc(
    const bigint_cell_t *restrict a_vec,
    unsigned int n,
    bigint_cell_t *restrict y_vec, unsigned int y_len
) {
    const unsigned int cell_count = n / BIGINT_CELL_WIDTH;
    const unsigned int bit_count  = n - cell_count;
    assert(y_len >= cell_count + (bit_count ? 1 : 0));
    if (bit_count) {
        const unsigned int copy_count = cell_count + 1;
        bigint_copy(y_vec, a_vec, copy_count);
        bigint_zero(y_vec + copy_count, y_len - copy_count);
        y_vec[cell_count] &= BIGINT_CELL_MAX >> (BIGINT_CELL_WIDTH - bit_count);
    } else {
        bigint_copy(y_vec, a_vec, cell_count);
        bigint_zero(y_vec + cell_count, y_len - cell_count);
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

#define assert_obj_is_int_or_dummy_int(z, obj_ptr) \
    (assert(!zis_object_is_smallint((obj_ptr)) && (zis_object_type((obj_ptr)) == NULL || zis_object_type((obj_ptr)) == (z)->globals->type_Int)))

#define assert_obj_is_not_dummy_int(obj_ptr) \
    (assert(zis_object_is_smallint((obj_ptr)) || zis_object_type((obj_ptr)) != NULL))

/// Initialize a dummy int object on stack.
static void dummy_int_obj_for_smi_init(dummy_int_obj_for_smi *restrict di, zis_smallint_t val) {
    // See `zis_int_obj_or_smallint()`.

    const bool val_neg = val < 0;
    const uint64_t val_abs = val_neg ? (uint64_t)-val : (uint64_t)val;

    struct zis_int_obj *self = &di->int_obj;
    zis_object_meta_init(self->_meta, 0, 0, 0);
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
static struct zis_int_obj *int_obj_alloc(struct zis_context *z, size_t cell_count) {
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
static size_t int_obj_cells_capacity(const struct zis_int_obj *self) {
    assert(self->_bytes_size >= INT_OBJ_BYTES_FIXED_SIZE);
    return (self->_bytes_size - INT_OBJ_BYTES_FIXED_SIZE) / sizeof(bigint_cell_t);
}

/// Number of used bits, aka bit width.
static unsigned int int_obj_width(const struct zis_int_obj *self) {
    const unsigned int cell_count = self->cell_count;
    return cell_count * BIGINT_CELL_WIDTH - zis_bits_count_lz(self->cells[cell_count - 1]);
}

// Checks if it is an integral power of two (has single bit).
static bool int_obj_is_pow2(const struct zis_int_obj *self) {
    const unsigned int cell_count = self->cell_count;
    const bigint_cell_t *const cells = self->cells;
    if (zis_bits_popcount(cells[cell_count - 1]) != 1)
        return false;
    for (unsigned int i = cell_count - 2; i != (unsigned int)-1; i++) {
        if (cells[i])
            return false;
    }
    return true;
}

/// Make a copy of an int object.
static struct zis_int_obj *int_obj_clone(struct zis_context *z, struct zis_int_obj *x) {
    struct zis_int_obj *new_x = int_obj_alloc(z, x->cell_count);
    assert(new_x);
    new_x->negative = x->negative;
    bigint_copy(new_x->cells, x->cells, x->cell_count);
    return new_x;
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
        if (c01 <= (!neg ? (uint64_t)ZIS_SMALLINT_MAX : UINT64_C(0)-(uint64_t)ZIS_SMALLINT_MIN)) {
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
        bigint_copy(obj->cells, x->cells, cell_count);
        return zis_object_from(obj);
    }

    // use the original
    assert_obj_is_not_dummy_int(zis_object_from(x));
    return zis_object_from(x);
}

/// Convert an int-obj or small-int to double.
static double int_obj_or_smallint_to_double(struct zis_object *x) {
    if (zis_object_is_smallint(x))
        return (double)zis_smallint_from_ptr(x);
    else
        return zis_int_obj_value_f(zis_object_cast(x, struct zis_int_obj));
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
        return int_obj_shrink(z, self);
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

int64_t zis_int_obj_value_trunc_i(const struct zis_int_obj *self) {
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
        const unsigned int num_width = int_obj_width(self);
        assert(num_width);
        const unsigned int n_digits = (unsigned int)((double)num_width / log2(base)) + 1;
        assert(n_digits);
        return self->negative ? n_digits + 1 : n_digits;
    }

    const char *const digits = uppercase ? digits_upper : digits_lower;
    const unsigned int cell_count = self->cell_count;
    bigint_cell_t *cell_dup = zis_mem_alloc(sizeof(bigint_cell_t) * cell_count);
    bigint_copy(cell_dup, self->cells, cell_count);
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

struct zis_object *zis_int_obj_trunc(
    struct zis_context *z,
    const struct zis_int_obj *num, unsigned int n_bits
) {
    const unsigned int num_width = int_obj_width(num);
    if (n_bits >= num_width)
        return zis_object_from(num);

    const unsigned int res_cell_count =
        zis_round_up_to_n_pow2(BIGINT_CELL_WIDTH, n_bits) / BIGINT_CELL_WIDTH;

    if (res_cell_count <= DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT) {
        dummy_int_obj_for_smi small_res;
        dummy_int_obj_for_smi_init(&small_res, 0);
        small_res.int_obj.negative = num->negative;
        bigint_trunc(num->cells, n_bits, small_res.int_obj.cells, small_res.int_obj.cell_count);
        return int_obj_shrink(z, &small_res.int_obj);
    }

    zis_locals_decl_1(z, var, const struct zis_int_obj *num);
    var.num = num;
    struct zis_int_obj *res = int_obj_alloc(z, res_cell_count);
    bigint_trunc(var.num->cells, n_bits, res->cells, res->cell_count);
    zis_locals_drop(z, var);
    assert(int_obj_shrink(z, res) == zis_object_from(res));
    return zis_object_from(res);
}

unsigned int zis_int_obj_length(const struct zis_int_obj *num) {
    return int_obj_width(num);
}

unsigned int zis_int_obj_count(const struct zis_int_obj *num, int bit) {
    unsigned int popcount = 0;
    for (unsigned int i = 0, n = num->cell_count; i < n; i++)
        popcount += zis_bits_popcount(num->cells[i]);
    return bit ? popcount : int_obj_width(num) - popcount;
}

/// Try to do simple small-int multiplication.
/// On failure, returns NULL, and `zis_int_obj_or_smallint_mul()` should be used instead.
struct zis_object *_smallint_try_mul(
    struct zis_context *z, zis_smallint_t lhs, zis_smallint_t rhs
) {
    bool overflow;
    int64_t result;
#if ZIS_USE_GNUC_OVERFLOW_ARITH
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
        assert(zis_object_type_is(rhs, z->globals->type_Int));
        dummy_int_obj_for_smi_init(&_dummy_int, zis_smallint_from_ptr(lhs));
        var.lhs_int_obj = &_dummy_int.int_obj;
        var.rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    } else if (zis_object_is_smallint(rhs)) {
        assert(zis_object_type_is(lhs, z->globals->type_Int));
        dummy_int_obj_for_smi_init(&_dummy_int, zis_smallint_from_ptr(rhs));
        var.lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
        var.rhs_int_obj = &_dummy_int.int_obj;
    } else {
        assert(zis_object_type_is(lhs, z->globals->type_Int) && zis_object_type_is(rhs, z->globals->type_Int));
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

/// Do multiplication using shift-left operation.
/// `rhs` must be a power of 2.
static struct zis_object *_int_obj_mul_using_shl(
    struct zis_context *z,
    struct zis_int_obj *lhs, struct zis_int_obj *rhs
) {
    assert(int_obj_is_pow2(rhs));
    const bool rhs_neg = rhs->negative;
    unsigned int shift_n = int_obj_width(rhs) - 1;
    struct zis_object *res =
        zis_int_obj_or_smallint_shl(z, zis_object_from(lhs), shift_n);
    if (rhs_neg) {
        if (zis_object_is_smallint(res)) {
            res = zis_int_obj_or_smallint(z, -zis_smallint_from_ptr(res));
        } else {
            struct zis_int_obj *res_v = zis_object_cast(res, struct zis_int_obj);
            assert(res_v != lhs && res_v != rhs);
            res_v->negative = !res_v->negative;
        }
    }
    return res;
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
        assert(zis_object_type_is(lhs, z->globals->type_Int));
        var.lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
    }
    if (zis_object_is_smallint(rhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int_r, zis_smallint_from_ptr(rhs));
        var.rhs_int_obj = &_dummy_int_r.int_obj;
    } else {
        assert(zis_object_type_is(rhs, z->globals->type_Int));
        var.rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    }

    do {
        struct zis_object *res;
        if (int_obj_is_pow2(var.rhs_int_obj))
            res = _int_obj_mul_using_shl(z, var.lhs_int_obj, var.rhs_int_obj);
        else if (int_obj_is_pow2(var.lhs_int_obj))
            res = _int_obj_mul_using_shl(z, var.rhs_int_obj, var.lhs_int_obj);
        else
            break;
        zis_locals_drop(z, var);
        return res;
    } while (0);

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

struct zis_float_obj *zis_int_obj_or_smallint_fdiv(
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

/// Do division using shift-right operation.
/// `rhs` must be a power of 2. `*quot_p` and `*rem_p` must be GC-safe.
static void _int_obj_divmod_using_shr(
    struct zis_context *z,
    struct zis_int_obj *lhs, struct zis_int_obj *rhs,
    struct zis_object **quot_p, struct zis_object **rem_p
) {
    assert(int_obj_is_pow2(rhs));

    if (bigint_cmp(lhs->cells, lhs->cell_count, rhs->cells, rhs->cell_count) <= 0) {
        *quot_p = zis_smallint_to_ptr(0);
        *rem_p = int_obj_shrink(z, lhs);
        return;
    }

    const bool rhs_neg = rhs->negative;
    unsigned int shift_n = int_obj_width(rhs) - 1;
    *rem_p = zis_object_from(lhs); // Protect it.
    struct zis_object *quot =
        zis_int_obj_or_smallint_shr(z, zis_object_from(lhs), shift_n);
    if (rhs_neg) {
        if (zis_object_is_smallint(quot)) {
            quot = zis_int_obj_or_smallint(z, -zis_smallint_from_ptr(quot));
        } else {
            struct zis_int_obj *res_v = zis_object_cast(quot, struct zis_int_obj);
            assert(res_v != lhs && res_v != rhs);
            res_v->negative = !res_v->negative;
        }
    }
    *quot_p = quot;
    lhs = zis_object_cast(*rem_p, struct zis_int_obj);
    *rem_p = zis_int_obj_trunc(z, lhs, shift_n);
}

zis_nodiscard bool zis_int_obj_or_smallint_divmod(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs,
    struct zis_object **quot, struct zis_object **rem
) {
    if (zis_unlikely(rhs == zis_smallint_to_ptr(0)))
        return false;

    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        // Generic selection expressions does not support statements or standalone
        // types in the association list, so I have to use conditional preprocessing blocks.
#if ZIS_SMALLINT_MAX == INT_MAX / 2
        const div_t div_res
#elif ZIS_SMALLINT_MAX == LONG_MAX / 2
        const ldiv_t div_res
#elif ZIS_SMALLINT_MAX == LLONG_MAX / 2
        const lldiv_t div_res
#else
#    error "???"
#endif
        = _Generic(lhs_v, int: div, long: ldiv, long long: lldiv)(lhs_v, rhs_v);
        if (quot)
            *quot = zis_smallint_to_ptr(div_res.quot);
        if (rem)
            *rem = zis_smallint_to_ptr(div_res.rem);
        return true;
    }

    dummy_int_obj_for_smi _dummy_int_l, _dummy_int_r;
    zis_locals_decl(
        z, var,
        struct zis_int_obj *lhs_int_obj, *rhs_int_obj, *res_quot, *res_rem;
        struct zis_object *res_tmp;
    );
    zis_locals_zero(var);

    if (zis_object_is_smallint(lhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int_l, zis_smallint_from_ptr(lhs));
        var.lhs_int_obj = &_dummy_int_l.int_obj;
    } else {
        assert(zis_object_type_is(lhs, z->globals->type_Int));
        var.lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
    }
    if (zis_object_is_smallint(rhs)) {
        dummy_int_obj_for_smi_init(&_dummy_int_r, zis_smallint_from_ptr(rhs));
        var.rhs_int_obj = &_dummy_int_r.int_obj;
    } else {
        assert(zis_object_type_is(rhs, z->globals->type_Int));
        var.rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    }

    if (int_obj_is_pow2(var.rhs_int_obj)) {
        _int_obj_divmod_using_shr(z, var.lhs_int_obj, var.rhs_int_obj, quot, rem);
        zis_locals_drop(z, var);
        return true;
    }

    var.res_quot = int_obj_alloc(z, var.lhs_int_obj->cell_count);
    var.res_rem  = int_obj_alloc(z, var.lhs_int_obj->cell_count);
    var.res_quot->negative = var.lhs_int_obj->negative != var.rhs_int_obj->negative;
    var.res_rem->negative  = var.res_quot->negative;

    if (var.rhs_int_obj->cell_count == 1) {
        bigint_copy(
            var.res_quot->cells, var.lhs_int_obj->cells,
            var.lhs_int_obj->cell_count
        );
        var.res_rem->cell_count = 1;
        var.res_rem->cells[0] = bigint_self_div_1(
            var.res_quot->cells, var.res_quot->cell_count,
            var.rhs_int_obj->cells[0]
        );
    } else {
        struct zis_int_obj *const tmp_buf =
            int_obj_alloc(z, var.lhs_int_obj->cell_count);
        bigint_div(
            var.lhs_int_obj->cells, var.lhs_int_obj->cell_count,
            var.rhs_int_obj->cells, var.rhs_int_obj->cell_count,
            tmp_buf->cells,
            var.res_quot->cells, var.res_rem->cells
        );
    }

    var.res_tmp = int_obj_shrink(z, var.res_rem);
    if (quot)
        *quot = int_obj_shrink(z, var.res_quot);
    if (rem)
        *rem = var.res_tmp; // rem

    zis_locals_drop(z, var);
    return true;
}

struct zis_object *zis_int_obj_or_smallint_pow(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
) {
    do {
        if (!zis_object_is_smallint(lhs) || !zis_object_is_smallint(rhs))
            break;
        zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs);
        zis_smallint_t rhs_smi = zis_smallint_from_ptr(rhs);
        if (zis_unlikely(lhs_smi == 1 || rhs_smi == 0))
            return zis_smallint_to_ptr(1);
        if (zis_unlikely(lhs_smi == 0))
            return zis_smallint_to_ptr(0);

        bool lhs_neg = lhs_smi < 0;
#if ZIS_SMALLINT_MAX > UINT32_MAX
        if (lhs_smi > UINT32_MAX || lhs_smi < -(zis_smallint_t)UINT32_MAX)
            break;
#endif
        uint32_t lhs_abs = lhs_neg ? ((zis_smallint_unsigned_t)0 - (zis_smallint_unsigned_t)lhs_smi) : (uint32_t)lhs_smi;

        if (rhs_smi < 0)
            break;
#if ZIS_SMALLINT_MAX > UINT32_MAX
        if (rhs_smi > UINT32_MAX)
            break;
#endif
        uint32_t rhs_v = (uint32_t)rhs_smi;

        int64_t result = zis_math_pow_u32(lhs_abs, rhs_v);
        if (result == 0 && lhs_abs != 0)
            break;
        if (lhs_neg && (rhs_v & 1))
            result = -result;
        return zis_int_obj_or_smallint(z, result);
    } while (0);

    if (zis_object_is_smallint(rhs) ?
            zis_smallint_from_ptr(rhs) < 0 :
            zis_int_obj_sign((struct zis_int_obj *)rhs)
    ) {
        const double result = pow(
            int_obj_or_smallint_to_double(lhs),
            int_obj_or_smallint_to_double(rhs)
        );
        return zis_object_from(zis_float_obj_new(z, result));
    }

    zis_smallint_unsigned_t exponent =
        (zis_smallint_unsigned_t)zis_smallint_from_ptr(rhs);
    zis_locals_decl(
        z, var,
        struct zis_object *base;
        struct zis_object *result;
    );
    var.base = lhs;
    var.result = zis_smallint_to_ptr(1);

    // See `zis_math_pow_u32()`.
    assert(exponent >= 1);
    while (true) {
        if (exponent & 1) {
            var.result = zis_int_obj_or_smallint_mul(z, var.result, var.base);
            if (zis_unlikely(!var.result))
                break; // Too large.
            if (zis_unlikely(exponent == 1))
                break; // Done.
        }
        var.base = zis_int_obj_or_smallint_mul(z, var.base, var.base);
        if (zis_unlikely(!var.base)) {
            var.result = NULL;
            break; // Too large.
        }
        exponent >>= 1;
    }

    zis_locals_drop(z, var);
    return var.result;
}

struct zis_object *zis_int_obj_or_smallint_shl(
    struct zis_context *z, struct zis_object *lhs, unsigned int rhs
) {
    if (zis_unlikely(!rhs))
        return lhs;

    dummy_int_obj_for_smi _dummy_int_lhs;
    struct zis_int_obj *lhs_v;

    if (zis_object_is_smallint(lhs)) {
        const int64_t lhs_as_i64 = zis_smallint_from_ptr(lhs);
        if (zis_likely(rhs < sizeof(lhs_as_i64) * 8)) {
            const int64_t res = lhs_as_i64 << rhs;
            if (res >> rhs == lhs_as_i64)
                return zis_int_obj_or_smallint(z, res);
        } // else: UB if do `lhs_as_i64 << rhs`.
        dummy_int_obj_for_smi_init(&_dummy_int_lhs, zis_smallint_from_ptr(lhs));
        lhs_v = &_dummy_int_lhs.int_obj;
        lhs = zis_object_from(lhs_v);
    } else {
        // assert(zis_object_type_is(lhs, z->globals->type_Int));
        assert_obj_is_int_or_dummy_int(z, lhs); // Being used in `_int_obj_mul_using_shl()`, `lhs` may be a dummy int.
        lhs_v = zis_object_cast(lhs, struct zis_int_obj);
    }

    const unsigned int lhs_width = int_obj_width(lhs_v);
    if (UINT_MAX - rhs < lhs_width || lhs_width + rhs > INT_OBJ_CELL_COUNT_MAX)
        return NULL;

    const unsigned int res_width = lhs_width + rhs;
    if (res_width <= BIGINT_CELL_WIDTH * DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT) {
        dummy_int_obj_for_smi _dummy_int;
        _dummy_int.int_obj.negative = lhs_v->negative;
        _dummy_int.int_obj.cell_count = DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT;
        bigint_shl(lhs_v->cells, lhs_v->cell_count, rhs, _dummy_int.int_obj.cells, _dummy_int.int_obj.cell_count);
        struct zis_object *res = int_obj_shrink(z, &_dummy_int.int_obj);
        return (void *)res == (void *)&_dummy_int.int_obj ?
            zis_object_from(int_obj_clone(z, &_dummy_int.int_obj)) : res;
    } else {
        zis_locals_decl_1(z, var, struct zis_int_obj *lhs);
        var.lhs = lhs_v;
        const unsigned int res_cell_count =
            zis_round_up_to_n_pow2(BIGINT_CELL_WIDTH, res_width) / BIGINT_CELL_WIDTH;
        struct zis_int_obj *res = int_obj_alloc(z, res_cell_count);
        res->negative = var.lhs->negative;
        bigint_shl(lhs_v->cells, lhs_v->cell_count, rhs, res->cells, res->cell_count);
        zis_locals_drop(z, var);
        assert(res->cells[res->cell_count - 1]); // `int_obj_shrink()` is not needed.
        return zis_object_from(res);
    }
}

struct zis_object *zis_int_obj_or_smallint_shr(
    struct zis_context *z, struct zis_object *lhs, unsigned int rhs
) {
    if (zis_unlikely(!rhs))
        return lhs;

    if (zis_object_is_smallint(lhs)) {
        const zis_smallint_t lhs_smi = zis_smallint_from_ptr(lhs);
        if (zis_unlikely(rhs >= sizeof(lhs_smi) * 8))
            return zis_smallint_to_ptr(0); // UB if do `lhs_smi >> rhs`.
#if 0 // The standard says it is implementation-defined whether to perform a arithmetic right shift.
        if (lhs_smi < 0)
            return zis_smallint_to_ptr(-(-lhs_smi >> rhs));
        else
#endif
        return zis_smallint_to_ptr(lhs_smi >> rhs);
    }

    assert(zis_object_type_is(lhs, z->globals->type_Int));
    struct zis_int_obj *const lhs_v = zis_object_cast(lhs, struct zis_int_obj);
    const unsigned int lhs_width = int_obj_width(lhs_v);
    if (rhs >= lhs_width)
        return lhs_v->negative ? zis_smallint_to_ptr(-1) : zis_smallint_to_ptr(0);

    const unsigned int res_width = lhs_width - rhs;
    if (res_width <= BIGINT_CELL_WIDTH * DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT) {
        dummy_int_obj_for_smi _dummy_int;
        _dummy_int.int_obj.negative = lhs_v->negative;
        _dummy_int.int_obj.cell_count = DUMMY_INT_OBJ_FOR_SMI_CELL_COUNT;
        bigint_shr(lhs_v->cells, lhs_v->cell_count, rhs, _dummy_int.int_obj.cells, _dummy_int.int_obj.cell_count);
        struct zis_object *res = int_obj_shrink(z, &_dummy_int.int_obj);
        return (void *)res == (void *)&_dummy_int.int_obj ?
            zis_object_from(int_obj_clone(z, &_dummy_int.int_obj)) : res;
    } else {
        zis_locals_decl_1(z, var, struct zis_int_obj *lhs);
        var.lhs = lhs_v;
        const unsigned int res_cell_count =
            zis_round_up_to_n_pow2(BIGINT_CELL_WIDTH, res_width) / BIGINT_CELL_WIDTH;
        struct zis_int_obj *res = int_obj_alloc(z, res_cell_count);
        res->negative = var.lhs->negative;
        bigint_shr(lhs_v->cells, lhs_v->cell_count, rhs, res->cells, res->cell_count);
        zis_locals_drop(z, var);
        assert(res->cells[res->cell_count - 1]); // `int_obj_shrink()` is not needed.
        return zis_object_from(res);
    }
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
    if (zis_object_is_smallint(lhs) || zis_object_is_smallint(rhs))
        return false;
    const struct zis_int_obj *lhs_int_obj = zis_object_cast(lhs, struct zis_int_obj);
    const struct zis_int_obj *rhs_int_obj = zis_object_cast(rhs, struct zis_int_obj);
    if (lhs_int_obj->cell_count != rhs_int_obj->cell_count)
        return false;
    return memcmp(lhs_int_obj->cells, rhs_int_obj->cells, lhs_int_obj->cell_count * sizeof(bigint_cell_t)) == 0;
}

/// See `_int_obj_or_smallint_bitwise_op_slow()`.
enum _bitwise_op {
    BITWISE_OP_AND,
    BITWISE_OP_OR,
    BITWISE_OP_XOR,
};

/// Do small-int or int-obj bitwise and/or/xor. Two operands cannot be both small-ints.
static struct zis_object *_int_obj_or_smallint_bitwise_op_slow(
    struct zis_context *z,
    struct zis_object *_lhs, struct zis_object *_rhs,
    enum _bitwise_op op
) {
    assert(!(zis_object_is_smallint(_lhs) && zis_object_is_smallint(_rhs)));

    dummy_int_obj_for_smi _dummy_int;
    zis_locals_decl(
        z, var,
        struct zis_int_obj *lhs_int_obj, *rhs_int_obj, *res_int_obj;
    );
    zis_locals_zero(var);

    if (zis_object_is_smallint(_lhs)) {
        assert(zis_object_type_is(_rhs, z->globals->type_Int));
        dummy_int_obj_for_smi_init(&_dummy_int, zis_smallint_from_ptr(_lhs));
        var.lhs_int_obj = &_dummy_int.int_obj;
        var.rhs_int_obj = zis_object_cast(_rhs, struct zis_int_obj);
    } else if (zis_object_is_smallint(_rhs)) {
        assert(zis_object_type_is(_lhs, z->globals->type_Int));
        dummy_int_obj_for_smi_init(&_dummy_int, zis_smallint_from_ptr(_rhs));
        var.lhs_int_obj = zis_object_cast(_lhs, struct zis_int_obj);
        var.rhs_int_obj = &_dummy_int.int_obj;
    } else {
        assert(zis_object_type_is(_lhs, z->globals->type_Int) && zis_object_type_is(_rhs, z->globals->type_Int));
        var.lhs_int_obj = zis_object_cast(_lhs, struct zis_int_obj);
        var.rhs_int_obj = zis_object_cast(_rhs, struct zis_int_obj);
    }

    // Make sure lhs is not shorter than rhs.
    if (var.lhs_int_obj->cell_count < var.rhs_int_obj->cell_count) {
        struct zis_int_obj *tmp = var.lhs_int_obj;
        var.lhs_int_obj = var.rhs_int_obj;
        var.rhs_int_obj = tmp;
    }

    // Use two's complement if negative.
    if (var.lhs_int_obj->negative)
        bigint_complement(var.lhs_int_obj->cells, var.lhs_int_obj->cell_count, var.lhs_int_obj->cells);
    if (var.rhs_int_obj->negative)
        bigint_complement(var.rhs_int_obj->cells, var.rhs_int_obj->cell_count, var.rhs_int_obj->cells);

    switch (op) {
    case BITWISE_OP_AND:
        var.res_int_obj = int_obj_alloc(z, var.rhs_int_obj->negative ? var.lhs_int_obj->cell_count : var.rhs_int_obj->cell_count);
        var.res_int_obj->negative = var.lhs_int_obj->negative && var.rhs_int_obj->negative;
        for (size_t i = 0, n = var.rhs_int_obj->cell_count; i < n; i++)
            var.res_int_obj->cells[i] = var.lhs_int_obj->cells[i] & var.rhs_int_obj->cells[i];
        break;

    case BITWISE_OP_OR:
        var.res_int_obj = int_obj_alloc(z, var.rhs_int_obj->negative ? var.rhs_int_obj->cell_count : var.lhs_int_obj->cell_count);
        var.res_int_obj->negative = var.lhs_int_obj->negative || var.rhs_int_obj->negative;
        for (size_t i = 0, n = var.rhs_int_obj->cell_count; i < n; i++)
            var.res_int_obj->cells[i] = var.lhs_int_obj->cells[i] | var.rhs_int_obj->cells[i];
        break;

    case BITWISE_OP_XOR:
        var.res_int_obj = int_obj_alloc(z, var.lhs_int_obj->cell_count);
        var.res_int_obj->negative = var.lhs_int_obj->negative != var.rhs_int_obj->negative;
        for (size_t i = 0, n = var.rhs_int_obj->cell_count; i < n; i++)
            var.res_int_obj->cells[i] = var.lhs_int_obj->cells[i] ^ var.rhs_int_obj->cells[i];
        if (var.rhs_int_obj->negative) {
            for (size_t i = var.rhs_int_obj->cell_count, i_end = var.lhs_int_obj->cell_count; i < i_end; i++)
                var.res_int_obj->cells[i] = ~var.lhs_int_obj->cells[i];
            goto skip_copying_rest_cells;
        }
        break;

    default:
        zis_unreachable();
    }
    if (var.res_int_obj->cell_count > var.rhs_int_obj->cell_count) {
        const size_t copied_count = var.rhs_int_obj->cell_count;
        const size_t rest_count = var.res_int_obj->cell_count - copied_count;
        bigint_copy(
            var.res_int_obj->cells + copied_count,
            var.lhs_int_obj->cells + copied_count,
            rest_count
        );
    }
skip_copying_rest_cells:;
    if (var.res_int_obj->negative) {
        bigint_complement(var.res_int_obj->cells, var.res_int_obj->cell_count, var.res_int_obj->cells);
    }

    // Undo two's complement if negative.
    if (var.lhs_int_obj->negative)
        bigint_complement(var.lhs_int_obj->cells, var.lhs_int_obj->cell_count, var.lhs_int_obj->cells);
    if (var.rhs_int_obj->negative && var.rhs_int_obj != &_dummy_int.int_obj)
        bigint_complement(var.rhs_int_obj->cells, var.rhs_int_obj->cell_count, var.rhs_int_obj->cells);

    zis_locals_drop(z, var);
    return int_obj_shrink(z, var.res_int_obj);
}

struct zis_object *zis_int_obj_or_smallint_not(
    struct zis_context *z, struct zis_object *val
) {
    if (zis_object_is_smallint(val)) {
        const zis_smallint_t v = zis_smallint_from_ptr(val);
        return zis_smallint_to_ptr(~v);
    }

    struct zis_object *const result =
        zis_int_obj_or_smallint_add(z, val, zis_smallint_to_ptr(1));
    assert(zis_object_type_is(result, z->globals->type_Int));
    struct zis_int_obj *res_int = zis_object_cast(result, struct zis_int_obj);
    res_int->negative = !res_int->negative;
    return result;
}

struct zis_object *zis_int_obj_or_smallint_and(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        return zis_smallint_to_ptr(lhs_v & rhs_v);
    }

    return _int_obj_or_smallint_bitwise_op_slow(z, lhs, rhs, BITWISE_OP_AND);
}

struct zis_object *zis_int_obj_or_smallint_or(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        return zis_smallint_to_ptr(lhs_v | rhs_v);
    }

    return _int_obj_or_smallint_bitwise_op_slow(z, lhs, rhs, BITWISE_OP_OR);
}

struct zis_object *zis_int_obj_or_smallint_xor(
    struct zis_context *z, struct zis_object *lhs, struct zis_object *rhs
) {
    if (zis_object_is_smallint(lhs) && zis_object_is_smallint(rhs)) {
        const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs), rhs_v = zis_smallint_from_ptr(rhs);
        return zis_smallint_to_ptr(lhs_v ^ rhs_v);
    }

    return _int_obj_or_smallint_bitwise_op_slow(z, lhs, rhs, BITWISE_OP_XOR);
}

#define assert_arg1_smi_or_Int(__z) \
do {                                \
    struct zis_object *x = (__z)->callstack->frame[1]; \
    zis_unused_var(x);              \
    assert(zis_object_is_smallint(x) || zis_object_type_is(x, (__z)->globals->type_Int)); \
} while (0)

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
        z, "value", NULL, "the integer is too large"
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
        struct zis_int_obj *res_int_obj = int_obj_clone(z, self_int_obj);
        res_int_obj->negative = !self_int_obj->negative;
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
        result = zis_int_obj_or_smallint_fdiv(z, self_v, other_v);
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

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_pow, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'**'(other :: Int|Float) :: Int|Float
    Operator **. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;

    struct zis_object *result;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type || other_type == g->type_Int) {
        result = zis_int_obj_or_smallint_pow(z, self_v, other_v);
        if (zis_unlikely(!result))
            return int_obj_too_large_error(z);
    } else if (other_type == g->type_Float) {
        double self_as_f = int_obj_or_smallint_to_double(self_v);
        double other_f = zis_float_obj_value(zis_object_cast(other_v, struct zis_float_obj));
        result = zis_object_from(zis_float_obj_new(z, pow(self_as_f, other_f)));
    } else {
        return int_obj_bin_op_unsupported_error(z, "**");
    }
    frame[0] = result;
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_shl, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'<<'(other :: Int) :: Int
    Operator <<. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type) {
        const zis_smallint_t x = zis_smallint_from_ptr(other_v);
        if (x >= 0) {
#if ZIS_SMALLINT_MAX > UINT_MAX
            if (x > UINT_MAX)
                return int_obj_too_large_error(z);
#endif
            const unsigned int n = (unsigned int)(zis_smallint_unsigned_t)x;
            struct zis_object *res = zis_int_obj_or_smallint_shl(z, self_v, n);
            if (!res)
                return int_obj_too_large_error(z);
            frame[0] = res;
        } else {
            unsigned int n;
#if ZIS_SMALLINT_MAX > UINT_MAX
            if (x == ZIS_SMALLINT_MIN || -x > UINT_MAX) {
                static_assert(UINT_MAX > INT_OBJ_CELL_COUNT_MAX * BIGINT_CELL_WIDTH, "");
                n = UINT_MAX;
            } else
#endif
            n = (unsigned int)(zis_smallint_unsigned_t)-x;
            frame[0] = zis_int_obj_or_smallint_shr(z, self_v, n);
        }
    } else if (other_type == g->type_Int) {
        if (!zis_object_cast(other_v, struct zis_int_obj)->negative)
            return int_obj_too_large_error(z);
        frame[0] = zis_smallint_to_ptr(0);
    } else {
        return int_obj_bin_op_unsupported_error(z, "<<");
    }
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_shr, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'>>'(other :: Int) :: Int
    Operator >>. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    struct zis_type_obj *other_type = zis_object_type_1(other_v);
    if (!other_type) {
        const zis_smallint_t x = zis_smallint_from_ptr(other_v);
        if (x >= 0) {
            unsigned int n;
#if ZIS_SMALLINT_MAX > UINT_MAX
            if (x > UINT_MAX) {
                static_assert(UINT_MAX > INT_OBJ_CELL_COUNT_MAX * BIGINT_CELL_WIDTH, "");
                n = UINT_MAX;
            } else
#endif
            n = (unsigned int)(zis_smallint_unsigned_t)x;
            frame[0] = zis_int_obj_or_smallint_shr(z, self_v, n);
        } else {
#if ZIS_SMALLINT_MAX > UINT_MAX
            if (x == ZIS_SMALLINT_MIN || -x > UINT_MAX)
                return int_obj_too_large_error(z);
#endif
            const unsigned int n = (unsigned int)(zis_smallint_unsigned_t)-x;
            struct zis_object *res = zis_int_obj_or_smallint_shl(z, self_v, n);
            if (!res)
                return int_obj_too_large_error(z);
            frame[0] = res;
        }
    } else if (other_type == g->type_Int) {
        if (zis_object_cast(other_v, struct zis_int_obj)->negative)
            return int_obj_too_large_error(z);
        frame[0] = zis_smallint_to_ptr(0);
    } else {
        return int_obj_bin_op_unsupported_error(z, ">>");
    }
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_not, z, {1, 0, 1}) {
    /*#DOCSTR# func Int:\'|'() :: Int
    Operator ~. */
    assert_arg1_smi_or_Int(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1];
    frame[0] = zis_int_obj_or_smallint_not(z, self_v);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_and, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'&'(other :: Int) :: Int
    Operator &. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    if (!(zis_object_is_smallint(other_v) || zis_object_type(other_v) == g->type_Int))
        return int_obj_bin_op_unsupported_error(z, "&");
    frame[0] = zis_int_obj_or_smallint_and(z, self_v, other_v);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_or, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'|'(other :: Int) :: Int
    Operator |. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    if (!(zis_object_is_smallint(other_v) || zis_object_type(other_v) == g->type_Int))
        return int_obj_bin_op_unsupported_error(z, "|");
    frame[0] = zis_int_obj_or_smallint_or(z, self_v, other_v);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_operator_xor, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:\'^'(other :: Int) :: Int
    Operator ^. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *other_v = frame[2];
    if (!(zis_object_is_smallint(other_v) || zis_object_type(other_v) == g->type_Int))
        return int_obj_bin_op_unsupported_error(z, "^");
    frame[0] = zis_int_obj_or_smallint_xor(z, self_v, other_v);
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

ZIS_NATIVE_FUNC_DEF(T_Int_M_div, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:div(d :: Int) :: Tuple[Int, Int]
    Computes the quotient and the remainder of the division of self by d. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *d_v = frame[2];
    if (!(zis_object_is_smallint(d_v) || zis_object_type(d_v) == g->type_Int)) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE, "d", d_v
        ));
        return ZIS_THR;
    }
    if (!zis_int_obj_or_smallint_divmod(z, self_v, d_v, frame + 1, frame + 2)) {
        frame[0] = zis_object_from(zis_exception_obj_format(
            z, "value", NULL, "division by zero"
        ));
        return ZIS_THR;
    }
    frame[0] = zis_object_from(zis_tuple_obj_new(z, frame + 1, 2));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_length, z, {1, 0, 1}) {
    /*#DOCSTR# func Int:length() :: Int
    Returns the number of bits in the integer, aka bit width. */
    assert_arg1_smi_or_Int(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1];
    unsigned int result;
    if (zis_object_is_smallint(self_v)) {
        zis_smallint_t self_smi = zis_smallint_from_ptr(self_v);
        zis_smallint_unsigned_t self_smi_abs = self_smi < 0 ?
            (zis_smallint_unsigned_t)0 - (zis_smallint_unsigned_t)self_smi :
            (zis_smallint_unsigned_t)self_smi;
        result = !self_smi_abs ? 0 : sizeof self_smi_abs * 8 - zis_bits_count_lz(self_smi_abs);
    } else {
        result = zis_int_obj_length(zis_object_cast(self_v, struct zis_int_obj));
    }
    frame[0] = zis_object_from(zis_int_obj_or_smallint(z, (int64_t)result));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(T_Int_M_count, z, {2, 0, 2}) {
    /*#DOCSTR# func Int:count(bit :: Int) :: Int
    Returns the number of `bit` (0 or 1) bits in the integer. */
    assert_arg1_smi_or_Int(z);
    struct zis_context_globals *g = z->globals;
    struct zis_object **frame = z->callstack->frame;
    struct zis_object *self_v = frame[1], *bit_v = frame[2];
    if (!(zis_object_is_smallint(bit_v) || zis_object_type(bit_v) == g->type_Int)) {
        frame[0] = zis_object_from(zis_exception_obj_format_common(
            z, ZIS_EXC_FMT_WRONG_ARGUMENT_TYPE, "bit", bit_v
        ));
        return ZIS_THR;
    }
    int bit;
    if (zis_object_is_smallint(bit_v)) {
        zis_smallint_t bit_ = zis_smallint_from_ptr(bit_v);
        bit = (bit_ == 0 || bit_ == 1) ? (int)bit_ : 2;
    } else {
        bit = 2;
    }
    unsigned int result;
    if (bit == 2) {
        result = 0;
    } else if (zis_object_is_smallint(self_v)) {
        zis_smallint_t self_smi = zis_smallint_from_ptr(self_v);
        zis_smallint_unsigned_t self_smi_abs = self_smi < 0 ?
            (zis_smallint_unsigned_t)0 - (zis_smallint_unsigned_t)self_smi :
            (zis_smallint_unsigned_t)self_smi;
        const unsigned int popcount = zis_bits_popcount(self_smi_abs);
        result = bit ? popcount : !self_smi_abs ? 0 : (sizeof self_smi_abs * 8 - zis_bits_count_lz(self_smi_abs) - popcount);
    } else {
        result = zis_int_obj_count(zis_object_cast(self_v, struct zis_int_obj), bit);
    }
    frame[0] = zis_object_from(zis_int_obj_or_smallint(z, (int64_t)result));
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
    { "**"          , &T_Int_M_operator_pow   },
    { "<<"          , &T_Int_M_operator_shl   },
    { ">>"          , &T_Int_M_operator_shr   },
    { "~"           , &T_Int_M_operator_not   },
    { "&"           , &T_Int_M_operator_and   },
    { "|"           , &T_Int_M_operator_or    },
    { "^"           , &T_Int_M_operator_xor   },
    { "=="          , &T_Int_M_operator_equ   },
    { "<=>"         , &T_Int_M_operator_cmp   },
    { "hash"        , &T_Int_M_hash           },
    { "to_string"   , &T_Int_M_to_string      },
    { "div"         , &T_Int_M_div            },
    { "length"      , &T_Int_M_length         },
    { "count"       , &T_Int_M_count          },
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
