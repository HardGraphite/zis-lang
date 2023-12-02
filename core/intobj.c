#include "intobj.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "context.h"
#include "globals.h"
#include "ndefutil.h"
#include "objmem.h"

struct zis_int_obj {
    ZIS_OBJECT_HEAD
    // --- BYTES ---
    const size_t _bytes_size; // !
    uint16_t cell_count;
    bool     negative;
    uint64_t cells[];
};

#define INT_OBJ_CELL_COUNT_MAX  UINT16_MAX

#define INT_OBJ_BYTES_FIXED_SIZE \
    ZIS_NATIVE_TYPE_STRUCT_XB_FIXED_SIZE(struct zis_int_obj, _bytes_size)

/// Allocate but do not initialize.
struct zis_int_obj *int_obj_alloc(
    struct zis_context *z, struct zis_object **ret,
    size_t cell_count
) {
    assert(cell_count > 0);
    assert(cell_count <= INT_OBJ_CELL_COUNT_MAX);
    struct zis_object *const obj = zis_objmem_alloc_ex(
        z, ZIS_OBJMEM_ALLOC_AUTO, z->globals->type_Int,
        0, INT_OBJ_BYTES_FIXED_SIZE + cell_count * sizeof(uint64_t)
    );
    *ret = obj;
    struct zis_int_obj *const self = zis_object_cast(obj, struct zis_int_obj);
    self->cell_count = (uint16_t)cell_count;
    return self;
}

/// Maximum number of cells in this object.
zis_unused_fn static size_t int_obj_cells_capacity(const struct zis_int_obj *self) {
    assert(self->_bytes_size >= INT_OBJ_BYTES_FIXED_SIZE);
    return (self->_bytes_size - INT_OBJ_BYTES_FIXED_SIZE) / sizeof(uint64_t);
}

void _zis_int_obj_new(struct zis_context *z, struct zis_object **ret, int64_t val) {
    struct zis_int_obj *const self = int_obj_alloc(z, ret, 1);
    if (val >= 0) {
        self->negative = false;
        self->cells[0] = (uint64_t)val;
    } else {
        self->negative = true;
        self->cells[0] = (uint64_t)-val;
    }
}

int zis_int_obj_value_i(const struct zis_int_obj *self) {
    const size_t cell_count = self->cell_count;
    assert(cell_count);
    if (cell_count == 1) {
        const uint64_t val = self->cells[0];
        if (val <= (uint64_t)INT_MAX) {
            const int val_int = (int)val;
            return self->negative ? -val_int : val_int;
        } else if (self->negative && val == (uint64_t)-(int64_t)INT_MIN) {
            errno = 0;
            return INT_MIN;
        }
    }
    errno = ERANGE;
    return INT_MIN;
}

int64_t zis_int_obj_value_l(const struct zis_int_obj *self) {
    const size_t cell_count = self->cell_count;
    assert(cell_count);
    if (cell_count == 1) {
        const uint64_t val = self->cells[0];
        if (val <= (uint64_t)INT64_MAX) {
            const int64_t val_i64 = (int64_t)val;
            return self->negative ? -val_i64 : val_i64;
        } else if (self->negative && val == UINT64_C(0) - (uint64_t)INT64_MIN) {
            errno = 0;
            return INT64_MIN;
        }
    }
    errno = ERANGE;
    return INT64_MIN;
}

double zis_int_obj_value_f(const struct zis_int_obj *self) {
    const size_t cell_count = self->cell_count;
    if (cell_count <= 31) {
        assert(cell_count);
        const double val_flt = (double)self->cells[cell_count - 1];
        return self->negative ? -val_flt : val_flt;
    }
    errno = ERANGE;
    return HUGE_VAL;
}

ZIS_NATIVE_FUNC_LIST_DEF(
    int_methods,
);

ZIS_NATIVE_FUNC_LIST_DEF(
    int_statics,
);

ZIS_NATIVE_TYPE_DEF_XB(
    Int,
    struct zis_int_obj, _bytes_size,
    NULL,
    ZIS_NATIVE_FUNC_LIST_VAR(int_methods),
    ZIS_NATIVE_FUNC_LIST_VAR(int_statics)
);
