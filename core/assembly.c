#include "assembly.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "attributes.h"
#include "debug.h"

/* ----- constants and lookup-tables ---------------------------------------- */

#if ZIS_FEATURE_ASM || ZIS_FEATURE_DIS || !defined(NDEBUG)

static const uint8_t op_types[] = {
#define E(CODE, NAME, TYPE)  [CODE] = (uint8_t)ZIS_OP_##TYPE ,
    ZIS_OP_LIST_FULL
#undef E
};

#define op_type_of(opcode) \
    (assert((int)opcode < 128), (enum zis_op_type)op_types[(int)opcode])

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
};

#pragma pack(pop)

enum pseudo_opcode {
    PSEUDO_END,
    PSEUDO_FUNC,
    PSEUDO_TYPE,
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

struct label_table {
    uint32_t *labels; // UINT32_MAX for unused.
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
    if (lt->length < lt->capacity) {
        const size_t id = lt->length++;
        assert(id <= INT_MAX);
        return (int)id;
    }
    for (size_t i = 0, n = lt->capacity; i < n; i++) {
        if (lt->labels[i] == UINT32_MAX) {
            lt->labels[i] = 0;
            assert(i <= INT_MAX);
            return (int)i;
        }
    }
    {
        const size_t new_cap = lt->capacity ? lt->capacity * 2 : 8;
        lt->labels = zis_mem_realloc(lt->labels, new_cap * sizeof(uint32_t));
        assert(lt->labels);
        lt->capacity = new_cap;
        return label_table_alloc(lt);
    }
}

static void label_table_free(struct label_table *lt, int id) {
    assert(lt->length <= lt->capacity);
    assert(id >= 0 && (size_t)id < lt->length);
    if ((size_t)id == lt->length - 1) {
        lt->length--;
        while (lt->length && lt->labels[lt->length - 1] == UINT32_MAX)
            lt->length--;
    } else {
        lt->labels[id] = UINT32_MAX;
    }
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

struct zis_assembler {
#define AS_OBJ_MEMBER_BEGIN func_constants
    struct zis_array_obj *func_constants;
    struct zis_array_obj *func_symbols;
#define AS_OBJ_MEMBER_END instr_buffer
    struct instr_buffer instr_buffer;
    struct label_table  label_table;
    struct zis_func_obj_meta func_meta;
};

/// GC objects visitor. See `zis_objmem_object_visitor_t`.
static void assembler_gc_visitor(void *_as, enum zis_objmem_obj_visit_op op) {
    struct zis_assembler *const as = _as;
    void *begin = (char *)as + offsetof(struct zis_assembler, AS_OBJ_MEMBER_BEGIN);
    void *end   = (char *)as + offsetof(struct zis_assembler, AS_OBJ_MEMBER_END);
    zis_objmem_visit_object_vec(begin, end, op);
}

struct zis_assembler *zis_assembler_create(struct zis_context *z) {
    struct zis_assembler *const as = zis_mem_alloc(sizeof(struct zis_assembler));

    as->func_constants = zis_object_cast(z->globals->val_nil, struct zis_array_obj);
    as->func_symbols = zis_object_cast(z->globals->val_nil, struct zis_array_obj);
    instr_buffer_init(&as->instr_buffer);
    label_table_init(&as->label_table);
    as->func_meta.na = 0, as->func_meta.no = 0, as->func_meta.nr = 0;

    zis_objmem_add_gc_root(z, as, assembler_gc_visitor);
    as->func_constants = zis_array_obj_new(z, NULL, 0);
    as->func_symbols = zis_array_obj_new(z, NULL, 0);

    return as;
}

void zis_assembler_destroy(struct zis_assembler *as, struct zis_context *z) {
    zis_objmem_remove_gc_root(z, as);
    instr_buffer_fini(&as->instr_buffer);
    label_table_fini(&as->label_table);
    zis_mem_free(as);
}

void zis_assembler_clear(struct zis_assembler *as) {
    zis_array_obj_clear(as->func_constants);
    zis_array_obj_clear(as->func_symbols);
    instr_buffer_clear(&as->instr_buffer);
    label_table_clear(&as->label_table);
    as->func_meta.na = 0, as->func_meta.no = 0, as->func_meta.nr = 0;
}

#if ZIS_DEBUG_LOGGING

static int _as_finish_debug_dump_fn(const struct zis_disassemble_result *dis, void *_file) {
    FILE *fp = _file;
    char buffer[80];
    int buffer_used = 0, n;
    n = snprintf(
        buffer, sizeof buffer, "%04x:  %08x  %-6s %i",
        dis->address, dis->instr, dis->op_name, dis->operands[0]
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
    fwrite(buffer, 1, (size_t)buffer_used, fp);
    return 0;
}

#endif // ZIS_DEBUG_LOGGING

struct zis_func_obj *zis_assembler_finish(struct zis_assembler *as, struct zis_context *z) {
    struct zis_func_obj *func_obj = zis_func_obj_new_bytecode(
        z, as->func_meta, as->instr_buffer.data, as->instr_buffer.length
    );
    zis_assembler_clear(as);
    zis_debug_log_1(DUMP, "Asm", "zis_disassemble_bytecode()", fp, {
        zis_disassemble_bytecode(z, func_obj, _as_finish_debug_dump_fn, fp);
    });
    return func_obj;
}

const struct zis_func_obj_meta *zis_assembler_func_meta(
    struct zis_assembler *as, const struct zis_func_obj_meta *restrict m
) {
    if (m) {
        as->func_meta.na = m->na, as->func_meta.no = m->no, as->func_meta.nr = m->nr;
    }
    return &as->func_meta;
}

int zis_assembler_alloc_label(struct zis_assembler *as) {
    return label_table_alloc(&as->label_table);
}

int zis_assembler_place_label(struct zis_assembler *as, int id) {
    const size_t addr = as->instr_buffer.length;
    assert(addr <= UINT32_MAX);
    *label_table_ref(&as->label_table, id) = (uint32_t)addr;
    return id;
}

void zis_assembler_free_label(struct zis_assembler *as, int id) {
    label_table_free(&as->label_table, id);
}

void zis_assembler_append(struct zis_assembler *as, zis_instr_word_t instr) {
    assert_op_type_(zis_instr_extract_opcode(instr), != ZIS_OP_X);
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

void zis_assembler_append_ABsCs(
    struct zis_assembler *as, enum zis_opcode opcode,
    uint32_t A, int32_t Bs, int32_t Cs
) {
    assert_op_type_(opcode, == ZIS_OP_ABsCs);
    zis_assembler_append(as, zis_instr_make_ABsCs(opcode, A, Bs, Cs));
}

#endif // ZIS_FEATURE_ASM || ZIS_FEATURE_SRC

/* ----- module assembler --------------------------------------------------- */

#if ZIS_FEATURE_ASM

#include "stack.h"
#include "strutil.h"

#include "exceptobj.h"
#include "moduleobj.h"
#include "streamobj.h"

struct tas_context {
    struct zis_context *z;
    struct zis_assembler *as;
    struct zis_stream_obj *input;
    unsigned int line_number;
    char line_buffer[128];
};

zis_cold_fn static void tas_record_error(struct tas_context *tas, const char *s) {
    snprintf(tas->line_buffer, sizeof tas->line_buffer, "line %u: %s", tas->line_number, s);
}

zis_cold_fn static struct zis_exception_obj * tas_error_exception(struct tas_context *tas) {
    return zis_exception_obj_format(tas->z, "syntax", NULL, tas->line_buffer);
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
    const char *pseudo_func_operands
) {
    zis_assembler_clear(tas->as);

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
    zis_assembler_func_meta(tas->as, &func_meta);

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
                    tas->as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0]
                );
                break;
            case ZIS_OP_Asw:
                if (line_result.instr.operand_count != 1)
                    goto bad_operands;
                zis_assembler_append_Asw(
                    tas->as, line_result.instr.opcode,
                    line_result.instr.operands[0]
                );
                break;
            case ZIS_OP_ABw:
                if (line_result.instr.operand_count != 2)
                    goto bad_operands;
                zis_assembler_append_ABw(
                    tas->as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    (uint32_t)line_result.instr.operands[1]
                );
                break;
            case ZIS_OP_ABsw:
                if (line_result.instr.operand_count != 2)
                    goto bad_operands;
                zis_assembler_append_ABsw(
                    tas->as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    line_result.instr.operands[1]
                );
                break;
            case ZIS_OP_ABC:
                if (line_result.instr.operand_count != 3)
                    goto bad_operands;
                zis_assembler_append_ABC(
                    tas->as, line_result.instr.opcode,
                    (uint32_t)line_result.instr.operands[0],
                    (uint32_t)line_result.instr.operands[1],
                    (uint32_t)line_result.instr.operands[2]
                );
                break;
            case ZIS_OP_ABsCs:
                if (line_result.instr.operand_count != 3)
                    goto bad_operands;
                zis_assembler_append_ABsCs(
                    tas->as, line_result.instr.opcode,
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
            if (line_result.pseudo.opcode == PSEUDO_END) {
                break;
            } else {
                tas_record_error(tas, "unexpected pseudo operation");
                return NULL;
            }
        } else {
            if (line_status == TAS_PARSE_EOF)
                tas_record_error(tas, "unexpected EOF");
            return NULL;
        }
    }
    return zis_assembler_finish(tas->as, tas->z);
}

struct zis_exception_obj *zis_assembler_module_from_text(
    struct zis_context *z, struct zis_assembler *as,
    struct zis_stream_obj *input, struct zis_module_obj *output
) {
    // NOTE: `input`(zis_stream_obj) won't be moved during GC.

    struct tas_context context = {
        .z = z,
        .as = as,
        .input = input,
        .line_number = 0,
    };

    struct zis_exception_obj *exc_obj = NULL;

    // ~~~ tmp_regs = { [0] = output_module, [1] = func_arr } ~~
    size_t tmp_regs_n = 2;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, tmp_regs_n);
    tmp_regs[0] = zis_object_from(output);
    tmp_regs[1] = zis_object_from(zis_array_obj_new(z, NULL, 0));

