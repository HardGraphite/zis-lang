#include "assembly.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attributes.h"
#include "debug.h"
#include "locals.h"

/* ----- constants and lookup-tables ---------------------------------------- */

#if ZIS_FEATURE_ASM || ZIS_FEATURE_DIS || !defined(NDEBUG)

static const uint8_t op_types[] = {
#define E(CODE, NAME, TYPE)  [CODE] = (uint8_t)ZIS_OP_##TYPE ,
    ZIS_OP_LIST_FULL
#undef E
};

#define op_type_of(opcode) \
    (assert((int)(opcode) < 128), (enum zis_op_type)op_types[(int)(opcode)])

#define assert_op_type_(opcode, type_expr) \
    (assert(op_type_of(opcode) type_expr))

#else // !(ZIS_FEATURE_ASM || ZIS_FEATURE_DIS || !defined(NDEBUG))

#define assert_op_type_(opcode, type_expr)  ((void)0)

#endif // ZIS_FEATURE_ASM || ZIS_FEATURE_DIS || !defined(NDEBUG)

#if ZIS_FEATURE_ASM | ZIS_FEATURE_DIS

#pragma pack(push, 1)

static const char *const op_names_in_order[] = {
#define E(CODE, NAME, TYPE)  [CODE] = #NAME ,
    ZIS_OP_LIST_FULL
#undef E
};

static const char *const op_names_sorted[] = {
#define E(CODE, NAME, TYPE)  #NAME ,
    ZIS_OP_LIST_FULL
#undef E
};

static const uint8_t op_names_sorted_code_table[] = {
#define E(CODE, NAME, TYPE)  CODE ,
    ZIS_OP_LIST_FULL
#undef E
};

static const char *const pseudo_names[] = {
    "END",
    "FUNC",
    "TYPE",
    "CONST",
    "SYM",
};

#pragma pack(pop)

enum pseudo_opcode {
    PSEUDO_END,
    PSEUDO_FUNC,
    PSEUDO_TYPE,
    PSEUDO_CONST,
    PSEUDO_SYM,
};

/// Find opcode by its uppercase name. Returns -1 if not found.
static int opcode_from_name(const char *name_upper) {
    static_assert(sizeof(size_t) == sizeof(intptr_t), "");
    const size_t op_count = ZIS_OP_LIST_LEN;
    static_assert(ZIS_OP_LIST_LEN && ZIS_OP_LIST_LEN <= sizeof op_names_sorted / sizeof op_names_sorted[0], "");

    size_t index_l = 0, index_r = op_count - 1;
    do {
        const size_t index_m = index_l + (index_r - index_l) / 2;
        const char *const s = op_names_sorted[index_m];
        const int diff = strcmp(s, name_upper);
        if (diff == 0)
            return op_names_sorted_code_table[index_m];
        if (diff < 0)
            index_l = index_m + 1;
        else
            index_r = index_m - 1;
    } while ((intptr_t)index_l <= (intptr_t)index_r);

    return -1;
}

/// Find pseudo opcode by its uppercase name. Returns -1 if not found.
static int pseudo_from_name(const char *name_upper) {
    // TODO: use binary search?
    const size_t op_count = sizeof pseudo_names / sizeof pseudo_names[0];
    for (size_t i = 0; i < op_count; i++) {
        if (strcmp(pseudo_names[i], name_upper) == 0)
            return (int)i;
    }
    return -1;
}

#endif // ZIS_FEATURE_ASM | ZIS_FEATURE_DIS

/* ----- function assembler ------------------------------------------------- */

#if ZIS_FEATURE_ASM || ZIS_FEATURE_SRC

#include "context.h"
#include "globals.h"
#include "memory.h"
#include "objmem.h"

#include "arrayobj.h"
#include "funcobj.h"
#include "mapobj.h"

struct instr_buffer {
    zis_instr_word_t *data;
    size_t length, capacity;
};

static void instr_buffer_init(struct instr_buffer *ib) {
    ib->data = NULL;
    ib->length = 0;
    ib->capacity = 0;
}

static void instr_buffer_fini(struct instr_buffer *ib) {
    zis_mem_free(ib->data);
}

static void instr_buffer_clear(struct instr_buffer *ib) {
    ib->length = 0;
}

static void instr_buffer_append(struct instr_buffer *ib, zis_instr_word_t x) {
    assert(ib->length <= ib->capacity);
    if (zis_unlikely(ib->length == ib->capacity)) {
        const size_t new_cap = ib->capacity ? ib->capacity * 2 : 32;
        ib->data = zis_mem_realloc(ib->data, new_cap * sizeof(zis_instr_word_t));
        assert(ib->data);
        ib->capacity = new_cap;
    }
    ib->data[ib->length++] = x;
}

static void instr_buffer_insert(struct instr_buffer *ib, size_t pos, zis_instr_word_t x) {
    assert(pos <= ib->length);
    instr_buffer_append(ib, x);
    if (pos < ib->length) {
        zis_instr_word_t *const p = ib->data + pos;
        const size_t n = ib->length - pos - 1;
        memmove(p + 1, p, n * sizeof *p);
        *p = x;
    }
}

struct label_table {
    uint32_t *labels;
    size_t length, capacity;
};

static void label_table_init(struct label_table *lt) {
    lt->labels = NULL;
    lt->length = 0;
    lt->capacity = 0;
}

