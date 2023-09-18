/// Bit-wise operations and bitset.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "attributes.h"

/* ----- bit-wise operations ------------------------------------------------ */

#if defined __GNUC__ || defined __clang__

/// Count trailing zero bits. `X` must not be 0.
#define zis_bits_count_tz(X) \
    (unsigned int) _Generic((X), \
        unsigned long long : __builtin_ctzll, \
        unsigned long      : __builtin_ctzl , \
        unsigned int       : __builtin_ctz    \
    ) ((X)) \
// ^^^ zis_bits_count_tz() ^^^

#elif defined _MSC_VER

#define zis_bits_count_tz(X) \
    _Generic((X),             \
        unsigned long long : _zis_bits_count_tz_u64_msvc, \
        unsigned long      : _zis_bits_count_tz_u32_msvc, \
        unsigned int       : _zis_bits_count_tz_u32_msvc  \
    ) ((X)) \
// ^^^ zis_bits_count_tz() ^^^

static __forceinline unsigned int _zis_bits_count_tz_u32_msvc(unsigned long mask) {
    unsigned long index;
    _BitScanForward(&index, mask);
    return (unsigned int)index;
}

static __forceinline unsigned int _zis_bits_count_tz_u64_msvc(unsigned __int64 mask) {
    unsigned long index;
    _BitScanForward64(&index, mask);
    return (unsigned int)index;
}

#else

#error "Not implemented on this platform."

#endif

/* ----- bitset ------------------------------------------------------------- */

typedef size_t zis_bitset_cell_t;

/// Bit set, an array of bits. Size is not stored inside for more usable space.
struct zis_bitset {
    zis_bitset_cell_t _cells[1];
};

/// The minimum size in bytes required for an `n_bits` bit map.
#define zis_bitset_required_size(n_bits) \
    (zis_round_up_to(sizeof(zis_bitset_cell_t) * 8, n_bits) / 8)

/// Convert bit index to internal indices.
#define zis_bitset_extract_index( \
    bit_index, cell_index_var, bit_offset_var, bit_mask_var \
)                                 \
    do {                          \
        (cell_index_var) = (bit_index) / (sizeof(zis_bitset_cell_t) * 8); \
        (bit_offset_var) = (bit_index) % (sizeof(zis_bitset_cell_t) * 8); \
        (bit_mask_var)   = (zis_bitset_cell_t)1u << (bit_offset_var);     \
    } while (0)                   \
// ^^^ zis_bitset_extract_index() ^^^

/// Test if a bit is set.
zis_static_force_inline bool
zis_bitset_test_bit(const struct zis_bitset *bs, size_t bit_index) {
    size_t cell_index, bit_offset;
    zis_bitset_cell_t bit_mask;
    zis_bitset_extract_index(bit_index, cell_index, bit_offset, bit_mask);
    return bs->_cells[cell_index] & bit_mask;
}

/// Set a bit to true.
zis_static_force_inline void
zis_bitset_set_bit(struct zis_bitset *bs, size_t bit_index) {
    size_t cell_index, bit_offset;
    zis_bitset_cell_t bit_mask;
    zis_bitset_extract_index(bit_index, cell_index, bit_offset, bit_mask);
    bs->_cells[cell_index] |= bit_mask;
}

/// Set a bit to false.
zis_static_force_inline void
zis_bitset_reset_bit(struct zis_bitset *bs, size_t bit_index) {
    size_t cell_index, bit_offset;
    zis_bitset_cell_t bit_mask;
    zis_bitset_extract_index(bit_index, cell_index, bit_offset, bit_mask);
    bs->_cells[cell_index] &= ~bit_mask;
}

/// Set a bit to true if it is false.
zis_static_force_inline void
zis_bitset_try_set_bit(struct zis_bitset *bs, size_t bit_index) {
    size_t cell_index, bit_offset;
    zis_bitset_cell_t bit_mask;
    zis_bitset_extract_index(bit_index, cell_index, bit_offset, bit_mask);
    zis_bitset_cell_t *const cell_ptr = bs->_cells + cell_index;
    const zis_bitset_cell_t cell_data = *cell_ptr;
    if (!(cell_data & bit_mask))
        *cell_ptr = cell_data | bit_mask;
}

/// Set a bit to false if it is true.
zis_static_force_inline void
zis_bitset_try_reset_bit(struct zis_bitset *bs, size_t bit_index) {
    size_t cell_index, bit_offset;
    zis_bitset_cell_t bit_mask;
    zis_bitset_extract_index(bit_index, cell_index, bit_offset, bit_mask);
    zis_bitset_cell_t *const cell_ptr = bs->_cells + cell_index;
    const zis_bitset_cell_t cell_data = *cell_ptr;
    if (cell_data & bit_mask)
        *cell_ptr = cell_data & ~bit_mask;
}

/// Set all bits to false.
zis_static_inline void zis_bitset_clear(struct zis_bitset *bs, size_t size) {
    for (size_t i = 0, n = size / sizeof(zis_bitset_cell_t); i < n; i++)
        bs->_cells[i] = 0;
}

/// Iterate over set bits.
#define zis_bitset_foreach_set(bitset, bitset_size, BIT_INDEX_VAR, STATEMENT) \
    do {                                                                      \
        zis_bitset_cell_t *const __cells = (bitset)->_cells;                  \
        const size_t __cells_count = bitset_size / sizeof(zis_bitset_cell_t); \
        for (size_t __cell_i = 0; __cell_i < __cells_count; __cell_i++) {     \
            zis_bitset_cell_t __cell_data = __cells[__cell_i];                \
            for (zis_bitset_cell_t __t; __cell_data; __cell_data ^= __t) {    \
                __t = __cell_data & -__cell_data;                             \
                unsigned int __n = zis_bits_count_tz(__cell_data);            \
                {                                                             \
                    const size_t BIT_INDEX_VAR =                              \
                        __cell_i * sizeof(zis_bitset_cell_t) * 8 + __n;       \
                    { STATEMENT }                                             \
                }                                                             \
            }                                                                 \
        }                                                                     \
    } while (0)                                                               \
// ^^^ zis_bitset_foreach_set() ^^^
