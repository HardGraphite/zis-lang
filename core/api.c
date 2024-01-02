#include <zis.h>

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "invoke.h"
#include "ndefutil.h"
#include "object.h"
#include "stack.h"

#include "arrayobj.h"
#include "boolobj.h"
#include "exceptobj.h"
#include "floatobj.h"
#include "funcobj.h"
#include "intobj.h"
#include "mapobj.h"
#include "moduleobj.h"
#include "stringobj.h"
#include "symbolobj.h"
#include "tupleobj.h"

#include "zis_config.h"

/* ----- common utilities --------------------------------------------------- */

#if (__GNUC__ + 0 >= 4) || defined(__clang__)
#    define ZIS_API __attribute__((used, visibility("default")))
#else
#    define ZIS_API
#endif

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

static struct zis_func_obj *api_get_current_func(zis_t z) {
    struct zis_callstack *const cs = z->callstack;
    assert(!zis_callstack_empty(cs));
    struct zis_object *x = zis_callstack_frame_info(cs)->prev_frame[0];
    assert(zis_object_type(x) == z->globals->type_Function);
    return zis_object_cast(x, struct zis_func_obj);
}

static struct zis_object *api_get_current_func_or(zis_t z, struct zis_object *alt) {
    struct zis_callstack *const cs = z->callstack;
    if (zis_callstack_empty(cs))
        return alt;
    struct zis_object *func_obj = zis_callstack_frame_info(cs)->prev_frame[0];
    if (zis_object_type(func_obj) != z->globals->type_Function)
        return alt;
    return func_obj;
}

/* ----- zis-api-general ---------------------------------------------------- */

ZIS_API const uint_least16_t zis_version[3] = {
    ZIS_VERSION_MAJOR,
    ZIS_VERSION_MINOR,
    ZIS_VERSION_PATCH,
};

/* ----- zis-api-context ---------------------------------------------------- */

ZIS_API zis_t zis_create(void) {
    return zis_context_create();
}

ZIS_API void zis_destroy(zis_t z) {
    zis_context_destroy(z);
}

ZIS_API zis_panic_handler_t zis_at_panic(zis_t z, zis_panic_handler_t h) {
    zis_panic_handler_t old_h = z->panic_handler;
    z->panic_handler = h;
    return old_h;
}

/* ----- zis-api-natives ---------------------------------------------------- */

ZIS_API int zis_native_block(zis_t z, size_t reg_max, int(*fn)(zis_t, void *), void *arg) {
    { // enter a new frame
        const size_t frame_size = reg_max + 1U;
        if (zis_unlikely(!frame_size))
            zis_context_panic(z, ZIS_CONTEXT_PANIC_SOV);

        struct zis_object *func = api_get_current_func_or(z, zis_object_from(z->globals->val_nil));
        struct zis_object **base_frame = z->callstack->frame;
        zis_callstack_enter(z->callstack, frame_size, NULL);
        struct zis_object **this_frame = z->callstack->frame;
        this_frame[0] = base_frame[0];
        base_frame[0] = func;
    }

    const int ret_val = fn(z, arg);

    { // leave the frame
        struct zis_object *ret_obj = z->callstack->frame[0];
        assert(zis_callstack_frame_info(z->callstack)->return_ip == NULL);
        zis_callstack_leave(z->callstack);
        z->callstack->frame[0] = ret_obj;
    }

    return ret_val;
}

/* ----- zis-api-values ----------------------------------------------------- */

ZIS_API int zis_load_nil(zis_t z, unsigned int reg, unsigned int n) {
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

ZIS_API int zis_make_symbol(zis_t z, unsigned int reg, const char *str, size_t sz) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_symbol_obj *sym_obj = zis_symbol_registry_get(z, str, sz);
    assert(sym_obj);
    *obj_ref = zis_object_from(sym_obj);
    return ZIS_OK;
}

