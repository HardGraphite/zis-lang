#include "test.h"

#include <assert.h> // static_assert()

#include "../core/instr.h"

zis_test0_define(check_num_min_and_max) {
    static_assert(ZIS_INSTR_U16_MAX == UINT16_MAX, "");
    static_assert(ZIS_INSTR_I16_MAX == INT16_MAX , "");
    static_assert(ZIS_INSTR_I16_MIN == INT16_MIN , "");
    static_assert(ZIS_INSTR_U8_MAX == UINT8_MAX, "");
    static_assert(ZIS_INSTR_I8_MAX == INT8_MAX , "");
    static_assert(ZIS_INSTR_I8_MIN == INT8_MIN , "");
}

zis_test0_define(make_and_extract_Aw) {
    for (uint32_t A = 0; A <= ZIS_INSTR_U25_MAX; A++) {
        uint32_t instr = zis_instr_make_Aw(0, A);
        uint32_t x;
        zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
        zis_instr_extract_operands_Aw(instr, x);
        zis_test_assert_eq(A, x);
    }
}

zis_test0_define(make_and_extract_Asw) {
    for (int32_t A = ZIS_INSTR_I25_MIN; A <= ZIS_INSTR_I25_MAX; A++) {
        uint32_t instr = zis_instr_make_Asw(0, A);
        int32_t x;
        zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
        zis_instr_extract_operands_Asw(instr, x);
        zis_test_assert_eq(A, x);
    }
}

zis_test0_define(make_and_extract_ABw) {
    for (uint32_t A = 0; A <= ZIS_INSTR_U9_MAX; A++) {
        for (uint32_t B = 0; B <= ZIS_INSTR_U16_MAX; B++) {
            uint32_t instr = zis_instr_make_ABw(0, A, B);
            uint32_t x, y;
            zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
            zis_instr_extract_operands_ABw(instr, x, y);
            zis_test_assert_eq(A, x);
            zis_test_assert_eq(B, y);
        }
    }
}

zis_test0_define(make_and_extract_AsBw) {
    for (int32_t A = ZIS_INSTR_I9_MAX; A <= ZIS_INSTR_I9_MAX; A++) {
        for (uint32_t B = 0; B <= ZIS_INSTR_U16_MAX; B++) {
            uint32_t instr = zis_instr_make_AsBw(0, A, B);
            int32_t x; uint32_t y;
            zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
            zis_instr_extract_operands_AsBw(instr, x, y);
            zis_test_assert_eq(A, x);
            zis_test_assert_eq(B, y);
        }
    }
}

zis_test0_define(make_and_extract_ABsw) {
    for (uint32_t A = 0; A <= ZIS_INSTR_U9_MAX; A++) {
        for (int32_t B = ZIS_INSTR_I16_MIN; B <= ZIS_INSTR_I16_MAX; B++) {
            uint32_t instr = zis_instr_make_ABsw(0, A, B);
            uint32_t x; int32_t y;
            zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
            zis_instr_extract_operands_ABsw(instr, x, y);
            zis_test_assert_eq(A, x);
            zis_test_assert_eq(B, y);
        }
    }
}

zis_test0_define(make_and_extract_ABC) {
    for (uint32_t A = 0; A <= ZIS_INSTR_U9_MAX; A++) {
        for (uint32_t B = 0; B <= ZIS_INSTR_U8_MAX; B++) {
            for (uint32_t C = 0; C <= ZIS_INSTR_U8_MAX; C++) {
                uint32_t instr = zis_instr_make_ABC(0, A, B, C);
                uint32_t x, y, z;
                zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
                zis_instr_extract_operands_ABC(instr, x, y, z);
                zis_test_assert_eq(A, x);
                zis_test_assert_eq(B, y);
                zis_test_assert_eq(C, z);
            }
        }
    }
}

zis_test0_define(make_and_extract_AsBC) {
    for (int32_t A = ZIS_INSTR_I9_MIN; A <= ZIS_INSTR_I9_MAX; A++) {
        for (uint32_t B = 0; B <= ZIS_INSTR_U8_MAX; B++) {
            for (uint32_t C = 0; C <= ZIS_INSTR_U8_MAX; C++) {
                uint32_t instr = zis_instr_make_AsBC(0, A, B, C);
                int32_t x; uint32_t y, z;
                zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
                zis_instr_extract_operands_AsBC(instr, x, y, z);
                zis_test_assert_eq(A, x);
                zis_test_assert_eq(B, y);
                zis_test_assert_eq(C, z);
            }
        }
    }
}

zis_test0_define(make_and_extract_ABsCs) {
    for (uint32_t A = 0; A <= ZIS_INSTR_U9_MAX; A++) {
        for (int32_t B = ZIS_INSTR_I8_MIN; B <= ZIS_INSTR_I8_MAX; B++) {
            for (int32_t C = ZIS_INSTR_I8_MIN; C <= ZIS_INSTR_I8_MAX; C++) {
                uint32_t instr = zis_instr_make_ABsCs(0, A, B, C);
                uint32_t x; int32_t y, z;
                zis_test_assert_eq(0, zis_instr_extract_opcode(instr));
                zis_instr_extract_operands_ABsCs(instr, x, y, z);
                zis_test_assert_eq(A, x);
                zis_test_assert_eq(B, y);
                zis_test_assert_eq(C, z);
            }
        }
    }
}

zis_test0_list(
    core_instr,
    check_num_min_and_max,
    make_and_extract_Aw,
    make_and_extract_Asw,
    make_and_extract_ABw,
    make_and_extract_AsBw,
    make_and_extract_ABsw,
    make_and_extract_ABC,
    make_and_extract_AsBC,
    make_and_extract_ABsCs,
)