static void label_table_fini(struct label_table *lt) {
    zis_mem_free(lt->labels);
}

static int label_table_alloc(struct label_table *restrict lt) {
    assert(lt->length <= lt->capacity);
    if (zis_unlikely(lt->length == lt->capacity)) {
        const size_t new_cap = lt->capacity ? lt->capacity * 2 : 8;
        lt->labels = zis_mem_realloc(lt->labels, new_cap * sizeof(uint32_t));
        assert(lt->labels);
        lt->capacity = new_cap;
    }
    const size_t id = lt->length++;
    assert(id <= INT_MAX);
    return (int)id;
}

static uint32_t label_table_get(const struct label_table *lt, int id) {
    assert(lt->length <= lt->capacity);
    assert(id >= 0 && (size_t)id < lt->length);
    return lt->labels[id];
}

static uint32_t *label_table_ref(struct label_table *lt, int id) {
    assert(lt->length <= lt->capacity);
    assert(id >= 0 && (size_t)id < lt->length);
    return lt->labels + id;
}

static void label_table_clear(struct label_table *lt) {
    assert(lt->length <= lt->capacity);
    if (lt->labels) {
        assert(lt->capacity);
        memset(lt->labels, 0xff, sizeof(uint32_t) * lt->length);
        assert(lt->labels[0] == UINT32_MAX);
        lt->length = 0;
    }
}

static void label_table_shift(struct label_table *lt, uint32_t addr_start) {
    uint32_t *labels = lt->labels;
    for (size_t i = 0, n = lt->length; i < n; i++) {
        const uint32_t x = labels[i];
        if (x >= addr_start)
            labels[i] = x + 1U;
    }
}

struct jumpinstr_table_entry {
    uint32_t address;
    int label;
    zis_instr_word_t instr[2]; // { [0] = `instr`, [1] = `extended_instr` or `UINT32_MAX` }
};

static bool jumpinstr_table_entry_is_extended(const struct jumpinstr_table_entry *e) {
    return e->instr[1] != UINT32_MAX;
}

struct jumpinstr_table {
    struct jumpinstr_table_entry *data;
    size_t length, capacity;
};

static void jumpinstr_table_init(struct jumpinstr_table *jt) {
    jt->data = NULL;
    jt->length = 0;
    jt->capacity = 0;
}

static void jumpinstr_table_fini(struct jumpinstr_table *jt) {
    zis_mem_free(jt->data);
}

static void jumpinstr_table_clear(struct jumpinstr_table *jt) {
    jt->length = 0;
}

static unsigned int jumpinstr_table_add(
    struct jumpinstr_table *jt, uint32_t addr, zis_instr_word_t instr, int label
) {
    assert(jt->length <= jt->capacity);
    if (zis_unlikely(jt->length == jt->capacity)) {
        const size_t new_cap = jt->capacity ? jt->capacity * 2 : 4;
        jt->data = zis_mem_realloc(jt->data, new_cap * sizeof(struct jumpinstr_table));
        assert(jt->data);
        jt->capacity = new_cap;
    }
    size_t index = jt->length++;
    struct jumpinstr_table_entry *entry = &jt->data[index];
    entry->address = addr;
    entry->label = label;
    entry->instr[0] = instr, entry->instr[1] = UINT_MAX;
    assert(index <= UINT_MAX);
    return (unsigned int)index;
}

static struct jumpinstr_table_entry *jumpinstr_table_get(
    struct jumpinstr_table *jt, size_t index
) {
    assert(index < jt->length);
    return &jt->data[index];
}

static void jumpinstr_table_shift(struct jumpinstr_table *jt, uint32_t addr_start) {
    struct jumpinstr_table_entry *data = jt->data;
    for (size_t i = 0, n = jt->length; i < n; i++) {
        const uint32_t x = data[i].address;
        if (x >= addr_start)
            data[i].address = x + 1U;
    }
}

struct zis_assembler {
#define AS_OBJ_MEMBER_BEGIN func_constants
    struct zis_array_obj *func_constants;
    struct zis_map_obj *func_symbols; // { symbol -> id }
#define AS_OBJ_MEMBER_END instr_buffer
    struct instr_buffer instr_buffer;
    struct label_table  label_table;
    struct jumpinstr_table jumpinstr_table;
    struct zis_func_obj_meta func_meta;
    struct zis_assembler *_as_list_next;
};

/// GC objects visitor. See `zis_objmem_object_visitor_t`.
static void assembler_gc_visitor(void *_as, enum zis_objmem_obj_visit_op op) {
    for (struct zis_assembler *as = _as; as; as = as->_as_list_next) {
        void *begin = (char *)as + offsetof(struct zis_assembler, AS_OBJ_MEMBER_BEGIN);
        void *end   = (char *)as + offsetof(struct zis_assembler, AS_OBJ_MEMBER_END);
        zis_objmem_visit_object_vec(begin, end, op);
    }
}