    while (true) {
        union tas_parse_line_result line_result;
        enum tas_parse_line_status line_status = tas_parse_line(&context, &line_result);
        if (line_status == TAS_PARSE_EOF)
            break;
        if (
            line_status != TAS_PARSE_PSEUDO ||
            !(line_result.pseudo.opcode == PSEUDO_FUNC || line_result.pseudo.opcode == PSEUDO_TYPE)
        ) {
            tas_record_error(&context, "expecting .FUNC or .TYPE");
            exc_obj = tas_error_exception(&context);
            break;
        }
        if (line_result.pseudo.opcode == PSEUDO_FUNC) {
            struct zis_func_obj *func_obj = tas_parse_func(&context, line_result.pseudo.operands);
            if (!func_obj) {
                exc_obj = tas_error_exception(&context);
                break;
            }
            assert(zis_object_type(tmp_regs[1]) == z->globals->type_Array);
            struct zis_array_obj *arr = zis_object_cast(tmp_regs[1], struct zis_array_obj);
            zis_array_obj_append(z, arr, zis_object_from(func_obj));
        } else {
            // TODO: .TYPE ...
            tas_record_error(&context, "not implemented");
            exc_obj = tas_error_exception(&context);
            break;
        }
    }

    if (!exc_obj) {
        struct zis_array_obj *arr;
        struct zis_array_slots_obj *tbl;

        assert(zis_object_type(tmp_regs[1]) == z->globals->type_Array);
        arr = zis_object_cast(tmp_regs[1], struct zis_array_obj);
        tbl = zis_array_slots_obj_new2(z, arr->length, arr->_data);
        assert(zis_object_type(tmp_regs[0]) == z->globals->type_Module);
        output = zis_object_cast(tmp_regs[0], struct zis_module_obj);
        output->_functions = tbl;
    }

