/// Bytecode instruction.

#pragma once

#include <stdint.h>

#include "oplist.h"

/// A unsigned integer type that holds an instruction.
typedef uint32_t zis_instr_word_t;

/// Instruction opcode.
enum zis_opcode {
#define E(CODE, NAME)  ZIS_OPC_##NAME = CODE ,
    ZIS_OP_LIST
#undef E
};

/// Instruction type (operands type).
enum zis_op_type {
    ZIS_OP_X,
    ZIS_OP_Aw,
    ZIS_OP_Asw,
    ZIS_OP_ABw,
    ZIS_OP_ABsw,
    ZIS_OP_ABC,
    ZIS_OP_ABsCs,
};

#define ZIS_INSTR_U25_MAX  ((1 << 25) - 1)
#define ZIS_INSTR_I25_MAX  ((1 << 24) - 1)
#define ZIS_INSTR_I25_MIN  (-(1 << 24))

#define ZIS_INSTR_U16_MAX  ((1 << 16) - 1)
#define ZIS_INSTR_I16_MAX  ((1 << 15) - 1)
#define ZIS_INSTR_I16_MIN  (-(1 << 15))

#define ZIS_INSTR_U9_MAX  ((1 << 9) - 1)
#define ZIS_INSTR_I9_MAX  ((1 << 8) - 1)
#define ZIS_INSTR_I9_MIN  (-(1 << 8))

#define ZIS_INSTR_U8_MAX  ((1 << 8) - 1)
#define ZIS_INSTR_I8_MAX  ((1 << 7) - 1)
#define ZIS_INSTR_I8_MIN  (-(1 << 7))

#define zis_instr_make_Aw(OP, Aw) \
    ((uint32_t)(OP) | (uint32_t)(Aw) << 7)

#define zis_instr_make_Asw(OP, Asw) \
    ((uint32_t)(OP) | (int32_t)(Asw) << 7)

#define zis_instr_make_ABw(OP, A, Bw) \
    ((uint32_t)(OP) | ((uint32_t)(A) & 0x1ff) << 7 | (uint32_t)(Bw) << 16)

#define zis_instr_make_ABsw(OP, A, Bsw) \
    ((uint32_t)(OP) | ((uint32_t)(A) & 0x1ff) << 7 | (int32_t)(Bsw) << 16)

#define zis_instr_make_ABC(OP, A, B, C) \
    ((uint32_t)(OP) | ((uint32_t)(A) & 0x1ff) << 7 | ((uint32_t)(B) & 0xff) << 16 | (uint32_t)(C) << 24)

#define zis_instr_make_ABsCs(OP, A, Bs, Cs) \
    ((uint32_t)(OP) | ((uint32_t)(A) & 0x1ff) << 7 | ((int32_t)(Bs) & 0xff) << 16 | (int32_t)(Cs) << 24)

#define zis_instr_extract_opcode(I) \
    ((I) & 0x7f)

#define zis_instr_extract_operands_Aw(I, Aw) \
do {                                         \
    Aw = (uint32_t)(I) >> 7;                 \
} while (0)

#define zis_instr_extract_operands_Asw(I, Asw) \
do {                                           \
    Asw = (int32_t)(I) >> 7;                   \
} while (0)

#define zis_instr_extract_operands_ABsw(I, A, Bsw) \
do {                                               \
    A = (uint32_t)(I) >> 7 & 0x1ff;                \
    Bsw = (int32_t)(I) >> 16;                      \
} while (0)

#define zis_instr_extract_operands_ABw(I, A, Bsw) \
do {                                              \
    A = (uint32_t)(I) >> 7 & 0x1ff;               \
    Bsw = (uint32_t)(I) >> 16;                    \
} while (0)

#define zis_instr_extract_operands_ABC(I, A, B, C) \
do {                                               \
    A = (uint32_t)(I) >> 7 & 0x1ff;                \
    B = (uint32_t)(I) >> 16 & 0xff;                \
    C = (uint32_t)(I) >> 24;                       \
} while (0)

#define zis_instr_extract_operands_ABsCs(I, A, Bs, Cs) \
do {                                                   \
    A = (uint32_t)(I) >> 7 & 0x1ff;                    \
    Bs = ((int32_t)(I) << 8) >> 24;                    \
    Cs = (int32_t)(I) >> 24;                           \
} while (0)