struct zis_assembler *zis_assembler_create(
    struct zis_context *z, struct zis_assembler *parent /* = NULL */
) {
    struct zis_assembler *const as = zis_mem_alloc(sizeof(struct zis_assembler));

    as->func_constants = zis_object_cast(z->globals->val_nil, struct zis_array_obj);
    as->func_symbols = zis_object_cast(z->globals->val_nil, struct zis_map_obj);
    instr_buffer_init(&as->instr_buffer);
    label_table_init(&as->label_table);
    jumpinstr_table_init(&as->jumpinstr_table);
    as->func_meta.na = 0, as->func_meta.no = 0, as->func_meta.nr = 0;
    as->_as_list_next = NULL;

    if (parent) {
        assert(!parent->_as_list_next);
        parent->_as_list_next = as;
    } else {
        zis_objmem_add_gc_root(z, as, assembler_gc_visitor);
    }

    as->func_constants = zis_array_obj_new(z, NULL, 0);
    as->func_symbols = zis_map_obj_new(z, 1.5f, 0);

    return as;
}

void zis_assembler_destroy(
    struct zis_assembler *as,
    struct zis_context *z, struct zis_assembler *parent /* = NULL */
) {
    assert(as->_as_list_next == NULL);
    if (parent) {
        assert(parent->_as_list_next == as);
        parent->_as_list_next = NULL;
    } else {
        zis_objmem_remove_gc_root(z, as);
    }

    instr_buffer_fini(&as->instr_buffer);
    label_table_fini(&as->label_table);
    jumpinstr_table_fini(&as->jumpinstr_table);
    zis_mem_free(as);
}

void zis_assembler_clear(struct zis_assembler *as) {
    zis_array_obj_clear(as->func_constants);
    zis_map_obj_clear(as->func_symbols);
    instr_buffer_clear(&as->instr_buffer);
    label_table_clear(&as->label_table);
    jumpinstr_table_clear(&as->jumpinstr_table);
    as->func_meta.na = 0, as->func_meta.no = 0, as->func_meta.nr = 0;
}