    zis_callstack_frame_free_temp(z, tmp_regs_n);

    return exc_obj;
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
    case ZIS_OP_ABsw:
        zis_instr_extract_operands_ABsw(instr, u[0], result->operands[1]);
        result->operands[0] = (int32_t)u[0], result->operands[2] = INT32_MIN;
        break;
    case ZIS_OP_ABC:
        zis_instr_extract_operands_ABC(instr, u[0], u[1], u[2]);
        result->operands[0] = (int32_t)u[0], result->operands[1] = (int32_t)u[1],
        result->operands[2] = (int32_t)u[2];
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
    struct zis_context *z, const struct zis_func_obj *func_obj,
    int (*fn)(const struct zis_disassemble_result *, void *), void *fn_arg
) {
    int fn_ret = 0;
    struct zis_disassemble_result dis_res;

    size_t tmp_regs_n = 1;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, tmp_regs_n);
    tmp_regs[0] = zis_object_from(func_obj);

    for (size_t i = 0, n = zis_func_obj_bytecode_length(func_obj); i < n; i++) {
        if ((void *)func_obj != (void *)tmp_regs[0]) {
            assert(zis_object_type(tmp_regs[0]) != z->globals->type_Function);
            func_obj = zis_object_cast(tmp_regs[0], struct zis_func_obj);
        }
        const zis_instr_word_t instr = func_obj->bytecode[i];
        dump_instr(instr, &dis_res);
        dis_res.address = (unsigned int)i;
        if ((fn_ret = fn(&dis_res, fn_arg)))
            return fn_ret;
    }

    zis_callstack_frame_free_temp(z, tmp_regs_n);

    return fn_ret;
}

#endif // ZIS_FEATURE_DIS