ZIS_API int zis_read_symbol(zis_t z, unsigned int reg, char *buf, size_t *sz) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj) || zis_object_type(obj) != z->globals->type_Symbol))
        return ZIS_E_TYPE;
    struct zis_symbol_obj *const sym_obj = zis_object_cast(obj, struct zis_symbol_obj);
    const size_t sym_sz = zis_symbol_obj_data_size(sym_obj);
    if (buf) {
        if (zis_unlikely(*sz < sym_sz))
            return ZIS_E_BUF;
        memcpy(buf, sym_obj->data, sym_sz);
    }
    *sz = sym_sz;
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

        case 'y': {
            const char *s = va_arg(x->ap, const char *);
            const size_t n = va_arg(x->ap, size_t);
            struct zis_symbol_obj *sym_obj = zis_symbol_registry_get(z, s, n);
            assert(sym_obj);
            *ret_p = zis_object_from(sym_obj);
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

        case '{': {
            if (nested) {
                zis_debug_log(ERROR, "API", "zis_make_values(): nested \"{...}\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            size_t reserve = 0U;
            if (fmt_p[1] == '*') {
                fmt_p++;
                reserve = va_arg(x->ap, size_t);
            }
            fmt_p++;
            const char *const s_end = strchr(fmt_p, '}');
            if (!s_end){
                zis_debug_log(ERROR, "API", "zis_make_values(): unmatched \"{...}\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count_x2 = (size_t)(s_end - fmt_p);
            if (elem_count_x2 & 1u) {
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count = elem_count_x2 / 2;
            zis_map_obj_new_r(z, ret_p, 0.0f, reserve < elem_count ? elem_count : reserve);
            if (elem_count) {
                const size_t tmp_regs_n = elem_count_x2 + 4;
                struct zis_object **tmp_regs =
                    zis_callstack_frame_alloc_temp(z, tmp_regs_n);
                struct zis_object **tmp_regs_set_regs = tmp_regs + elem_count_x2;
                PUSH_STATE();
                const int rv = api_make_values_impl(
                    x, tmp_regs, tmp_regs + elem_count_x2, s_end, true
                );
                PULL_STATE();
                if (rv != ZIS_OK) {
                    zis_callstack_frame_free_temp(z, tmp_regs_n);
                    return rv;
                }
                for (size_t i = 0; i < elem_count_x2; i += 2) {
                    tmp_regs_set_regs[0] = *ret_p,
                    tmp_regs_set_regs[1] = tmp_regs[i],
                    tmp_regs_set_regs[2] = tmp_regs[i + 1];
                    status = zis_map_obj_set_r(z, tmp_regs_set_regs);
                    if (status != ZIS_OK) {
                        zis_callstack_frame_free_temp(z, tmp_regs_n);
                        assert(status == ZIS_THR);
                        return status;
                    }
                }
                zis_callstack_frame_free_temp(z, tmp_regs_n);
                assert(zis_object_type(*ret_p) == z->globals->type_Map);
                assert(zis_map_obj_length(zis_object_cast(*ret_p, struct zis_map_obj)) == elem_count);
            }
            assert(*fmt_p == '}');
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

        case 'y': {
            CHECK_TYPE(Symbol);
            char *buf = va_arg(x->ap, char *);
            size_t *sz = va_arg(x->ap, size_t *);
            struct zis_symbol_obj *const sym_obj =
                zis_object_cast(in_obj, struct zis_symbol_obj);
            const size_t sym_sz = zis_symbol_obj_data_size(sym_obj);
            if (buf) {
                if (zis_unlikely(*sz < sym_sz)) {
                    status = ZIS_E_BUF;
                    goto do_return;
                }
                memcpy(buf, sym_obj->data, sym_sz);
            }
            *sz = sym_sz;
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

        case '{': {
            CHECK_TYPE(Map);
            struct zis_map_obj *const map_obj =
                zis_object_cast(in_obj, struct zis_map_obj);
            if (nested) {
                zis_debug_log(ERROR, "API", "zis_read_values(): nested \"{...}\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            if (fmt_p[1] == '*') {
                fmt_p++;
                count++;
                *va_arg(x->ap, size_t *) = zis_map_obj_length(map_obj);
            }
            fmt_p++;
            const char *const s_end = strchr(fmt_p, '}');
            if (!s_end){
                zis_debug_log(ERROR, "API", "zis_read_values(): unmatched \"{...}\"");
                status = ZIS_E_ARG;
                goto do_return;
            }
            const size_t elem_count = (size_t)(s_end - fmt_p);
            if (elem_count != 0) { // Does not support getting elements.
                status = ZIS_E_ARG;
                goto do_return;
            }
            count--;
            assert(*fmt_p == '}');
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

ZIS_API int zis_make_exception(
    zis_t z, unsigned int reg,
    const char *type, unsigned int reg_data, const char *msg_fmt, ...
) {
    struct zis_object **res_obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!res_obj_ref))
        return ZIS_E_IDX;
    struct zis_object *obj_data = api_get_local(z, reg_data);
    if (zis_unlikely(!obj_data && reg_data != (unsigned int)-1))
        return ZIS_E_IDX;

    va_list msg_args;
    va_start(msg_args, msg_fmt);
    struct zis_exception_obj *const exc_obj =
        zis_exception_obj_vformat(z, type, obj_data, msg_fmt, msg_args);
    va_end(msg_args);

    *res_obj_ref = zis_object_from(exc_obj);
    return ZIS_OK;
}

ZIS_API int zis_read_exception(
    zis_t z, unsigned int reg,
    unsigned int reg_type, unsigned int reg_data, unsigned int reg_what
) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(zis_object_is_smallint(obj) || zis_object_type(obj) != z->globals->type_Exception))
        return ZIS_E_TYPE;
    struct zis_exception_obj *const exc_obj = zis_object_cast(obj, struct zis_exception_obj);

    struct zis_object **type_obj_ref = api_ref_local(z, reg_type);
    struct zis_object **data_obj_ref = api_ref_local(z, reg_data);
    struct zis_object **what_obj_ref = api_ref_local(z, reg_what);
    if (!(type_obj_ref && data_obj_ref && what_obj_ref))
        return ZIS_E_IDX;

    *type_obj_ref = exc_obj->type;
    *data_obj_ref = exc_obj->data;
    *what_obj_ref = exc_obj->what;
    return ZIS_OK;
}

/* ----- zis-api-code ------------------------------------------------------- */

ZIS_API int zis_make_function(
    zis_t z, unsigned int reg,
    const struct zis_native_func_def *def, unsigned int reg_module
) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_func_obj *const func_obj = zis_func_obj_new_native(z, def->meta, def->code);
    *obj_ref = zis_object_from(func_obj);
    struct zis_object *maybe_mod_obj = api_get_local(z, reg_module);
    if (maybe_mod_obj && zis_object_type(maybe_mod_obj) == z->globals->type_Module)
        zis_func_obj_set_module(z, func_obj, zis_object_cast(maybe_mod_obj, struct zis_module_obj));
    return ZIS_OK;
}

ZIS_API int zis_make_module(zis_t z, unsigned int reg, const struct zis_native_module_def *def) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
    struct zis_module_obj *const mod_obj = zis_module_obj_new_r(z, tmp_regs);
    zis_callstack_frame_free_temp(z, 2);
    *obj_ref = zis_object_from(mod_obj);
    zis_module_obj_load_native_def(z, mod_obj, def);
    return ZIS_OK;
}

ZIS_API int zis_invoke(zis_t z, const unsigned int regs[], size_t argc) {
    struct zis_object *const  callable_obj = api_get_local(z, regs[1]);
    struct zis_object **const ret_val_ref  = api_ref_local(z, regs[0]);
    if (zis_unlikely(!(callable_obj && ret_val_ref)))
        return ZIS_E_IDX;

    // prepare + pass_args
    struct zis_func_obj *func_obj;
    if (zis_unlikely(argc == (size_t)-1)) { // --- packed args ---
        struct zis_object *packed_args_obj = api_get_local(z, regs[2]);
        if (zis_unlikely(!packed_args_obj))
            return ZIS_E_IDX;
        if (zis_unlikely(zis_object_is_smallint(packed_args_obj))) {
        packed_args_obj_wrong_type:
            z->callstack->frame[0] = zis_object_from(zis_exception_obj_format(
                z, "type", packed_args_obj, "wrong type of packed arguments"));
            return ZIS_THR;
        } else {
            struct zis_type_obj *const packed_args_obj_type =
                zis_object_type(packed_args_obj);
            if (packed_args_obj_type == z->globals->type_Tuple) {
                struct zis_tuple_obj *const tuple_obj =
                    zis_object_cast(packed_args_obj, struct zis_tuple_obj);
                argc = zis_tuple_obj_length(tuple_obj);
            } else if (packed_args_obj_type == z->globals->type_Array) {
                struct zis_array_obj *const array_obj =
                    zis_object_cast(packed_args_obj, struct zis_array_obj);
                argc            = zis_array_obj_length(array_obj);
                packed_args_obj = zis_object_from(array_obj->_data);
            } else {
                goto packed_args_obj_wrong_type;
            }
        }
        func_obj = zis_invoke_prepare_pa(z, callable_obj, packed_args_obj, argc);
        if (zis_unlikely(!func_obj))
            return ZIS_THR;
    } else {
        if (argc > 1 && regs[3] == (unsigned int)-1) { // --- vector ---
            size_t index = regs[2];
            struct zis_object **const argv = z->callstack->frame + index;
            if (zis_unlikely(argv + argc - 1 > z->callstack->top))
                return ZIS_E_IDX;
            func_obj = zis_invoke_prepare_va(z, callable_obj, argv, argc);
            if (zis_unlikely(!func_obj))
                return ZIS_THR;
        } else { // --- one by one ---
            // FIXME: validate reg indices.
            func_obj = zis_invoke_prepare_da(z, callable_obj, regs + 2, argc);
            if (zis_unlikely(!func_obj))
                return ZIS_THR;
        }
    }

    // call + cleanup.
    const int status = zis_invoke_func(z, func_obj);
    *ret_val_ref = zis_invoke_cleanup(z);
    return status;
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

ZIS_API int zis_load_global(zis_t z, unsigned int reg, const char *name, size_t name_len) {
    struct zis_symbol_obj *name_sym;
    if (name) {
        name_sym = zis_symbol_registry_find(z, name, name_len);
        if (zis_unlikely(!name_sym))
            return ZIS_E_ARG; // Not found.
    } else {
        struct zis_object *name_obj = z->callstack->frame[0];
        if (zis_unlikely(zis_object_type(name_obj) != z->globals->type_Symbol))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object **const obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_module_obj *mod = zis_func_obj_module(api_get_current_func(z));
    struct zis_object *obj = zis_module_obj_get(mod, name_sym);
    if (zis_unlikely(!obj))
        return ZIS_E_ARG;
    *obj_ref = obj;
    return ZIS_OK;
}

ZIS_API int zis_store_global(zis_t z, unsigned int reg, const char *name, size_t name_len) {
    struct zis_symbol_obj *name_sym;
    if (name) {
        name_sym = zis_symbol_registry_get(z, name, name_len);
        assert(name_sym);
    } else {
        struct zis_object *name_obj = z->callstack->frame[0];
        if (zis_unlikely(zis_object_type(name_obj) != z->globals->type_Symbol))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object *const obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_module_obj *mod = zis_func_obj_module(api_get_current_func(z));
    zis_module_obj_set(z, mod, name_sym, obj);
    return ZIS_OK;
}

ZIS_API int zis_load_field(
    zis_t z, unsigned int reg_obj,
    const char *name, size_t name_len, unsigned int reg_val
) {
    struct zis_symbol_obj *name_sym;
    if (name) {
        name_sym = zis_symbol_registry_find(z, name, name_len);
        if (zis_unlikely(!name_sym))
            return ZIS_E_ARG; // Not found.
    } else {
        struct zis_object *name_obj = z->callstack->frame[0];
        if (zis_unlikely(zis_object_type(name_obj) != z->globals->type_Symbol))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object **const val_ref = api_ref_local(z, reg_val);
    if (zis_unlikely(!val_ref))
        return ZIS_E_IDX;
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    if (obj_type == z->globals->type_Module) {
        struct zis_module_obj *const mod = zis_object_cast(obj, struct zis_module_obj);
        struct zis_object *const val = zis_module_obj_get(mod, name_sym);
        if (zis_unlikely(!val))
            return ZIS_E_ARG;
        *val_ref = val;
        return ZIS_OK;
    }
    return ZIS_E_ARG;
}

ZIS_API int zis_store_field(
    zis_t z, unsigned int reg_obj,
    const char *name, size_t name_len, unsigned int reg_val
) {
    struct zis_symbol_obj *name_sym;
    if (name) {
        name_sym = zis_symbol_registry_get(z, name, name_len);
        assert(name_sym);
    } else {
        struct zis_object *name_obj = z->callstack->frame[0];
        if (zis_unlikely(zis_object_type(name_obj) != z->globals->type_Symbol))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const val = api_get_local(z, reg_val);
    if (zis_unlikely(!val))
        return ZIS_E_IDX;
    struct zis_type_obj *const obj_type = zis_object_type(obj);
    if (obj_type == z->globals->type_Module) {
        struct zis_module_obj *const mod = zis_object_cast(obj, struct zis_module_obj);
        zis_module_obj_set(z, mod, name_sym, val);
        return ZIS_OK;
    }
    return ZIS_E_ARG;
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
    if (obj_type == g->type_Map) {
        struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 2);
        tmp_regs[0] = obj, tmp_regs[1] = key;
        const int status = zis_map_obj_get_r(z, tmp_regs);
        *val_ref = tmp_regs[0];
        zis_callstack_frame_free_temp(z, 2);
        if (status != ZIS_OK)
            *val_ref = zis_object_from(g->val_nil);
        return status;
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
    if (obj_type == g->type_Map) {
        struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 4);
        tmp_regs[0] = obj, tmp_regs[1] = key, tmp_regs[2] = val;
        const int status = zis_map_obj_set_r(z, tmp_regs);
        zis_callstack_frame_free_temp(z, 4);
        return status;
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
    if (obj_type == g->type_Map) {
        struct zis_object **tmp_regs = zis_callstack_frame_alloc_temp(z, 3);
        tmp_regs[0] = obj, tmp_regs[1] = key;
        const int status = zis_map_obj_unset_r(z, tmp_regs);
        zis_callstack_frame_free_temp(z, 3);
        return status;
    }
    return ZIS_E_TYPE;
}
