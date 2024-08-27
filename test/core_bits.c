#include "test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../core/bits.h"

zis_test0_define(bits_count_tz_u32) {
    for (unsigned int i = 1; i < 32; i++) {
        unsigned int result;
        result = zis_bits_count_tz(UINT32_C(1) << i);
        zis_test_assert_eq(result, i);
        result = zis_bits_count_tz((UINT32_C(1) << i) | (UINT32_C(1) << 31));
        zis_test_assert_eq(result, i);
    }
}

zis_test0_define(bits_count_tz_u64) {
    for (unsigned int i = 1; i < 64; i++) {
        unsigned int result;
        result = zis_bits_count_tz(UINT64_C(1) << i);
        zis_test_assert_eq(result, i);
        result = zis_bits_count_tz((UINT64_C(1) << i) | (UINT64_C(1) << 63));
        zis_test_assert_eq(result, i);
    }
}

static void mem_fill_zero(void *mem, size_t mem_len) {
    memset(mem, 0, mem_len);
}

static void mem_fill_one(void *mem, size_t mem_len) {
    memset(mem, 0xff, mem_len);
}

static bool mem_all_zero(const void *mem, size_t mem_len) {
    const char *mem1 = (const char *)mem;
    for (size_t i = 0; i < mem_len; i++) {
        if (mem1[i])
            return false;
    }
    return true;
}

static bool mem_all_one(const void *mem, size_t mem_len) {
    const unsigned char *mem1 = (const unsigned char *)mem;
    for (size_t i = 0; i < mem_len; i++) {
        if (mem1[i] != 0xff)
            return false;
    }
    return true;
}

static bool num_in_array(size_t num, const size_t array[], size_t array_len) {
    for (size_t i = 0; i < array_len; i++) {
        if (num == array[i])
            return true;
    }
    return false;
}

zis_test0_define(bitset_clear) {
    char data[zis_bitset_required_size(256) * 2];
    const size_t data_size = sizeof data;
    const size_t half_data_size = data_size / 2;
    mem_fill_one(data, data_size);

    struct zis_bitset *const bitset = (struct zis_bitset *)data;
    const size_t bitset_size = half_data_size;
    zis_bitset_clear(bitset, bitset_size);

    zis_test_assert(mem_all_zero(data, half_data_size)); // First half is cleared.
    zis_test_assert(mem_all_one(data + half_data_size, half_data_size)); // Second half is untouched.
}

zis_test0_define(bitset_read_and_modify) {
    char data[3][zis_bitset_required_size(256)];
    struct zis_bitset *const bitset = (struct zis_bitset *)data[1];
    const size_t bitset_length = 256;
    mem_fill_one(data, sizeof data);

    for (size_t i = 0; i < bitset_length; i++) {
        mem_fill_zero(data[1], sizeof data[1]);

        zis_bitset_set_bit(bitset, i);
        zis_test_assert(!mem_all_zero(data[1], sizeof data[1]));

        for (size_t j = 0; j < bitset_length; j++) {
            const bool bit_is_set = i == j;
            zis_test_assert_eq(zis_bitset_test_bit(bitset, j), bit_is_set);
        }

        zis_bitset_reset_bit(bitset, i);
        zis_test_assert(mem_all_zero(data[1], sizeof data[1]));

        zis_bitset_try_set_bit(bitset, i);
        zis_test_assert(!mem_all_zero(data[1], sizeof data[1]));

        for (size_t j = 0; j < bitset_length; j++) {
            const bool bit_is_set = i == j;
            zis_test_assert_eq(zis_bitset_test_bit(bitset, j), bit_is_set);
        }

        zis_bitset_try_reset_bit(bitset, i);
        zis_test_assert(mem_all_zero(data[1], sizeof data[1]));
    }
}

zis_test0_define(bitset_foreach) {
    char data[zis_bitset_required_size(256)];
    struct zis_bitset *const bitset = (struct zis_bitset *)data;
    const size_t bitset_size = sizeof data;

    const size_t bit_indices[] = {0, 1, 2, 4, 8, 25, 100, 254, 255};
    const size_t bit_indices_len = sizeof bit_indices / sizeof bit_indices[0];

    zis_bitset_clear(bitset, bitset_size);
    for (size_t i = 0; i < bit_indices_len; i++)
        zis_bitset_set_bit(bitset, bit_indices[i]);

    size_t count = 0;
    zis_bitset_foreach_set(bitset, bitset_size, index, {
        zis_test_assert(num_in_array(index, bit_indices, bit_indices_len));
        count++;
    });
    zis_test_assert_eq(count, bit_indices_len);
}

zis_test0_list(
    core_bits,
    zis_test0_case(bits_count_tz_u32),
    zis_test0_case(bits_count_tz_u64),
    zis_test0_case(bitset_clear),
    zis_test0_case(bitset_read_and_modify),
    zis_test0_case(bitset_foreach),
)
