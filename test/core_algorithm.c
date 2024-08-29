#include "test.h"

#include <math.h>

#include "../core/algorithm.c"

zis_test0_define(pow_u32) {
    const uint32_t OVERFLOW = 0;
    zis_test_assert_eq(zis_math_pow_u32(0, 0), 1);
    zis_test_assert_eq(zis_math_pow_u32(0, 1), 0);
    zis_test_assert_eq(zis_math_pow_u32(1, 1), 1);
    zis_test_assert_eq(zis_math_pow_u32(1, UINT32_MAX), 1);
    for (uint32_t i = 2; i < 500; i++) {
        for (uint32_t j = 0; ; j++) {
            double a = pow(i, j);
            uint32_t b = zis_math_pow_u32(i, j);
            if (a > (double)UINT32_MAX) {
                zis_test_assert_eq(b, OVERFLOW);
                break;
            } else {
                zis_test_assert_eq(b, a);
            }
        }
    }
    zis_test_assert_eq(zis_math_pow_u32(UINT32_MAX, 0), 1);
    zis_test_assert_eq(zis_math_pow_u32(UINT32_MAX, 1), UINT32_MAX);
}

zis_test0_list(
    core_algorithm,
    zis_test0_case(pow_u32),
)
