/// The assembly code support.

#pragma once

#include <stdint.h>

#include "instr.h"

#include "zis_config.h" // ZIS_FEATURE_ASM, ZIS_FEATURE_DIS, ZIS_FEATURE_SRC

struct zis_context;
struct zis_func_obj;
struct zis_func_obj_meta;
struct zis_module_obj;
struct zis_object;
struct zis_stream_obj;
struct zis_symbol_obj;

/* ----- function assembler ------------------------------------------------- */

#if ZIS_FEATURE_ASM || ZIS_FEATURE_SRC

/// The function bytecode assembler.
struct zis_assembler;

/// Create an assembler. The parameter `parent` is optional but recommended.
/// One assembler can have at most one child.
struct zis_assembler *zis_assembler_create(
    struct zis_context *z, struct zis_assembler *parent /* = NULL */
);

/// Delete an assembler. The parameter `parent` must be the same with
/// that passed to `zis_assembler_create()`. One assembler must have no child
/// when being destroyed.
void zis_assembler_destroy(
    struct zis_assembler *as,
    struct zis_context *z, struct zis_assembler *parent /* = NULL */
);

/// Clear the assembling data and reset the assembler.
void zis_assembler_clear(struct zis_assembler *as);

/// Finish the assembling and output the generated function.
/// `zis_assembler_clear()` will be called.
struct zis_func_obj *zis_assembler_finish(
    struct zis_assembler *as,
    struct zis_context *z, struct zis_module_obj *module
);

/// Get or update the function meta. `m` is optional.
const struct zis_func_obj_meta *zis_assembler_func_meta(
    struct zis_assembler *as, const struct zis_func_obj_meta *restrict m
);

/// Register or find a function-scope constant. Returns the ID.
unsigned int zis_assembler_func_constant(
    struct zis_assembler *as, struct zis_context *z, struct zis_object *v
);

/// Register or find a function-scope symbol. Returns the ID.
unsigned int zis_assembler_func_symbol(
    struct zis_assembler *as, struct zis_context *z, struct zis_symbol_obj *v
);

/// Allocate a label for jump targets. Returns the label ID.
int zis_assembler_alloc_label(struct zis_assembler *as);

/// Set the location of the label and return `id`.
/// The label must not be placed before.
int zis_assembler_place_label(struct zis_assembler *as, int id);

/// Append an instruction.
void zis_assembler_append(struct zis_assembler *as, zis_instr_word_t instr);

void zis_assembler_append_Aw(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t Aw
);

void zis_assembler_append_Asw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int32_t Asw
);

void zis_assembler_append_ABw(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, uint32_t Bw
);

void zis_assembler_append_AsBw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int32_t As, uint32_t Bw
);

void zis_assembler_append_ABsw(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, int32_t Bsw
);

void zis_assembler_append_ABC(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, uint32_t B, uint32_t C
);

void zis_assembler_append_AsBC(
    struct zis_assembler *as, enum zis_opcode opcode,
    int32_t As, uint32_t B, uint32_t C
);

void zis_assembler_append_ABsCs(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, int32_t Bs, int32_t Cs
);

void zis_assembler_append_jump_Asw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int label
);

void zis_assembler_append_jump_AsBw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int label, uint32_t Bw
);

void zis_assembler_append_jump_AsBC(
    struct zis_assembler *as, enum zis_opcode opcode,
    int label, uint32_t B, uint32_t C
);

#endif // ZIS_FEATURE_ASM || ZIS_FEATURE_SRC

/* ----- module assembler --------------------------------------------------- */

#if ZIS_FEATURE_ASM

/// Generate a function from the assemble text stream.
/// On failure, formats an exception (REG-0) and returns NULL.
struct zis_func_obj *zis_assemble_func_from_text(
    struct zis_context *z, struct zis_stream_obj *input,
    struct zis_module_obj *module
);

#endif // ZIS_FEATURE_ASM

/* ----- function disassembler ---------------------------------------------- */

#if ZIS_FEATURE_DIS

/// Disassemble result of one instruction.
struct zis_disassemble_result {
    unsigned int      address; ///< Instruction index.
    zis_instr_word_t  instr;   ///< The instruction.
    enum zis_opcode   opcode;  ///< Opcode.
    const char       *op_name; ///< Operation name.
    int32_t           operands[3]; ///< Operands. Unused ones are assigned with INT32_MIN.
};

/// Disassemble the bytecode function.
int zis_disassemble_bytecode(
    struct zis_context *z, const struct zis_func_obj *func_obj,
    int (*fn)(const struct zis_disassemble_result *, void *), void *fn_arg
);

#endif // ZIS_FEATURE_DIS