static const enum zis_opcode _opposite_jump_instr_table[] = {
    [(unsigned)ZIS_OPC_JMPT  - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPF,
    [(unsigned)ZIS_OPC_JMPF  - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPT,
    [(unsigned)ZIS_OPC_JMPLE - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPGT,
    [(unsigned)ZIS_OPC_JMPLT - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPGE,
    [(unsigned)ZIS_OPC_JMPEQ - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPNE,
    [(unsigned)ZIS_OPC_JMPGT - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPLE,
    [(unsigned)ZIS_OPC_JMPGE - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPLT,
    [(unsigned)ZIS_OPC_JMPNE - (unsigned)ZIS_OPC_JMP - 1U] = ZIS_OPC_JMPEQ,
};

static enum zis_opcode _as_finish_opposite_jump_instr(enum zis_opcode opcode) {
    const unsigned int index = (unsigned int)opcode - (unsigned int)ZIS_OPC_JMP - 1U;
    assert(index < sizeof _opposite_jump_instr_table / sizeof opcode);
    return _opposite_jump_instr_table[index];
}

static int _as_finish_id_map_to_slots(struct zis_object *k, struct zis_object *v, void *_slots) {
    struct zis_array_slots_obj *slots = _slots;
    assert(zis_object_is_smallint(v));
    const zis_smallint_t id = zis_smallint_from_ptr(v);
    assert(id >= 0 && (size_t)id < zis_array_slots_obj_length(slots));
    zis_array_slots_obj_set(slots, (size_t)id, k);
    return 0;
}

struct zis_func_obj *zis_assembler_finish(
    struct zis_assembler *as,
    struct zis_context *z, struct zis_module_obj *_module
) {
    // Append a RETNIL instrcution at the end of the function.
    do {
        if (as->instr_buffer.length) {
            enum zis_opcode last_op = (enum zis_opcode)zis_instr_extract_opcode(
                as->instr_buffer.data[as->instr_buffer.length - 1]
            );
            if (last_op == ZIS_OPC_RET || last_op == ZIS_OPC_RETNIL || last_op == ZIS_OPC_THR)
                break;
        }
        zis_assembler_append_Aw(as, ZIS_OPC_RETNIL, 0);
    } while (false);

    // Fill the jump instructions and apply them to the bytecode.
    {
    _re_fill_jump_instr:;
        zis_instr_word_t *const instr_seq = as->instr_buffer.data;
        // Fill instructions in the jumpinstr_table.
        for (size_t jt_i = 0, jt_n = as->jumpinstr_table.length; jt_i < jt_n; jt_i++) {
            struct jumpinstr_table_entry *jt_entry =
                jumpinstr_table_get(&as->jumpinstr_table, jt_i);
            const uint32_t instr_i = jt_entry->address;
            assert(instr_i < as->instr_buffer.length);
            assert(zis_instr_extract_opcode(instr_seq[instr_i]) == _ZIS_OPC_COUNT);
            const int32_t jump_offset =
                (int32_t)(label_table_get(&as->label_table, jt_entry->label) - instr_i);
            enum zis_opcode opcode = zis_instr_extract_opcode(jt_entry->instr[0]);
            switch (op_type_of(opcode)) {
            case ZIS_OP_Asw:
                assert(!jumpinstr_table_entry_is_extended(jt_entry));
                if (jump_offset < ZIS_INSTR_I25_MIN || ZIS_INSTR_I25_MAX < jump_offset)
                    zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT);
                jt_entry->instr[0] = zis_instr_make_Asw(opcode, jump_offset);
                break;
            case ZIS_OP_AsBw:
            case ZIS_OP_AsBC:
                if (!jumpinstr_table_entry_is_extended(jt_entry)) {
                    uint32_t _operand0, operand; zis_unused_var(_operand0);
                    zis_instr_extract_operands_AsBw(jt_entry->instr[0], _operand0, operand);
                    if (jump_offset < ZIS_INSTR_I9_MIN || ZIS_INSTR_I9_MAX < jump_offset) {
                        opcode = _as_finish_opposite_jump_instr(opcode);
                        jt_entry->instr[0] = zis_instr_make_AsBw(opcode, 2, operand);
                        jt_entry->instr[1] = zis_instr_make_Asw(ZIS_OPC_JMP, jump_offset); // extended
                        assert(jumpinstr_table_entry_is_extended(jt_entry));
                        instr_buffer_insert(&as->instr_buffer, instr_i + 1, zis_instr_make_Asw(_ZIS_OPC_COUNT, jt_i));
                        label_table_shift(&as->label_table, instr_i + 1);
                        jumpinstr_table_shift(&as->jumpinstr_table, instr_i + 1);
                        goto _re_fill_jump_instr;
                    }
                    jt_entry->instr[0] = zis_instr_make_AsBw(opcode, jump_offset, operand);
                } else {
                    assert(zis_instr_extract_opcode(jt_entry->instr[1]) == ZIS_OPC_JMP);
                    if (jump_offset < ZIS_INSTR_I25_MIN || ZIS_INSTR_I25_MAX < jump_offset)
                        zis_context_panic(z, ZIS_CONTEXT_PANIC_ABORT);
                    jt_entry->instr[1] = zis_instr_make_Asw(ZIS_OPC_JMP, jump_offset);
                }
                break;
            default:
                zis_unreachable();
            }
        }
        // Copy instructions from the jumpinstr_table to the instr_buffer.
        for (size_t jt_i = 0, jt_n = as->jumpinstr_table.length; jt_i < jt_n; jt_i++) {
            struct jumpinstr_table_entry *jt_entry =
                jumpinstr_table_get(&as->jumpinstr_table, jt_i);
            const uint32_t instr_i = jt_entry->address;
            assert(instr_i < as->instr_buffer.length);
            assert(zis_instr_extract_opcode(instr_seq[instr_i]) == _ZIS_OPC_COUNT);
            instr_seq[instr_i] = jt_entry->instr[0];
            if (jumpinstr_table_entry_is_extended(jt_entry)) {
                assert(zis_instr_extract_opcode(instr_seq[instr_i + 1]) == _ZIS_OPC_COUNT);
                instr_seq[instr_i + 1] = jt_entry->instr[1];
            }
        }
    }

    // Create a function object from the bytecode.
    zis_locals_decl_1(z, var, struct zis_func_obj *func_obj);
    zis_locals_zero_1(var, func_obj);
    var.func_obj = zis_func_obj_new_bytecode(
        z, as->func_meta, as->instr_buffer.data, as->instr_buffer.length
    );
    zis_func_obj_set_module(z, var.func_obj, _module); // No GC should have been triggered before this.

    // Add constants & symbols to the function object.
    if (zis_array_obj_length(as->func_constants)) {
        struct zis_array_slots_obj *const tbl =
            zis_array_slots_obj_new2(z, zis_array_obj_length(as->func_constants), as->func_constants->_data);
        zis_func_obj_set_resources(var.func_obj, NULL, tbl);
    }
    if (zis_map_obj_length(as->func_symbols)) {
        struct zis_array_slots_obj *const tbl =
            zis_array_slots_obj_new(z, NULL, zis_map_obj_length(as->func_symbols));
        zis_func_obj_set_resources(var.func_obj, tbl, NULL);
        zis_map_obj_foreach(z, as->func_symbols, _as_finish_id_map_to_slots, tbl);
    }

    // Reset the assembler.
    zis_assembler_clear(as);

    // Dump the bytecode.
    zis_debug_log_1(DUMP, "Asm", "zis_debug_dump_bytecode()", fp, {
        zis_debug_dump_bytecode(z, var.func_obj, (uint32_t)-1, fp);
    });

    zis_locals_drop(z, var);
    return var.func_obj;
}

const struct zis_func_obj_meta *zis_assembler_func_meta(
    struct zis_assembler *as, const struct zis_func_obj_meta *restrict m
) {
    if (m) {
        as->func_meta.na = m->na, as->func_meta.no = m->no, as->func_meta.nr = m->nr;
    }
    return &as->func_meta;
}

unsigned int zis_assembler_func_constant(
    struct zis_assembler *as, struct zis_context *z, struct zis_object *v
) {
    // TODO: check whether the constant has been added.

    const size_t n = zis_array_obj_length(as->func_constants);
    zis_array_obj_append(z, as->func_constants, v);
    assert(n <= UINT_MAX);
    return (unsigned int)n;
}

unsigned int zis_assembler_func_symbol(
    struct zis_assembler *as, struct zis_context *z, struct zis_symbol_obj *v
) {
    struct zis_object *id_o = zis_map_obj_sym_get(as->func_symbols, v);
    unsigned int id;
    if (id_o) {
        assert(zis_object_is_smallint(id_o));
        const zis_smallint_t id_smi = zis_smallint_from_ptr(id_o);
        assert(id_smi >= 0 && id_smi <= UINT_MAX);
        id = (unsigned int)id_smi;
    } else {
        const size_t n = zis_map_obj_length(as->func_symbols);
        assert(n <= ZIS_SMALLINT_MAX);
        zis_map_obj_sym_set(z, as->func_symbols, v, zis_smallint_to_ptr((zis_smallint_t)n));
        assert(n <= UINT_MAX);
        id = (unsigned int)n;
    }
    return id;
}

int zis_assembler_alloc_label(struct zis_assembler *as) {
    const int id = label_table_alloc(&as->label_table);
    zis_debug_log(TRACE, "Asm", "new label #%i", id);
    return id;
}

int zis_assembler_place_label(struct zis_assembler *as, int id) {
    const size_t addr = as->instr_buffer.length;
    assert(addr <= UINT32_MAX);
    *label_table_ref(&as->label_table, id) = (uint32_t)addr;
    zis_debug_log(TRACE, "Asm", "place label #%i at +%zu", id, addr);
    return id;
}

void zis_assembler_append(struct zis_assembler *as, zis_instr_word_t instr) {
    zis_debug_log(TRACE, "Asm", "append instruction %08x", instr);
    instr_buffer_append(&as->instr_buffer, instr);
}

void zis_assembler_append_Aw(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t Aw
) {
    assert_op_type_(opcode, == ZIS_OP_Aw);
    zis_assembler_append(as, zis_instr_make_Aw(opcode, Aw));
}

void zis_assembler_append_Asw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int32_t Asw
) {
    assert_op_type_(opcode, == ZIS_OP_Asw);
    zis_assembler_append(as, zis_instr_make_Asw(opcode, Asw));
}

void zis_assembler_append_ABw(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, uint32_t Bw
) {
    assert_op_type_(opcode, == ZIS_OP_ABw);
    zis_assembler_append(as, zis_instr_make_ABw(opcode, A, Bw));
}

void zis_assembler_append_AsBw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int32_t As, uint32_t Bw
) {
    assert_op_type_(opcode, == ZIS_OP_AsBw);
    zis_assembler_append(as, zis_instr_make_AsBw(opcode, As, Bw));
}

void zis_assembler_append_ABsw(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, int32_t Bsw
) {
    assert_op_type_(opcode, == ZIS_OP_ABsw);
    zis_assembler_append(as, zis_instr_make_ABsw(opcode, A, Bsw));
}

void zis_assembler_append_ABC(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, uint32_t B, uint32_t C
) {
    assert_op_type_(opcode, == ZIS_OP_ABC);
    zis_assembler_append(as, zis_instr_make_ABC(opcode, A, B, C));
}

void zis_assembler_append_AsBC(
    struct zis_assembler *as, enum zis_opcode opcode,
    int32_t As, uint32_t B, uint32_t C
) {
    assert_op_type_(opcode, == ZIS_OP_AsBC);
    zis_assembler_append(as, zis_instr_make_AsBC(opcode, As, B, C));
}

void zis_assembler_append_ABsCs(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, int32_t Bs, int32_t Cs
) {
    assert_op_type_(opcode, == ZIS_OP_ABsCs);
    zis_assembler_append(as, zis_instr_make_ABsCs(opcode, A, Bs, Cs));
}

void zis_assembler_append_jump_Asw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int label
) {
    assert_op_type_(opcode, == ZIS_OP_Asw);
    const zis_instr_word_t instr = zis_instr_make_Asw(opcode, 0);
    const uint32_t addr = (uint32_t)as->instr_buffer.length;
    const unsigned int ji = jumpinstr_table_add(&as->jumpinstr_table, addr, instr, label);
    zis_assembler_append(as, zis_instr_make_Aw(_ZIS_OPC_COUNT, ji));
}

void zis_assembler_append_jump_AsBw(
    struct zis_assembler *as, enum zis_opcode opcode,
    int label, uint32_t Bw
) {
    assert_op_type_(opcode, == ZIS_OP_AsBw);
    const zis_instr_word_t instr = zis_instr_make_AsBw(opcode, 0, Bw);
    const uint32_t addr = (uint32_t)as->instr_buffer.length;
    const unsigned int ji = jumpinstr_table_add(&as->jumpinstr_table, addr, instr, label);
    zis_assembler_append(as, zis_instr_make_Aw(_ZIS_OPC_COUNT, ji));
}

void zis_assembler_append_jump_AsBC(
    struct zis_assembler *as, enum zis_opcode opcode,
    int label, uint32_t B, uint32_t C
) {
    assert_op_type_(opcode, == ZIS_OP_AsBC);
    const zis_instr_word_t instr = zis_instr_make_AsBC(opcode, 0, B, C);
    const uint32_t addr = (uint32_t)as->instr_buffer.length;
    const unsigned int ji = jumpinstr_table_add(&as->jumpinstr_table, addr, instr, label);
    zis_assembler_append(as, zis_instr_make_Aw(_ZIS_OPC_COUNT, ji));
}

#endif // ZIS_FEATURE_ASM || ZIS_FEATURE_SRC

/* ----- module assembler --------------------------------------------------- */

#if ZIS_FEATURE_ASM

#include "stack.h"
#include "strutil.h"

#include "arrayobj.h"
#include "exceptobj.h"
#include "floatobj.h"
#include "intobj.h"
#include "moduleobj.h"
#include "streamobj.h"
#include "stringobj.h"
#include "symbolobj.h"

struct tas_context {
    struct zis_context *z;
    struct zis_stream_obj *input;
    struct zis_module_obj **module_ref;
    unsigned int line_number;
    char line_buffer[128];
};

zis_cold_fn static void tas_record_error(struct tas_context *tas, const char *s) {
    snprintf(tas->line_buffer, sizeof tas->line_buffer, "line %u: %s", tas->line_number, s);
}

zis_cold_fn static struct zis_exception_obj * tas_error_exception(struct tas_context *tas) {
    return zis_exception_obj_format(tas->z, "syntax", NULL, "%s", tas->line_buffer);
}

enum tas_parse_line_status {
    TAS_PARSE_INSTR,
    TAS_PARSE_PSEUDO,
    TAS_PARSE_ERROR,
    TAS_PARSE_EOF,
};

union tas_parse_line_result {
    struct { enum zis_opcode opcode; size_t operand_count; int32_t operands[3]; } instr;
    struct { enum pseudo_opcode opcode; const char *operands; } pseudo;
};

static enum tas_parse_line_status tas_parse_line(
    struct tas_context *restrict tas, union tas_parse_line_result *restrict res
) {
    char *const line_buffer = tas->line_buffer;
    const size_t line_buffer_size = sizeof tas->line_buffer;

    while (true) {
        const size_t line_len =
            zis_stream_obj_read_line(tas->input, line_buffer, line_buffer_size - 1);
        if (!line_len) {
            return TAS_PARSE_EOF;
        }
        tas->line_number++;
        if (line_buffer[line_len - 1] != '\n') {
            tas_record_error(tas, "the line is too long");
            return TAS_PARSE_ERROR;
        }
        line_buffer[line_len - 1] = 0;

        const char *const spaces = " \t\v";
        char *p = line_buffer;

        p += strspn(p, spaces);
        char *const  op_name = p;
        const size_t op_name_len = strcspn(p, spaces);
        p += strspn(p + op_name_len, spaces) + op_name_len;
        op_name[op_name_len] = '\0';
        zis_str_toupper(op_name, op_name_len);
        if (op_name[0] == '#' || op_name[0] == '\n' || !op_name[0])
            continue;

        if (op_name[0] == '.') {
            const int opcode = pseudo_from_name(op_name + 1);
            if (opcode == -1) {
                tas_record_error(tas, "unrecognized pseudo operation name");
                return TAS_PARSE_ERROR;
            }
            res->pseudo.opcode = (enum pseudo_opcode)opcode;
            res->pseudo.operands = p;
            return TAS_PARSE_PSEUDO;
        }

        const int opcode = opcode_from_name(op_name);
        if (opcode == -1) {
            tas_record_error(tas, "unrecognized operation name");
            return TAS_PARSE_ERROR;
        }
        res->instr.opcode = (enum zis_opcode)opcode;
        int scan_len[3] = { 0, 0, 0 };
        const int scan_ret = sscanf(
            p, "%" SCNi32 "%n,%" SCNi32 "%n,%" SCNi32 "%n",
            res->instr.operands + 0, scan_len + 0,
            res->instr.operands + 1, scan_len + 1,
            res->instr.operands + 2, scan_len + 2
        );
        if (scan_ret <= 0) {
            tas_record_error(tas, "illegal operands");
            return TAS_PARSE_ERROR;
        }
        res->instr.operand_count = (size_t)scan_ret;
        assert(scan_ret <= 3);
        p += strspn(p + scan_len[scan_ret - 1], spaces) + scan_len[scan_ret - 1];
        if (*p && *p != '#') {
            tas_record_error(tas, "unexpected trailing junk");
            return TAS_PARSE_ERROR;
        }
        return TAS_PARSE_INSTR;
    }
}

static struct zis_func_obj *tas_parse_func(
    struct tas_context *restrict tas,
    const char *pseudo_func_operands,
    struct zis_assembler *restrict as
) {
    struct zis_func_obj_meta func_meta;
    if (
        sscanf(
            pseudo_func_operands, "%hhu,%hhi,%hu",
            &func_meta.na, (signed char *)&func_meta.no, &func_meta.nr
        ) != 3
    ) {
        tas_record_error(tas, "illegal operands");
        return NULL;
    }
    zis_assembler_func_meta(as, &func_meta);

    while (true) {
        union tas_parse_line_result line_result;
        enum tas_parse_line_status line_status = tas_parse_line(tas, &line_result);
        if (line_status == TAS_PARSE_INSTR) {
            switch (op_type_of(line_result.instr.opcode)) {
                // FIXME: ranges of operands are not checked.
            case ZIS_OP_Aw:
                if (line_result.instr.operand_count != 1)
                    goto bad_operands;
                zis_assembler_append_Aw(
                    as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0]
                );
                break;
            case ZIS_OP_Asw:
                if (line_result.instr.operand_count != 1)
                    goto bad_operands;
                zis_assembler_append_Asw(
                    as, line_result.instr.opcode,
                    line_result.instr.operands[0]
                );
                break;
            case ZIS_OP_ABw:
                if (line_result.instr.operand_count != 2)
                    goto bad_operands;
                zis_assembler_append_ABw(
                    as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    (uint32_t)line_result.instr.operands[1]
                );
                break;
            case ZIS_OP_AsBw:
                if (line_result.instr.operand_count != 2)
                    goto bad_operands;
                zis_assembler_append_AsBw(
                    as, line_result.instr.opcode,
                    line_result.instr.operands[0],
                    (uint32_t)line_result.instr.operands[1]
                );
                break;
            case ZIS_OP_ABsw:
                if (line_result.instr.operand_count != 2)
                    goto bad_operands;
                zis_assembler_append_ABsw(
                    as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    line_result.instr.operands[1]
                );
                break;
            case ZIS_OP_ABC:
                if (line_result.instr.operand_count != 3)
                    goto bad_operands;
                zis_assembler_append_ABC(
                    as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    (uint32_t)line_result.instr.operands[1],
                    (uint32_t)line_result.instr.operands[2]
                );
                break;
            case ZIS_OP_AsBC:
                if (line_result.instr.operand_count != 3)
                    goto bad_operands;
                zis_assembler_append_AsBC(
                    as, line_result.instr.opcode,
                    line_result.instr.operands[0],
                    (uint32_t)line_result.instr.operands[1],
                    (uint32_t)line_result.instr.operands[2]
                );
                break;
            case ZIS_OP_ABsCs:
                if (line_result.instr.operand_count != 3)
                    goto bad_operands;
                zis_assembler_append_ABsCs(
                    as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    line_result.instr.operands[1],
                    line_result.instr.operands[2]
                );
                break;
            default:
                zis_context_panic(NULL, ZIS_CONTEXT_PANIC_ABORT);
            bad_operands:
                tas_record_error(tas, "illegal operands");
                return NULL;
            }
        } else if (line_status == TAS_PARSE_PSEUDO) {
            switch (line_result.pseudo.opcode) {
            case PSEUDO_END:
                goto finish;
            case PSEUDO_FUNC: {
                struct zis_assembler *as1 = zis_assembler_create(tas->z, as);
                struct zis_object *f = zis_object_from(
                    tas_parse_func(tas, line_result.pseudo.operands, as1)
                );
                zis_assembler_destroy(as1, tas->z, as);
                if (!f)
                    return NULL;
                zis_assembler_func_constant(as, tas->z, f);
                break;
            }
            case PSEUDO_CONST: {
                if (line_result.pseudo.operands[1] != ':')
                    goto pseudo_bad_operand;
                struct zis_object *v;
                switch (toupper(line_result.pseudo.operands[0])) {
                case 'I':
                    v = zis_int_obj_or_smallint(
                        tas->z, atoll(line_result.pseudo.operands + 2)
                    );
                    break;
                case 'F':
                    v = zis_object_from(zis_float_obj_new(
                        tas->z, atof(line_result.pseudo.operands + 2)
                    ));
                    break;
                case 'S':
                    v = zis_object_from(zis_string_obj_new(
                        tas->z, line_result.pseudo.operands + 2, (size_t)-1
                    ));
                    break;
                default:
                pseudo_bad_operand:
                    tas_record_error(tas, "illegal operands");
                    return NULL;
                }
                zis_assembler_func_constant(as, tas->z, v);
                break;
            }
            case PSEUDO_SYM:
                zis_assembler_func_symbol(
                    as, tas->z,
                    zis_symbol_registry_get(tas->z, line_result.pseudo.operands, (size_t)-1)
                );
                break;
            default:
                tas_record_error(tas, "unexpected pseudo operation");
                return NULL;
            }
        } else {
            if (line_status == TAS_PARSE_EOF)
                tas_record_error(tas, "unexpected EOF");
            return NULL;
        }
    }
finish:
    return zis_assembler_finish(as, tas->z, *tas->module_ref);
}

struct zis_func_obj *zis_assemble_func_from_text(
    struct zis_context *z, struct zis_stream_obj *input,
    struct zis_module_obj *_module
) {
    // NOTE: `input`(zis_stream_obj) won't be moved during GC.

    zis_locals_decl(
        z, var,
        struct zis_stream_obj *input;
        struct zis_module_obj *module;
    );
    var.input = input, var.module = _module;

    struct tas_context context = {
        .z = z,
        .input = input,
        .module_ref = &var.module,
        .line_number = 0,
    };

    struct zis_exception_obj *exc_obj = NULL;
    struct zis_func_obj *func_obj = NULL;

    union tas_parse_line_result line_result;
    enum tas_parse_line_status line_status = tas_parse_line(&context, &line_result);
    if (line_status != TAS_PARSE_PSEUDO || line_result.pseudo.opcode != PSEUDO_FUNC) {
        tas_record_error(&context, "expecting .FUNC");
        exc_obj = tas_error_exception(&context);
    } else {
        struct zis_assembler *as = zis_assembler_create(z, NULL);
        func_obj = tas_parse_func(&context, line_result.pseudo.operands, as);
        zis_assembler_destroy(as, z, NULL);
        if (!func_obj)
            exc_obj = tas_error_exception(&context);
    }

    zis_locals_drop(z, var);
    if (func_obj)
        return func_obj;
    zis_context_set_reg0(z, zis_object_from(exc_obj));
    return NULL;
}

#endif // ZIS_FEATURE_ASM

/* ----- function & module disassembler ------------------------------------- */

#if ZIS_FEATURE_DIS

static void dump_instr(
    zis_instr_word_t instr, struct zis_disassemble_result *restrict result
) {
    result->instr = instr;

    const unsigned int opcode = zis_instr_extract_opcode(instr);
    result->opcode = (enum zis_opcode)opcode;
    result->op_name = opcode < ZIS_OP_LIST_MAX_LEN ? op_names_in_order[opcode] : "";

    switch (op_type_of(opcode)) {
        uint32_t u[3];
    case ZIS_OP_Aw:
        zis_instr_extract_operands_Aw(instr, u[0]);
        result->operands[0] = (int32_t)u[0],
        result->operands[1] = INT32_MIN, result->operands[2] = INT32_MIN;
        break;
    case ZIS_OP_Asw:
        zis_instr_extract_operands_Asw(instr, result->operands[0]);
        result->operands[1] = INT32_MIN, result->operands[2] = INT32_MIN;
        break;
    case ZIS_OP_ABw:
        zis_instr_extract_operands_ABw(instr, u[0], u[1]);
        result->operands[0] = (int32_t)u[0], result->operands[1] = (int32_t)u[1],
        result->operands[2] = INT32_MIN;
        break;
    case ZIS_OP_AsBw:
        zis_instr_extract_operands_AsBw(instr, result->operands[0], u[1]);
        result->operands[1] = (int32_t)u[1], result->operands[2] = INT32_MIN;
        break;
    case ZIS_OP_ABsw:
        zis_instr_extract_operands_ABsw(instr, u[0], result->operands[1]);
        result->operands[0] = (int32_t)u[0], result->operands[2] = INT32_MIN;
        break;
    case ZIS_OP_ABC:
        zis_instr_extract_operands_ABC(instr, u[0], u[1], u[2]);
        result->operands[0] = (int32_t)u[0], result->operands[1] = (int32_t)u[1],
        result->operands[2] = (int32_t)u[2];
        break;
    case ZIS_OP_AsBC:
        zis_instr_extract_operands_AsBC(instr, result->operands[0], u[1], u[2]);
        result->operands[1] = (int32_t)u[1], result->operands[2] = (int32_t)u[2];
        break;
    case ZIS_OP_ABsCs:
        zis_instr_extract_operands_ABsCs(instr, u[0], result->operands[1], result->operands[2]);
        result->operands[0] = (int32_t)u[0];
        break;
    default:
        result->operands[0] = 0,
        result->operands[1] = INT32_MIN, result->operands[2] = INT32_MIN;
        break;
    }
}

int zis_disassemble_bytecode(
    struct zis_context *z, const struct zis_func_obj *_func_obj,
    int (*fn)(const struct zis_disassemble_result *, void *), void *fn_arg
) {
    int fn_ret = 0;
    struct zis_disassemble_result dis_res;

    zis_locals_decl_1(z, var, const struct zis_func_obj *func_obj);
    var.func_obj = _func_obj;

    for (size_t i = 0, n = zis_func_obj_bytecode_length(var.func_obj); i < n; i++) {
        const zis_instr_word_t instr = var.func_obj->bytecode[i];
        dump_instr(instr, &dis_res);
        dis_res.address = (uint32_t)i;
        if ((fn_ret = fn(&dis_res, fn_arg)))
            return fn_ret;
    }

    zis_locals_drop(z, var);
    return fn_ret;
}

#if ZIS_DEBUG_LOGGING

struct _debug_dump_bytecode_state {
    FILE *stream;
    uint32_t highlight_address;
};

static int _debug_dump_bytecode_fn(const struct zis_disassemble_result *dis, void *arg) {
    struct _debug_dump_bytecode_state *const state = arg;
    char buffer[80];
    int buffer_used = 0, n;
    n = snprintf(
        buffer, sizeof buffer, "%04x%s%08x  %-6s %i",
        dis->address, dis->address == state->highlight_address ? "==>" : ":  ", dis->instr,
        *dis->op_name ? dis->op_name : "??", dis->operands[0]
    );
    assert(n > 0);
    buffer_used += n;
    for (int i = 1; i <= 2 && dis->operands[i] != INT32_MIN; i++) {
        assert(sizeof buffer > (size_t)buffer_used);
        n = snprintf(buffer + buffer_used, sizeof buffer - buffer_used, ", %i", dis->operands[i]);
        assert(n > 0);
        buffer_used += n;
    }
    buffer[buffer_used++] = '\n';
    fwrite(buffer, 1, (size_t)buffer_used, state->stream);
    return 0;
}

void zis_debug_dump_bytecode(
    struct zis_context *z, const struct zis_func_obj *func_obj,
    uint32_t highlight_offset, void *restrict FILE_p
) {
    FILE *const restrict fp = FILE_p;
    struct _debug_dump_bytecode_state state = {
        .stream = fp,
        .highlight_address = highlight_offset,
    };
    fprintf(fp, "# disassembly of function@%p\n", (void *)func_obj);
    fprintf(
        fp, "# meta = {.na = %u, .no = %u, .nr = %u}\n# constants.len = %zu, symbols.len = %zu\n",
        func_obj->meta.na, func_obj->meta.no, func_obj->meta.nr,
        zis_array_slots_obj_length(func_obj->_constants),
        zis_array_slots_obj_length(func_obj->_symbols)
    );
    zis_disassemble_bytecode(z, func_obj, _debug_dump_bytecode_fn, &state);
    fprintf(fp, "# end of function@%p\n", (void *)func_obj);
}

#endif // ZIS_DEBUG_LOGGING

#endif // ZIS_FEATURE_DIS
