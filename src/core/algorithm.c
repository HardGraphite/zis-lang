#include "algorithm.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "attributes.h"
#include "platform.h"
#include "smallint.h"

#include "zis_config.h" // ZIS_USE_GNUC_OVERFLOW_ARITH

/* ----- numbers ------------------------------------------------------------ */

double zis_math_log(double a, double x) {
    return log(x) / log(a);
}

zis_static_force_inline bool uint32_mul_overflow(uint32_t a, uint32_t b, uint32_t *res) {
#if ZIS_USE_GNUC_OVERFLOW_ARITH
    return __builtin_mul_overflow(a, b, res);
#else // !ZIS_USE_GNUC_OVERFLOW_ARITH
    uint64_t prod = (uint64_t)a * (uint64_t)b;
    *res = (uint32_t)prod;
    return prod >> 32;
#endif // ZIS_USE_GNUC_OVERFLOW_ARITH
}

uint32_t zis_math_pow_u32(uint32_t base, uint32_t exponent) {
    if (zis_unlikely(exponent == 0))
        return 1;

    uint32_t result = 1;
    while (true) {
        if (exponent & 1) {
            if (zis_unlikely(uint32_mul_overflow(result, base, &result)))
                return 0;
            if (zis_unlikely(exponent == 1))
                break;
        }
        if (zis_unlikely(uint32_mul_overflow(base, base, &base)))
            return 0;
        exponent >>= 1;
    }
    return result;
}

/* ----- hash functions ----------------------------------------------------- */

size_t zis_hash_float(double num) {
    if (isnormal(num)) {
        int exp;
        const double f = ldexp(frexp(fabs(num), &exp), sizeof(unsigned int) * 8);
        assert(f <= (double)(unsigned int)-1 && f > 0.0);
        const unsigned int h = (unsigned int)f ^ (unsigned int)exp;
#if UINT_MAX < SIZE_MAX
        return h;
#else
        return (size_t)h & (SIZE_MAX >> 2);
#endif
    }
    if (num == 0.0) {
        return 0U;
    }
    return (SIZE_MAX >> 2) - 1;
}

size_t zis_hash_pointer(const void *ptr) {
    const size_t ptr_num = (size_t)(uintptr_t)ptr;
    return ptr_num >> 4;
}

#ifdef _MSC_VER
extern unsigned int _rotl(unsigned int value, int shift);
#endif // _MSC_VER

zis_static_force_inline uint32_t rotl32(uint32_t x, int8_t r) {
#if defined(_MSC_VER)
    return _rotl(x, r);
#else // !_MSC_VER
    return (x << r) | (x >> (32 - r));
#endif // _MSC_VER
}

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.
static uint32_t MurmurHash3_x86_32(const void *key, int len, uint32_t seed) {
    const uint8_t *data = (const uint8_t *)key;
    const int nblocks = len / 4;

    uint32_t h1 = seed;

    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const uint32_t *blocks = (const uint32_t *)(data + (ptrdiff_t)nblocks * 4);

    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];

        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    const uint8_t *tail = (const uint8_t *)(data + (ptrdiff_t)nblocks * 4);

    uint32_t k1 = 0;

    switch (len & 3) {
    case 3:
        k1 ^= tail[2] << 16;
        zis_fallthrough;
    case 2:
        k1 ^= tail[1] << 8;
        zis_fallthrough;
    case 1:
        k1 ^= tail[0];
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
    default:
        break;
    };

    h1 ^= len;

    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;

    return h1;
}

size_t zis_hash_bytes(const void *data, size_t size) {
    assert(size <= INT32_MAX);
    const uint32_t seed = 0x5d9ee90;
    const uint32_t h = MurmurHash3_x86_32(data, (int)size, seed);
#if UINT_MAX < SIZE_MAX
    return h;
#else
    return (size_t)h & (SIZE_MAX >> 2);
#endif
}

void zis_hash_combine(size_t *restrict hash_code_p, size_t new_hash_code) {
    // Adapted from Boost version 1.86 (boost::hash_combine).
    // License: https://www.boost.org/LICENSE_1_0.txt .

    size_t x = *hash_code_p + 0x9e3779b9 + new_hash_code;

#if ZIS_WORDSIZE == 64

    const uint64_t m = 0xe9846af9b1a615d;

    x ^= x >> 32;
    x *= m;
    x ^= x >> 32;
    x *= m;
    x ^= x >> 28;

#else // ZIS_WORDSIZE == 32

    const uint32_t m1 = 0x21f0aaad;
    const uint32_t m2 = 0x735a2d97;

    x ^= x >> 16;
    x *= m1;
    x ^= x >> 15;
    x *= m2;
    x ^= x >> 15;

#endif // ZIS_WORDSIZE

    *hash_code_p = x;
}

size_t zis_hash_truncate(size_t hash_code) {
    static_assert(sizeof hash_code == sizeof(zis_smallint_t), "");
    hash_code = (size_t)(((zis_smallint_t)hash_code << 1) >> 1);
    assert((zis_smallint_t)hash_code >= ZIS_SMALLINT_MIN);
    assert((zis_smallint_t)hash_code <= ZIS_SMALLINT_MAX);
    return hash_code;
}
