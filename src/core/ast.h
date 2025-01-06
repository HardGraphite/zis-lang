/// Abstract syntax tree.

#pragma once

#include <stdbool.h>

#include "attributes.h"
#include "compat.h"
#include "object.h"

#include "zis_config.h" // ZIS_FEATURE_SRC

struct zis_context;

#if ZIS_FEATURE_SRC

/* ----- AST nodes ---------------------------------------------------------- */

#include "astdef.h"

/// AST node type.
enum zis_ast_node_type {

#define E(NAME, FIELD_LIST) ZIS_AST_NODE_##NAME ,
    ZIS_AST_NODE_LIST
#undef E

    _ZIS_AST_NODE_TYPE_COUNT
};

/// Allocate a node object. If `__data_init` is false, all the data fields
/// are not initialized and must be assigned immediately.
#define zis_ast_node_new(__z, __type, __data_init) \
    (_zis_ast_node_obj_new(                        \
        (__z), ZIS_AST_NODE_##__type,              \
        sizeof(struct zis_ast_node_##__type##_data) / sizeof(void *), \
        (__data_init)                              \
    ))                                             \
// ^^^ zis_ast_node_new() ^^^

/// Get data field of a node.
#define zis_ast_node_get_field(__node, __type, __field) \
(                                                       \
    assert(zis_ast_node_obj_type((__node)) == ZIS_AST_NODE_##__type),                 \
    _zis_ast_node_obj_data_as((__node), struct zis_ast_node_##__type##_data)->__field \
)                                                        \
// ^^^ zis_ast_node_get_field() ^^^

/// Set data field of a node.
#define zis_ast_node_set_field(__node, __type, __field, __value) \
do {                                                             \
    (void)sizeof((((struct zis_ast_node_##__type##_data *)0)->__field = (__value)) ? 1 : 0); \
    void *const __value_1 = (__value);                           \
    struct zis_ast_node_obj *const __node_1 = (__node);          \
    assert(zis_ast_node_obj_type(__node_1) == ZIS_AST_NODE_##__type); \
    _zis_ast_node_obj_data_as(__node_1, struct zis_ast_node_##__type##_data)->__field = __value_1; \
    zis_object_write_barrier(__node_1, __value_1);               \
} while (0)                                                      \
// ^^^ zis_ast_node_set_field() ^^^

/// Represent node type as text.
const char *zis_ast_node_type_represent(enum zis_ast_node_type type);

/// Get field names and types of a node type.
/// Returns the number of fields; or -1 on error.
/// The field type being NULL means the field can be any object.
int zis_ast_node_type_fields(
    struct zis_context *z, enum zis_ast_node_type type,
    const char *restrict f_names[ZIS_PARAMARRAY_STATIC 4],
    struct zis_type_obj *restrict f_types[ZIS_PARAMARRAY_STATIC 4]
);

/* ----- node object -------------------------------------------------------- */

/// AST node object.
struct zis_ast_node_obj {
    ZIS_OBJECT_HEAD
    // --- SLOTS ---
    struct zis_object *_slots_num;
    struct zis_object *_type; ///< AST node type, a smallint
    struct zis_object *_data[];
    // --- BYTES ---
    // struct zis_ast_node_obj_location location;
};

/// Node source-location info.
struct zis_ast_node_obj_location {
    unsigned int line0, column0, line1, column1;
};

/// Create an AST node object.
struct zis_ast_node_obj *_zis_ast_node_obj_new(
    struct zis_context *z,
    enum zis_ast_node_type type, size_t data_elem_count, bool init_data
);

/// Get the node type.
zis_static_force_inline enum zis_ast_node_type
zis_ast_node_obj_type(const struct zis_ast_node_obj *self) {
    assert(zis_object_is_smallint(self->_type));
    const zis_smallint_t t = zis_smallint_from_ptr(self->_type);
    assert(t >= 0 && t < (zis_smallint_t)_ZIS_AST_NODE_TYPE_COUNT);
    return (enum zis_ast_node_type)(zis_smallint_unsigned_t)t;
}

/// Get node location.
struct zis_ast_node_obj_location *zis_ast_node_obj_location(struct zis_ast_node_obj *self);

/// Get reference to the node data.
/// The `__data_type` must be a struct consisting of object pointers.
#define _zis_ast_node_obj_data_as(__self, __data_type) \
(                                                      \
    assert(sizeof(__data_type) == (size_t)(zis_smallint_from_ptr((__self)->_slots_num) - 2) * sizeof(void *)), \
    (__data_type *)((__self)->_data)                   \
)

#endif // ZIS_FEATURE_SRC
