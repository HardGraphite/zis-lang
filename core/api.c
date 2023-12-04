#include <zis.h>

#include <errno.h>

#include "attributes.h"
#include "context.h"
#include "globals.h"
#include "object.h"
#include "stack.h"

#include "boolobj.h"
#include "floatobj.h"
#include "intobj.h"
#include "stringobj.h"

/* ----- common utilities --------------------------------------------------- */

static struct zis_object **api_ref_local(zis_t z, unsigned int i) {
    struct zis_callstack *const cs = z->callstack;
    struct zis_object **const ref = cs->frame + i;
    if (zis_unlikely(ref > cs->top))
        return NULL;
    return ref;
}

static struct zis_object **api_ref_local_or_last(zis_t z, unsigned int i) {
    struct zis_callstack *const cs = z->callstack;
    struct zis_object **const ref = cs->frame + i;
    if (zis_unlikely(ref > cs->top))
        return cs->top;
    return ref;
}

static struct zis_object *api_get_local(zis_t z, unsigned int i) {
    struct zis_callstack *const cs = z->callstack;
    struct zis_object **const ref = cs->frame + i;
    if (zis_unlikely(ref > cs->top))
        return NULL;
    return *ref;
}

/* ----- zis-api-context ---------------------------------------------------- */

ZIS_API zis_t zis_create(void) {
    return zis_context_create();
}

ZIS_API void zis_destroy(zis_t z) {
    zis_context_destroy(z);
}

/* ----- zis-api-natives ---------------------------------------------------- */

ZIS_API int zis_native_block(zis_t z, size_t reg_max, int(*fn)(zis_t, void *), void *arg) {
    if (!zis_callstack_enter(z->callstack, reg_max + 1U, NULL))
        return ZIS_E_ARG;
    z->callstack->frame[0] = zis_callstack_frame_info(z->callstack)->prev_frame[0];
    const int ret_val = fn(z, arg);
    zis_callstack_frame_info(z->callstack)->prev_frame[0] = z->callstack->frame[0];
    assert(zis_callstack_frame_info(z->callstack)->return_ip == NULL);
    zis_callstack_leave(z->callstack);
    return ret_val;
}

/* ----- zis-api-values ----------------------------------------------------- */

ZIS_API int zis_load_nil(zis_t z, unsigned int reg, size_t n) {
    struct zis_object **reg_begin = api_ref_local(z, reg);
    if (zis_unlikely(!reg_begin))
        return ZIS_E_IDX;
    if (zis_unlikely(!n))
        return ZIS_OK;
    struct zis_object **reg_last = api_ref_local_or_last(z, reg + n - 1);
    struct zis_object *const nil = zis_object_from(z->globals->val_nil);
    for (struct zis_object **p = reg_begin; p <= reg_last; p++)
        *p = nil;
    return ZIS_OK;
}

ZIS_API int zis_read_nil(zis_t z, unsigned int reg) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (obj == zis_object_from(z->globals->val_nil))
        return ZIS_OK;
    assert(zis_object_type(obj) != z->globals->type_Nil);
    return ZIS_E_TYPE;
}

ZIS_API int zis_load_bool(zis_t z, unsigned int reg, bool val) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_context_globals *const g = z->globals;
    *obj_ref = zis_object_from(val ? g->val_true : g->val_false);
    return ZIS_OK;
}

ZIS_API int zis_read_bool(zis_t z, unsigned int reg, bool *val) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj) || zis_object_type(obj) != z->globals->type_Bool))
        return ZIS_E_TYPE;
    *val = zis_bool_obj_value(zis_object_cast(obj, struct zis_bool_obj));
    return ZIS_OK;
}

ZIS_API int zis_make_int(zis_t z, unsigned int reg, int64_t val) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    zis_smallint_or_int_obj(z, obj_ref, val);
    return ZIS_OK;
}

ZIS_API int zis_read_int(zis_t z, unsigned int reg, int64_t *val) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_object_is_smallint(obj)) {
        *val = zis_smallint_from_ptr(obj);
        return ZIS_OK;
    }
    if (zis_object_type(obj) == z->globals->type_Int) {
        const int64_t v_i64 =
            zis_int_obj_value_l(zis_object_cast(obj, struct zis_int_obj));
        if (zis_unlikely(v_i64 == INT64_MIN && errno == ERANGE))
            return ZIS_E_BUF;
        *val = v_i64;
        return ZIS_OK;
    }
    return ZIS_E_TYPE;
}

ZIS_API int zis_make_float(zis_t z, unsigned int reg, double val) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    zis_float_obj_new(z, obj_ref, val);
    return ZIS_OK;
}

ZIS_API int zis_read_float(zis_t z, unsigned int reg, double *val) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj) || zis_object_type(obj) != z->globals->type_Float))
        return ZIS_E_TYPE;
    *val = zis_float_obj_value(zis_object_cast(obj, struct zis_float_obj));
    return ZIS_OK;
}

ZIS_API int zis_make_string(zis_t z, unsigned int reg, const char *str, size_t sz) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    const size_t err_pos = zis_string_obj_new(z, obj_ref, str, sz);
    return err_pos == (size_t)-1 ? ZIS_OK : ZIS_E_ARG;
}

ZIS_API int zis_read_string(zis_t z, unsigned int reg, char *buf, size_t *sz) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj) || zis_object_type(obj) != z->globals->type_String))
        return ZIS_E_TYPE;
    const size_t n = zis_string_obj_value(zis_object_cast(obj, struct zis_string_obj), buf, *sz);
    if (zis_unlikely(n == (size_t)-1))
        return ZIS_E_BUF;
    *sz = n;
    return ZIS_OK;
}

/* ----- zis-api-variables --------------------------------------------------- */

ZIS_API int zis_move_local(zis_t z, unsigned int dst, unsigned int src) {
    struct zis_object **dst_ref = api_ref_local(z, dst);
    struct zis_object **src_ref = api_ref_local(z, src);
    if (zis_unlikely(!(dst_ref && src_ref)))
        return ZIS_E_IDX;
    *dst_ref = *src_ref;
    return ZIS_OK;
}
