#include "object.h"

#include "context.h"
#include "globals.h"
#include "smallint.h"

bool zis_object_hash(
    size_t *restrict hash_code,
    struct zis_context *z, struct zis_object *obj
) {
    if (zis_unlikely(zis_object_is_smallint(obj))) {
        *hash_code = zis_smallint_hash(zis_smallint_from_ptr(obj));
        return true;
    }

    // TODO: hash of object of other types.
    zis_unused_var(z);
    return false;
}

enum zis_object_ordering zis_object_compare(
    struct zis_context *z,
    struct zis_object *lhs, struct zis_object *rhs
) {
    struct zis_type_obj *lhs_type;

    if (zis_unlikely(zis_object_is_smallint(lhs))) {
        if (zis_object_is_smallint(rhs)) {
            const zis_smallint_t lhs_v = zis_smallint_from_ptr(lhs);
            const zis_smallint_t rhs_v = zis_smallint_from_ptr(rhs);
            return
                lhs_v == rhs_v ? ZIS_OBJECT_EQ :
                lhs_v <  rhs_v ? ZIS_OBJECT_LT : ZIS_OBJECT_GT;
        }

        lhs_type = z->globals->type_Int;
    } else {
        lhs_type = zis_object_type(lhs);
    }

    // TODO: compare objects of other types.
    zis_unused_var(lhs_type);
    return ZIS_OBJECT_IC;
}

bool zis_object_equals(
    struct zis_context *z,
    struct zis_object *obj1, struct zis_object *obj2
) {
    if (obj1 == obj2)
        return true;

    const enum zis_object_ordering cmp_res = zis_object_compare(z, obj1, obj2);
    return cmp_res == ZIS_OBJECT_EQ;
}
