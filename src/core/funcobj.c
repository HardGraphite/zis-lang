#include "funcobj.h"

#include <assert.h>
#include <string.h>

#include "algorithm.h"
#include "context.h"
#include "globals.h"
#include "invoke.h"
#include "ndefutil.h"
#include "objmem.h"
#include "stack.h"

#include "tupleobj.h"

static_assert(sizeof(struct zis_func_obj_meta) <= sizeof(void *), "");
static_assert(sizeof(struct zis_native_func_meta) == sizeof(struct zis_func_obj_meta), "");

bool zis_func_obj_meta_conv(
    struct zis_func_obj_meta *dst,
    struct zis_native_func_meta func_def_meta
) {
    if (zis_unlikely(func_def_meta.na + zis_func_obj_meta_no_abs(func_def_meta.no) > func_def_meta.nl))
        return false;
    const struct zis_func_obj_meta func_obj_meta = {
        .na = func_def_meta.na,
        .no = func_def_meta.no,
        .nr = UINT16_C(1) + func_def_meta.nl,
    };
    if (zis_unlikely(func_obj_meta.nr == 0))
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
    const enum zis_objmem_alloc_type alloc_type =
        bytecode_len ? ZIS_OBJMEM_ALLOC_NOMV : ZIS_OBJMEM_ALLOC_SURV;
    struct zis_func_obj *const self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, alloc_type, z->globals->type_Function,
            0U, FUN_OBJ_BYTES_FIXED_SIZE + sizeof(zis_func_obj_bytecode_word_t) * bytecode_len
        ),
        struct zis_func_obj
    );
    struct zis_context_globals *g = z->globals;
    zis_func_obj_set_resources(self, g->val_empty_array_slots, g->val_empty_array_slots);
    self->_module = g->val_mod_unnamed;
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
    struct zis_func_obj *const self = func_obj_alloc(z, code_len);
    self->meta = meta;
    self->native = NULL;
    const size_t code_sz = code_len * sizeof code[0];
    assert(self->_bytes_size >= FUN_OBJ_BYTES_FIXED_SIZE + code_sz);
    memcpy(self->bytecode, code, code_sz);
    return self;
}

void zis_func_obj_set_resources(
    struct zis_func_obj *self,
    struct zis_array_slots_obj *symbols /*=NULL*/, struct zis_array_slots_obj *constants /*=NULL*/
) {
    if (symbols) {
        self->_symbols = symbols;
        zis_object_write_barrier(self, symbols);
    }
    if (constants) {
        self->_constants = constants;
        zis_object_write_barrier(self, constants);
    }
}

void zis_func_obj_set_module(
    struct zis_context *z,
    struct zis_func_obj *self, struct zis_module_obj *mod
) {
    zis_unused_var(z);
    assert(self->_module == z->globals->val_mod_unnamed);
    self->_module = mod;
    zis_object_assert_no_write_barrier_2(self, zis_object_from(mod));
}

size_t zis_func_obj_bytecode_length(const struct zis_func_obj *self) {
    assert(self->_bytes_size >= FUN_OBJ_BYTES_FIXED_SIZE);
    return (self->_bytes_size - FUN_OBJ_BYTES_FIXED_SIZE) / sizeof(zis_func_obj_bytecode_word_t);
}

#define assert_arg1_Function(__z) \
    (assert(zis_object_type_is((__z)->callstack->frame[1], (__z)->globals->type_Function)))

ZIS_NATIVE_FUNC_DEF(T_Function_M_operator_call, z, {1, -1, 2}) {
    /*#DOCSTR# func Function:\'()'(*args) :: Any
    Call the function. */
    assert_arg1_Function(z);
    struct zis_object **frame = z->callstack->frame;
    assert(zis_object_type_is(frame[2], z->globals->type_Tuple));
    const size_t argc = zis_tuple_obj_length(zis_object_cast(frame[2], struct zis_tuple_obj));
    struct zis_func_obj *func = zis_invoke_prepare_pa(z, frame[1], frame, frame[2], argc);
    if (zis_unlikely(!func))
        return ZIS_THR;
    assert(zis_object_from(func) == frame[1]);
    return zis_invoke_func(z, func);
}

ZIS_NATIVE_FUNC_DEF(T_Function_M_hash, z, {1, 0, 1}) {
    /*#DOCSTR# func Function:hash() :: Int
    Generates hash code. */
    assert_arg1_Function(z);
    struct zis_object **frame = z->callstack->frame;
    struct zis_func_obj *const self = zis_object_cast(frame[1], struct zis_func_obj);
    size_t hash_code;
    if (self->native) {
        void *p = (void *)(uintptr_t)self->native;
        hash_code = zis_hash_pointer(p);
    } else {
        void *p = self; // Bytecode function object won't be moved by GC.
        hash_code = zis_hash_pointer(p);
    }
    frame[0] = zis_smallint_to_ptr((zis_smallint_t)zis_hash_truncate(hash_code));
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    T_Function_D_methods,
    { "()"      , &T_Function_M_operator_call   },
    { "hash"    , &T_Function_M_hash            },
);

ZIS_NATIVE_TYPE_DEF_XB(
    Function,
    struct zis_func_obj, _bytes_size,
    NULL, T_Function_D_methods, NULL
);
