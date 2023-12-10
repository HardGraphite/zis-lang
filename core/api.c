#include <zis.h>

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "object.h"
#include "stack.h"

#include "arrayobj.h"
#include "boolobj.h"
#include "floatobj.h"
#include "intobj.h"
#include "stringobj.h"
#include "tupleobj.h"

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
    zis_callstack_enter(z->callstack, reg_max + 1U, NULL);
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
    *obj_ref = zis_smallint_or_int_obj(z, val);
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
    *obj_ref = zis_object_from(zis_float_obj_new(z, val));
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
    struct zis_string_obj *const str_obj = zis_string_obj_new(z, str, sz);
    if (zis_unlikely(!str_obj))
        return ZIS_E_ARG;
    *obj_ref = zis_object_from(str_obj);
    return ZIS_OK;
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

struct api_make_values_state {
    struct zis_context *z;
    va_list ap;
    const char *fmt_p;
    int count;
};

static int api_make_values_impl(
    struct api_make_values_state *x,
    struct zis_object **ret_p, struct zis_object **ret_end /*excluding*/,
    const char *fmt_end /*excluding*/,
    bool nested
) {
    struct zis_context *const z = x->z;
    int status = ZIS_OK;
    const char *fmt_p;
    int count;

#define PULL_STATE() \
    (fmt_p = x->fmt_p, count = x->count)
#define PUSH_STATE() \
    (x->fmt_p = fmt_p, x->count = count)

    PULL_STATE();

    while (true) {
        assert(fmt_p <= fmt_end);
        if (fmt_p == fmt_end)
            goto do_return;

        assert(ret_p <= ret_end);
        if (ret_p == ret_end)
            goto do_return;

        switch (*fmt_p) {
        case '%': {
            const unsigned int reg = va_arg(x->ap, unsigned int);
#if ZIS_DEBUG
            if (reg == 0U)
                zis_debug_log(WARN, "API", "zis_make_values(): read REG-0");
#endif // ZIS_DEBUG
            struct zis_object *obj = api_get_local(z, reg);
            if (zis_unlikely(!obj)) {
                status = ZIS_E_ARG;
                goto do_return;
            }
            *ret_p = obj;
            break;
        }

        case 'n':
            *ret_p = zis_object_from(z->globals->val_nil);
            break;

        case 'x': {
            struct zis_context_globals *const g = z->globals;
            *ret_p = zis_object_from(va_arg(x->ap, int) ? g->val_true : g->val_false);
            break;
        }

        case 'i':
            *ret_p = zis_smallint_or_int_obj(z, va_arg(x->ap, int64_t));
            break;

        case 'f':
            *ret_p = zis_object_from(zis_float_obj_new(z, va_arg(x->ap, double)));
            break;

        case 's': {
            const char *s = va_arg(x->ap, const char *);
            const size_t n = va_arg(x->ap, size_t);
            struct zis_string_obj *const str_obj = zis_string_obj_new(z, s, n);
            if (zis_unlikely(!str_obj)) {
                status = ZIS_E_ARG;
                goto do_return;
            }
            *ret_p = zis_object_from(str_obj);
            break;
        }

        case '(': {
            if (nested) {
                zis_debug_log(ERROR, "API", "zis_make_values(): nested \"(...)\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            fmt_p++;
            const char *const s_end = strchr(fmt_p, ')');
            if (!s_end) {
                zis_debug_log(ERROR, "API", "zis_make_values(): unmatched \"(...)\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count = (size_t)(s_end - fmt_p);
            if (elem_count) {
                PUSH_STATE();
                struct zis_object **tmp_regs =
                    zis_callstack_frame_alloc_temp(z, elem_count);
                const int rv = api_make_values_impl(
                    x, tmp_regs, tmp_regs + elem_count, s_end, true
                );
                PULL_STATE();
                if (rv == ZIS_OK) {
                    *ret_p = zis_object_from(zis_tuple_obj_new(z, tmp_regs, elem_count));
                    zis_callstack_frame_free_temp(z, elem_count);
                } else {
                    zis_callstack_frame_free_temp(z, elem_count);
                    return rv;
                }
            } else {
                *ret_p = zis_object_from(zis_tuple_obj_new(z, NULL, 0U));
            }
            assert(*fmt_p == ')');
            break;
        }

        case '[': {
            if (nested) {
                zis_debug_log(ERROR, "API", "zis_make_values(): nested \"[...]\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            size_t reserve = 0U;
            if (fmt_p[1] == '*') {
                fmt_p++;
                reserve = va_arg(x->ap, size_t);
            }
            fmt_p++;
            const char *const s_end = strchr(fmt_p, ']');
            if (!s_end){
                zis_debug_log(ERROR, "API", "zis_make_values(): unmatched \"[...]\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count = (size_t)(s_end - fmt_p);
            if (elem_count) {
                PUSH_STATE();
                struct zis_object **tmp_regs =
                    zis_callstack_frame_alloc_temp(z, elem_count);
                const int rv = api_make_values_impl(
                    x, tmp_regs, tmp_regs + elem_count, s_end, true
                );
                if (rv == ZIS_OK) {
                    PULL_STATE();
                    *ret_p = zis_object_from(
                        zis_array_obj_new2(z, reserve, tmp_regs, elem_count)
                    );
                    zis_callstack_frame_free_temp(z, elem_count);
                } else {
                    zis_callstack_frame_free_temp(z, elem_count);
                    return rv;
                }
            } else {
                *ret_p = zis_object_from(zis_array_obj_new2(z, reserve, NULL, 0U));
            }
            assert(*fmt_p == ']');
            break;
        }

        case '-':
            break;

        default:
            if (!*fmt_p)
                goto do_return;
            zis_debug_log(
                ERROR, "API",
                "zis_make_values(): unrecognized specifier '%c'", *fmt_p
            );
            return ZIS_E_ARG;
        }

        assert(status == ZIS_OK);

        ret_p++;
        fmt_p++;
        count++;
    }

do_return:
    PUSH_STATE();
    assert(status <= 0);
    return status;

#undef PULL_STATE
#undef PUSH_STATE
}

ZIS_API int zis_make_values(zis_t z, unsigned int reg_begin, const char *fmt, ...) {
    struct zis_object **const reg_beg_p = api_ref_local(z, reg_begin);
    if (zis_unlikely(!reg_beg_p))
        return ZIS_E_IDX;
    struct zis_object **const reg_end = z->callstack->top + 1;
    assert(reg_beg_p < reg_end);

    struct api_make_values_state x;
    x.z = z;
    x.fmt_p = fmt;
    x.count = 0;
    va_start(x.ap, fmt);
    const int ret = api_make_values_impl(&x, reg_beg_p, reg_end, (void *)UINTPTR_MAX, false);
    va_end(x.ap);
    assert(ret <= 0);
    return ret == 0 ? x.count : ret;
}

struct api_read_values_state {
    struct zis_context *z;
    va_list ap;
    const char *fmt_p;
    int count;
    bool no_type_err_for_nil;
};

static int api_read_values_impl(
    struct api_read_values_state *x,
    struct zis_object *const *in_p, struct zis_object *const *in_end /*excluding*/,
    const char *fmt_end /*excluding*/,
    bool nested
) {
    struct zis_context *const z = x->z;
    struct zis_context_globals *const g = z->globals;
    int status = ZIS_OK;
    const char *fmt_p;
    int count;

#define PULL_STATE() \
    (fmt_p = x->fmt_p, count = x->count)
#define PUSH_STATE() \
    (x->fmt_p = fmt_p, x->count = count)

    PULL_STATE();

    while (true) {
        assert(fmt_p <= fmt_end);
        if (fmt_p == fmt_end)
            goto do_return;

        assert(in_p <= in_end);
        if (in_p == in_end)
            goto do_return;

        struct zis_object *const in_obj = *in_p;

#define CHECK_TYPE(T) \
do {                  \
    if (zis_likely(!zis_object_is_smallint(in_obj) && zis_object_type(in_obj) == g->type_##T)) \
        break;        \
    if (x->no_type_err_for_nil && in_obj == zis_object_from(g->val_nil))                       \
        goto break_switch;                                                                     \
    status = ZIS_E_TYPE;                                                                       \
    goto do_return;   \
} while(0)            \
// ^^^ CHECK_TYPE() ^^^

        switch (*fmt_p) {
        case '%': {
            const unsigned int reg_tgt = va_arg(x->ap, unsigned int);
#if ZIS_DEBUG
            if (reg_tgt == 0U)
                zis_debug_log(WARN, "API", "zis_read_values(): write REG-0");
#endif // ZIS_DEBUG
            struct zis_object **tgt_ref = api_ref_local(z, reg_tgt);
            if (zis_unlikely(!tgt_ref)) {
                status = ZIS_E_ARG;
                goto do_return;
            }
            *tgt_ref = in_obj;
            break;
        }

        case 'n':
            CHECK_TYPE(Nil);
            break;

        case 'x':
            CHECK_TYPE(Bool);
            *va_arg(x->ap, bool *) =
                zis_bool_obj_value(zis_object_cast(in_obj, struct zis_bool_obj));
            break;

        case 'i': {
            int64_t *const out_p = va_arg(x->ap, int64_t *);
            if (zis_object_is_smallint(in_obj)) {
                *out_p = zis_smallint_from_ptr(in_obj);
                break;
            }
            CHECK_TYPE(Int);
            const int64_t val =
                zis_int_obj_value_l(zis_object_cast(in_obj, struct zis_int_obj));
            if (zis_unlikely(val == INT64_MIN && errno == ERANGE)) {
                status = ZIS_E_BUF;
                goto do_return;
            }
            *out_p = val;
            break;
        }

        case 'f':
            CHECK_TYPE(Float);
            *va_arg(x->ap, double *) =
                zis_float_obj_value(zis_object_cast(in_obj, struct zis_float_obj));
            break;

        case 's': {
            CHECK_TYPE(String);
            char *buf = va_arg(x->ap, char *);
            size_t *sz = va_arg(x->ap, size_t *);
            const size_t n = zis_string_obj_value(
                zis_object_cast(in_obj, struct zis_string_obj), buf, *sz
            );
            if (zis_unlikely(n == (size_t)-1)) {
                status = ZIS_E_BUF;
                goto do_return;
            }
            *sz = n;
            break;
        }

        case '(': {
            CHECK_TYPE(Tuple);
            struct zis_tuple_obj *const tuple_obj =
                zis_object_cast(in_obj, struct zis_tuple_obj);
            if (nested) {
                zis_debug_log(ERROR, "API", "zis_read_values(): nested \"(...)\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            if (fmt_p[1] == '*') {
                fmt_p++;
                count++;
                *va_arg(x->ap, size_t *) = zis_tuple_obj_length(tuple_obj);
            }
            fmt_p++;
            const char *const s_end = strchr(fmt_p, ')');
            if (!s_end) {
                zis_debug_log(ERROR, "API", "zis_read_values(): unmatched \"(...)\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count = (size_t)(s_end - fmt_p);
            struct zis_object *const *const data = zis_tuple_obj_data(tuple_obj);
            PUSH_STATE();
            const int rv = api_read_values_impl(x, data, data + elem_count, s_end, true);
            if (rv)
                return rv;
            PULL_STATE();
            count--;
            assert(*fmt_p == ')');
            break;
        }

        case '[': {
            CHECK_TYPE(Array);
            struct zis_array_obj *const array_obj =
                zis_object_cast(in_obj, struct zis_array_obj);
            if (nested) {
                zis_debug_log(ERROR, "API", "zis_read_values(): nested \"[...]\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            if (fmt_p[1] == '*') {
                fmt_p++;
                count++;
                *va_arg(x->ap, size_t *) = zis_array_obj_length(array_obj);
            }
            fmt_p++;
            const char *const s_end = strchr(fmt_p, ']');
            if (!s_end){
                zis_debug_log(ERROR, "API", "zis_read_values(): unmatched \"[...]\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count = (size_t)(s_end - fmt_p);
            struct zis_object *const *const data = zis_array_obj_data(array_obj);
            PUSH_STATE();
            const int rv = api_read_values_impl(x, data, data + elem_count, s_end, true);
            if (rv)
                return rv;
            PULL_STATE();
            count--;
            assert(*fmt_p == ']');
            break;
        }

        case '-':
            break;

        case '?':
            x->no_type_err_for_nil = true;
            in_p--;
            count--;
            break;

        default:
            if (!*fmt_p)
                goto do_return;
            zis_debug_log(
                ERROR, "API",
                "zis_make_values(): unrecognized specifier '%c'", *fmt_p
            );
            return ZIS_E_ARG;
        }
    break_switch:;

#undef CHECK_TYPE

        assert(status == ZIS_OK);

        in_p++;
        fmt_p++;
        count++;
    }

do_return:
    PUSH_STATE();
    assert(status <= 0);
    return status;

#undef PULL_STATE
#undef PUSH_STATE
}

ZIS_API int zis_read_values(zis_t z, unsigned int reg_begin, const char *fmt, ...) {
    struct zis_object **const reg_beg_p = api_ref_local(z, reg_begin);
    if (zis_unlikely(!reg_beg_p))
        return ZIS_E_IDX;
    struct zis_object **const reg_end = z->callstack->top + 1;
    assert(reg_beg_p < reg_end);

    struct api_read_values_state x;
    x.z = z;
    x.fmt_p = fmt;
    x.count = 0;
    x.no_type_err_for_nil = false;
    va_start(x.ap, fmt);
    const int ret = api_read_values_impl(&x, reg_beg_p, reg_end, (void *)UINTPTR_MAX, false);
    va_end(x.ap);
    assert(ret <= 0);
    return ret == 0 ? x.count : ret;
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

ZIS_API int zis_load_element(
    zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val
) {
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const key = api_get_local(z, reg_key);
    if (zis_unlikely(!key))
        return ZIS_E_IDX;
    struct zis_object **const val_ref = api_ref_local(z, reg_val);
    if (zis_unlikely(!val_ref))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj)))
        return ZIS_E_TYPE;
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        struct zis_object *const val = zis_array_obj_Mx_get_element(z, array, key);
        if (zis_unlikely(!val))
            return ZIS_E_ARG;
        *val_ref = val;
        return ZIS_OK;
    }
    if (obj_type == g->type_Tuple) {
        struct zis_tuple_obj *const tuple = zis_object_cast(obj, struct zis_tuple_obj);
        struct zis_object *const val = zis_tuple_obj_Mx_get_element(z, tuple, key);
        if (zis_unlikely(!val))
            return ZIS_E_ARG;
        *val_ref = val;
        return ZIS_OK;
    }
    return ZIS_E_TYPE;
}

ZIS_API int zis_store_element(
    zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val
) {
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const key = api_get_local(z, reg_key);
    if (zis_unlikely(!key))
        return ZIS_E_IDX;
    struct zis_object *const val = api_get_local(z, reg_val);
    if (zis_unlikely(!val))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj)))
        return ZIS_E_TYPE;
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        const bool ok = zis_array_obj_Mx_set_element(z, array, key, val);
        return ok ? ZIS_OK : ZIS_E_ARG;
    }
    return ZIS_E_TYPE;
}

ZIS_API int zis_insert_element(
    zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val
) {
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const key = api_get_local(z, reg_key);
    if (zis_unlikely(!key))
        return ZIS_E_IDX;
    struct zis_object *const val = api_get_local(z, reg_val);
    if (zis_unlikely(!val))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj)))
        return ZIS_E_TYPE;
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        const bool ok = zis_array_obj_Mx_insert_element(z, array, key, val);
        return ok ? ZIS_OK : ZIS_E_ARG;
    }
    return ZIS_E_TYPE;
}

ZIS_API int zis_remove_element(zis_t z, unsigned int reg_obj, unsigned int reg_key) {
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const key = api_get_local(z, reg_key);
    if (zis_unlikely(!key))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj)))
        return ZIS_E_TYPE;
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        const bool ok = zis_array_obj_Mx_remove_element(z, array, key);
        return ok ? ZIS_OK : ZIS_E_ARG;
    }
    return ZIS_E_TYPE;
}
