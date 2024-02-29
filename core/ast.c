#include "ast.h"

#include <assert.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

#include "typeobj.h"

#if ZIS_FEATURE_SRC

/* ----- AST nodes ---------------------------------------------------------- */

#pragma pack(push, 1)

static const char *const node_type_text[(unsigned int)_ZIS_AST_NODE_TYPE_COUNT] = {

#define E(NAME, FIELD_LIST) [ (unsigned int) ZIS_AST_NODE_##NAME ] = #NAME ,
    ZIS_AST_NODE_LIST
#undef E

};

#pragma pack(pop)

const char *zis_ast_node_type_represent(enum zis_ast_node_type type) {
    const unsigned int type_index = (unsigned int)type;
    if (type_index < (unsigned int)_ZIS_AST_NODE_TYPE_COUNT)
        return node_type_text[type_index];
    return "?";
}

/* ----- node object -------------------------------------------------------- */

struct zis_ast_node_obj *_zis_ast_node_obj_new(
    struct zis_context *z,
    enum zis_ast_node_type type, size_t data_elem_count, bool init_data
) {
    struct zis_ast_node_obj *self = zis_object_cast(
        zis_objmem_alloc_ex(
            z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_AstNode,
            2 + data_elem_count, 0
        ),
        struct zis_ast_node_obj
    );
    self->_type = zis_smallint_to_ptr((zis_smallint_t)(zis_smallint_unsigned_t)type);
    if (init_data)
        zis_object_vec_zero(self->_data, data_elem_count);
    return self;
}

struct zis_ast_node_obj_position *
zis_ast_node_obj_position(struct zis_ast_node_obj *self) {
    const size_t slots_count = zis_smallint_from_ptr(self->_slots_num);
    assert(slots_count == zis_object_slot_count(zis_object_from(self)));
    void *const bytes = zis_object_ref_bytes(zis_object_from(self), slots_count);
    return bytes;
}

const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( AstNode ) = {
    .name       = NULL,
    .slots_num  = (size_t)-1,
    .bytes_size = sizeof(struct zis_ast_node_obj_position),
    .fields     = NULL,
    .methods    = NULL,
    .statics    = NULL,
};

#endif // ZIS_FEATURE_SRC
