#include "ast.h"

#include <assert.h>
#include <string.h>

#include "compat.h"
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

static const char *node_type_fields[(unsigned int)_ZIS_AST_NODE_TYPE_COUNT] = {

    // [ TYPE ] = "NAME1\0TYPE1\0NAME2\0TYPE2\0"

#define E(NAME, FIELD_LIST) [ (unsigned int) ZIS_AST_NODE_##NAME ] = FIELD_LIST ,
    ZIS_AST_NODE_LIST
#undef E

};

#pragma pack(pop)

const char *zis_ast_node_type_represent(enum zis_ast_node_type type) {
    const unsigned int type_index = (unsigned int)type;
    if (type_index < (unsigned int)_ZIS_AST_NODE_TYPE_COUNT)
        return node_type_text[type_index];
    return NULL;
}

int zis_ast_node_type_fields(
    struct zis_context *z, enum zis_ast_node_type type,
    const char *restrict f_names[ZIS_PARAMARRAY_STATIC 4],
    struct zis_type_obj *restrict f_types[ZIS_PARAMARRAY_STATIC 4]
) {
    const unsigned int type_index = (unsigned int)type;
    if (type_index >= (unsigned int)_ZIS_AST_NODE_TYPE_COUNT)
        return -1;
    struct zis_context_globals *const g = z->globals;
    const char *fields = node_type_fields[type_index];
    for (unsigned int i = 0; ; i++) {
        assert(i < 4);
        if (!*fields)
            return (unsigned int)i;
        const char *field_type_name = fields;
        const char *field_name = field_type_name + strlen(field_type_name) + 1;
        fields = field_name + strlen(field_name) + 1;
        f_names[i] = field_name;
        struct zis_type_obj *field_type;
        if (strcmp(field_type_name, "Node") == 0)
            field_type = g->type_AstNode;
        else if (strcmp(field_type_name, "Array") == 0)
            field_type = g->type_Array;
        else if (strcmp(field_type_name, "Symbol") == 0)
            field_type = g->type_Symbol;
        else if (strcmp(field_type_name, "Bool") == 0)
            field_type = g->type_Bool;
        else if (strcmp(field_type_name, "Object") == 0)
            field_type = NULL;
        else
            return -1;
        f_types[i] = field_type;
    }
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

struct zis_ast_node_obj_location *
zis_ast_node_obj_location(struct zis_ast_node_obj *self) {
    const size_t slots_count = zis_smallint_from_ptr(self->_slots_num);
    assert(slots_count == zis_object_slot_count(zis_object_from(self)));
    void *const bytes = zis_object_ref_bytes(zis_object_from(self), slots_count);
    return bytes;
}

const struct zis_native_type_def ZIS_NATIVE_TYPE_VAR( AstNode ) = {
    .name       = NULL,
    .slots_num  = (size_t)-1,
    .bytes_size = sizeof(struct zis_ast_node_obj_location),
    .fields     = NULL,
    .methods    = NULL,
    .statics    = NULL,
};

#endif // ZIS_FEATURE_SRC
