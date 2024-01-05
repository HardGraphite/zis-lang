#include "funcobj.h"

#include <assert.h>
#include <string.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

static_assert(sizeof(struct zis_func_obj_meta) <= sizeof(void *), "");
static_assert(sizeof(struct zis_native_func_meta) == sizeof(struct zis_func_obj_meta), "");

bool zis_func_obj_meta_conv(
    struct zis_func_obj_meta *dst,
    struct zis_native_func_meta func_def_meta
) {
    const struct zis_func_obj_meta func_obj_meta = {
        .na = func_def_meta.na,
        .no = func_def_meta.no,
        .nr = 1 + func_def_meta.na + (func_def_meta.no == (uint8_t)-1 ? 1 : func_def_meta.no) + func_def_meta.nl,
    };
    if (zis_unlikely(func_obj_meta.nr <= func_def_meta.nl))
        return false;
    *dst = func_obj_meta;
    return true;
}

#define FUN_OBJ_BYTES_FIXED_SIZE \
    (ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_func_obj, _bytes_size))

/// Allocate a function object and initialize slots.
static struct zis_func_obj *func_obj_alloc(
    struct zis_context *z, size_t bytecode_len
) {
    struct zis_func_obj *const self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_SURV, z->globals->type_Function,
            0U, FUN_OBJ_BYTES_FIXED_SIZE + sizeof(zis_func_obj_bytecode_word_t) * bytecode_len
        ),
        struct zis_func_obj
    );
    self->_symbols = z->globals->val_empty_array_slots;
    self->_constants = z->globals->val_empty_array_slots;
    self->_module = z->globals->val_common_top_module;
    return self;
}

struct zis_func_obj *zis_func_obj_new_native(
    struct zis_context *z,
    struct zis_func_obj_meta meta, zis_native_func_t code
) {
    struct zis_func_obj *const self = func_obj_alloc(z, 0);
    self->meta = meta;
    self->native = code;
    return self;
}

struct zis_func_obj *zis_func_obj_new_bytecode(
    struct zis_context *z,
    struct zis_func_obj_meta meta,
    const zis_func_obj_bytecode_word_t *code, size_t code_len
) {
    struct zis_func_obj *const self = func_obj_alloc(z, 0);
    self->meta = meta;
    self->native = NULL;
    const size_t code_sz = code_len * sizeof code[0];
    assert(self->_bytes_size >= FUN_OBJ_BYTES_FIXED_SIZE + code_sz);
    memcpy(self->bytecode, code, code_sz);
    return self;
}

void zis_func_obj_set_module(
    struct zis_context *z,
    struct zis_func_obj *self, struct zis_module_obj *mod
) {
    zis_unused_var(z);
    assert(self->_module == z->globals->val_common_top_module);
    self->_module = mod;
    zis_object_assert_no_write_barrier_2(self, zis_object_from(mod));
}

ZIS_NATIVE_TYPE_DEF_XB(
    Function,
    struct zis_func_obj, _bytes_size,
    NULL, NULL, NULL
);
