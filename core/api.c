#include <zis.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "attributes.h"
#include "context.h"
#include "debug.h"
#include "globals.h"
#include "invoke.h"
#include "loader.h"
#include "locals.h"
#include "ndefutil.h"
#include "object.h"
#include "stack.h"
#include "strutil.h"

#include "arrayobj.h"
#include "boolobj.h"
#include "bytesobj.h"
#include "exceptobj.h"
#include "floatobj.h"
#include "funcobj.h"
#include "intobj.h"
#include "mapobj.h"
#include "moduleobj.h"
#include "pathobj.h"
#include "platform.h"
#include "streamobj.h"
#include "stringobj.h"
#include "symbolobj.h"
#include "tupleobj.h"
#include "typeobj.h"

#include "zis_config.h"

/* ----- common utilities --------------------------------------------------- */

#if !ZIS_EXPORT_API
#    error "macro ZIS_EXPORT_API must be true"
#endif
#if ZIS_IMPORT_API
#    error "macro ZIS_IMPORT_API must be false"
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#    define ZIS_API __declspec(dllexport)
#elif (__GNUC__ + 0 >= 4) || defined(__clang__)
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
    assert(zis_object_type_is(x, z->globals->type_Function));
    return zis_object_cast(x, struct zis_func_obj);
}

static struct zis_object *api_get_current_func_or(zis_t z, struct zis_object *alt) {
    struct zis_callstack *const cs = z->callstack;
    if (zis_callstack_empty(cs))
        return alt;
    struct zis_object *func_obj = zis_callstack_frame_info(cs)->prev_frame[0];
    if (!zis_object_type_is(func_obj, z->globals->type_Function))
        return alt;
    return func_obj;
}

zis_noinline static int api_format_exception_with_name(
    zis_t z, const char *restrict type, struct zis_object *data,
    const char *restrict fmt, const char *name, size_t name_len, unsigned int alt_name_reg
) {
    if (name) {
        if (name_len == (size_t)-1)
            name_len = strlen(name);
    } else {
        struct zis_object *name_obj = api_get_local(z, alt_name_reg);
        if (name_obj && zis_object_type_is(name_obj, z->globals->type_Symbol)) {
            struct zis_symbol_obj *name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
            name = zis_symbol_obj_data(name_sym);
            name_len = zis_symbol_obj_data_size(name_sym);
        } else {
            name = "?";
            name_len = 0;
        }
    }
    zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
        z, type, data, fmt, (int)name_len, name
    )));
    return ZIS_THR;
}

/* ----- zis-api-general ---------------------------------------------------- */

ZIS_API const struct zis_build_info zis_build_info = {
    .system    = ZIS_SYSTEM_NAME,
    .machine   = ZIS_ARCH_NAME,
    .compiler  = ZIS_BUILD_COMPILER_INFO,
    .extra     =
#ifdef ZIS_BUILD_EXTRA_INFO
        ZIS_BUILD_EXTRA_INFO
#else // !ZIS_BUILD_EXTRA_INFO
        NULL
#endif // ZIS_BUILD_EXTRA_INFO
    ,
    .timestamp = ZIS_BUILD_TIMESTAMP,
    .version   = {
        ZIS_VERSION_MAJOR,
        ZIS_VERSION_MINOR,
        ZIS_VERSION_PATCH,
    },
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
        zis_callstack_enter(z->callstack, frame_size, NULL, z->callstack->frame);
        struct zis_object **this_frame = z->callstack->frame;
        this_frame[0] = base_frame[0];
        base_frame[0] = func;
    }

    const int ret_val = fn(z, arg);

    { // leave the frame
        struct zis_object *ret_obj = z->callstack->frame[0];
        assert(zis_callstack_frame_info(z->callstack)->caller_ip == NULL);
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
    assert(!zis_object_type_is(obj, z->globals->type_Nil));
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
    if (zis_unlikely(!zis_object_type_is(obj, z->globals->type_Bool)))
        return ZIS_E_TYPE;
    *val = zis_bool_obj_value(zis_object_cast(obj, struct zis_bool_obj));
    return ZIS_OK;
}

ZIS_API int zis_make_int(zis_t z, unsigned int reg, int64_t val) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    *obj_ref = zis_int_obj_or_smallint(z, val);
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
            zis_int_obj_value_i(zis_object_cast(obj, struct zis_int_obj));
        if (zis_unlikely(v_i64 == INT64_MIN && errno == ERANGE))
            return ZIS_E_BUF;
        *val = v_i64;
        return ZIS_OK;
    }
    return ZIS_E_TYPE;
}

ZIS_API int zis_make_int_s(zis_t z, unsigned int reg, const char *str, size_t str_sz, int base) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    if (str_sz == (size_t)-1)
        str_sz = strlen(str);
    base = abs(base);
    if (zis_unlikely(!(str_sz && 2 <= base && base <= 36)))
        return ZIS_E_ARG;
    const char *const str_end = str + str_sz, *str_end_1 = str_end;
    struct zis_object *obj = zis_int_obj_or_smallint_s(z, str, &str_end_1, base);
    if (zis_unlikely(str_end != str_end_1))
        return ZIS_E_ARG;
    *obj_ref = obj;
    return ZIS_OK;
}

ZIS_API int zis_read_int_s(zis_t z, unsigned int reg, char *buf, size_t *buf_sz, int base) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    size_t n;
    if (zis_object_is_smallint(obj))
        n = zis_smallint_to_str(zis_smallint_from_ptr(obj), buf, *buf_sz, base);
    else if (zis_object_type(obj) == z->globals->type_Int)
        n = zis_int_obj_value_s(zis_object_cast(obj, struct zis_int_obj), buf, *buf_sz, base);
    else
        return ZIS_E_TYPE;
    if (zis_unlikely(n == (size_t)-1))
        return ZIS_E_BUF;
    *buf_sz = n;
    return ZIS_OK;
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
    if (zis_unlikely(!zis_object_type_is(obj, z->globals->type_Float)))
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
    if (zis_unlikely(!zis_object_type_is(obj, z->globals->type_String)))
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
    if (zis_unlikely(!zis_object_type_is(obj, z->globals->type_Symbol)))
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

ZIS_API int zis_make_bytes(zis_t z, unsigned int reg, const void *data, size_t sz) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    *obj_ref = zis_object_from(zis_bytes_obj_new(z, data, sz));
    return ZIS_OK;
}

ZIS_API int zis_read_bytes(zis_t z, unsigned int reg, void *buf, size_t *sz) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(!zis_object_type_is(obj, z->globals->type_Bytes)))
        return ZIS_E_TYPE;
    struct zis_bytes_obj *const bytes_obj = zis_object_cast(obj, struct zis_bytes_obj);
    const size_t bytes_sz = zis_bytes_obj_size(bytes_obj);
    if (buf) {
        if (zis_unlikely(*sz < bytes_sz))
            return ZIS_E_BUF;
        memcpy(buf, zis_bytes_obj_data(bytes_obj), bytes_sz);
    }
    *sz = bytes_sz;
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
            *ret_p = zis_int_obj_or_smallint(z, va_arg(x->ap, int64_t));
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
            *ret_p = zis_object_from(zis_map_obj_new(z, 0.0f, reserve < elem_count ? elem_count : reserve));
            if (elem_count) {
                struct zis_object **tmp_regs =
                    zis_callstack_frame_alloc_temp(z, elem_count_x2);
                PUSH_STATE();
                const int rv = api_make_values_impl(
                    x, tmp_regs, tmp_regs + elem_count_x2, s_end, true
                );
                PULL_STATE();
                if (rv != ZIS_OK) {
                    zis_callstack_frame_free_temp(z, elem_count_x2);
                    return rv;
                }
                for (size_t i = 0; i < elem_count_x2; i += 2) {
                    assert(zis_object_type_is(*ret_p, z->globals->type_Map));
                    status = zis_map_obj_set(
                        z, zis_object_cast(*ret_p, struct zis_map_obj),
                        tmp_regs[i], tmp_regs[i + 1]
                    );
                    if (status != ZIS_OK) {
                        zis_callstack_frame_free_temp(z, elem_count_x2);
                        assert(status == ZIS_THR);
                        return status;
                    }
                }
                zis_callstack_frame_free_temp(z, elem_count_x2);
                assert(zis_object_type_is(*ret_p, z->globals->type_Map));
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
    if (zis_likely(zis_object_type_is(in_obj, g->type_##T))) \
        break;        \
    if (x->no_type_err_for_nil && in_obj == zis_object_from(g->val_nil)) \
        goto break_switch;                                   \
    status = ZIS_E_TYPE;                                     \
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
                zis_int_obj_value_i(zis_object_cast(in_obj, struct zis_int_obj));
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

ZIS_API int zis_read_exception(zis_t z, unsigned int reg, int flag, unsigned int reg_out) {
    struct zis_object *obj = api_get_local(z, reg);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    if (zis_unlikely(!zis_object_type_is(obj, z->globals->type_Exception)))
        return ZIS_E_TYPE;
    struct zis_exception_obj *const exc_obj = zis_object_cast(obj, struct zis_exception_obj);
    struct zis_object **out_obj_ref = api_ref_local(z, reg_out);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;

    switch (flag) {
    case ZIS_RDE_TEST:
        break;
    case ZIS_RDE_TYPE:
        *out_obj_ref = exc_obj->type;
        break;
    case ZIS_RDE_DATA:
        *out_obj_ref = exc_obj->data;
        break;
    case ZIS_RDE_WHAT:
        *out_obj_ref = exc_obj->what;
        break;
    case ZIS_RDE_DUMP:
        zis_exception_obj_print(
            z, exc_obj,
            zis_object_type_is(*out_obj_ref, z->globals->type_Stream) ?
                zis_object_cast(*out_obj_ref, struct zis_stream_obj) : NULL
        );
        break;
    default:
        return ZIS_E_ARG;
    }

    return ZIS_OK;
}

struct _api_make_stream_context {
    int stream_obj_flags, api_flags;
    va_list api_args;
    struct zis_context *z;
    struct zis_object **res_obj_ref;
};

static int _api_make_stream_open_file_fn(const zis_path_char_t *path, void *_arg) {
    struct _api_make_stream_context *const x =_arg;
    const char *const encoding = va_arg(x->api_args, char *);
    int flags = x->stream_obj_flags;
    if (encoding) {
        flags |= ZIS_STREAM_OBJ_TEXT;
        if (!encoding[0] || zis_str_icmp(encoding, "UTF-8") == 0)
            flags |= ZIS_STREAM_OBJ_UTF8;
        else
            return ZIS_E_ARG; // Supports UTF-8 only.
    }
    struct zis_stream_obj *stream_obj = zis_stream_obj_new_file(x->z, path, flags);
    if (!stream_obj)
        return ZIS_THR;
    *x->res_obj_ref = zis_object_from(stream_obj);
    return ZIS_OK;
}

static int _api_make_stream_get_stdio(struct _api_make_stream_context *restrict x) {
    struct zis_stream_obj *stream_obj;
    const struct zis_context_globals *const g = x->z->globals;
    switch (va_arg(x->api_args, int)) {
    case 0 : stream_obj = g->val_stream_stdin ; break;
    case 1 : stream_obj = g->val_stream_stdout; break;
    case 2 : stream_obj = g->val_stream_stderr; break;
    default: return ZIS_E_ARG;
    }
    *x->res_obj_ref = zis_object_from(stream_obj);
    return ZIS_OK;
}

static int _api_make_stream_open_str(struct _api_make_stream_context *restrict x) {
    const char *str = va_arg(x->api_args, const char *);
    struct zis_stream_obj *stream_obj;
    if (str) {
        const size_t str_sz = va_arg(x->api_args, size_t);
        const bool str_static = x->api_flags & ZIS_IOS_STATIC;
        stream_obj = zis_stream_obj_new_str(x->z, str, str_sz, str_static);
    } else {
        const unsigned int reg = va_arg(x->api_args, unsigned int);
        struct zis_object *obj = api_get_local(x->z, reg);
        if (zis_unlikely(!obj))
            return ZIS_E_IDX;
        if (zis_unlikely(!zis_object_type_is(obj, x->z->globals->type_String)))
            return ZIS_E_TYPE;
        stream_obj = zis_stream_obj_new_strob(x->z, zis_object_cast(obj, struct zis_string_obj));
    }
    *x->res_obj_ref = zis_object_from(stream_obj);
    return ZIS_OK;
}

#define ZIS_STREAM_TYPE_MASK 0x0f

static int api_stream_flags_conv(int api_flags) {
    int result = 0;
    if (api_flags & ZIS_IOS_WRONLY) {
        if (api_flags & ZIS_IOS_RDONLY)
            return 0;
        result |= ZIS_STREAM_OBJ_MODE_OUT;
    } else {
        result |= ZIS_STREAM_OBJ_MODE_IN;
    }
    if (api_flags & ZIS_IOS_WINEOL)
        result |= ZIS_STREAM_OBJ_CRLF;
    return result;
}

ZIS_API int zis_make_stream(zis_t z, unsigned int reg, int flags, ...) {
    struct _api_make_stream_context context;
    context.stream_obj_flags = api_stream_flags_conv(flags);
    context.api_flags = flags;
    if (zis_unlikely(!context.stream_obj_flags))
        return ZIS_E_ARG;
    context.res_obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!context.res_obj_ref))
        return ZIS_E_IDX;
    context.z = z;
    int status;
    va_start(context.api_args, flags);
    switch (flags & ZIS_STREAM_TYPE_MASK) {
    case ZIS_IOS_FILE:
        status = zis_path_with_temp_path_from_str(
            va_arg(context.api_args, const char *),
            _api_make_stream_open_file_fn, &context
        );
        break;
    case ZIS_IOS_STDX:
        status = _api_make_stream_get_stdio(&context);
        break;
    case ZIS_IOS_TEXT:
        status = _api_make_stream_open_str(&context);
        break;
    default:
        status = ZIS_E_ARG;
        break;
    }
    va_end(context.api_args);
    return status;
}

/* ----- zis-api-code ------------------------------------------------------- */

ZIS_API int zis_make_function(
    zis_t z, unsigned int reg,
    const struct zis_native_func_def *def, unsigned int reg_module
) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_func_obj_meta func_obj_meta;
    if (zis_unlikely(!zis_func_obj_meta_conv(&func_obj_meta, def->meta)))
        return ZIS_E_ARG;
    struct zis_func_obj *const func_obj = zis_func_obj_new_native(z, func_obj_meta, def->code);
    *obj_ref = zis_object_from(func_obj);
    struct zis_object *maybe_mod_obj = api_get_local(z, reg_module);
    if (maybe_mod_obj && zis_object_type_is(maybe_mod_obj, z->globals->type_Module))
        zis_func_obj_set_module(z, func_obj, zis_object_cast(maybe_mod_obj, struct zis_module_obj));
    return ZIS_OK;
}

ZIS_API int zis_make_type(
    zis_t z, unsigned int reg,
    const struct zis_native_type_def *def
) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_type_obj *type_obj = zis_type_obj_new(z);
    *obj_ref = zis_object_from(type_obj);
    zis_type_obj_load_native_def(z, type_obj, def);
    return ZIS_OK;
}

ZIS_API int zis_make_module(zis_t z, unsigned int reg, const struct zis_native_module_def *def) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_module_obj *mod_obj = zis_module_obj_new(z, true);
    *obj_ref = zis_object_from(mod_obj);
    return zis_module_obj_do_init(z, zis_module_obj_load_native_def(z, mod_obj, def));
}

ZIS_API int zis_invoke(zis_t z, const unsigned int regs[], size_t argc) {
    struct zis_object **const ret_val_ref  = api_ref_local(z, regs[0]);
    if (zis_unlikely(!ret_val_ref))
        return ZIS_E_IDX;

    // prepare + pass_args
    struct zis_func_obj *func_obj;
    if (zis_unlikely(argc == (size_t)-1)) { // --- packed args ---
        struct zis_object *const callable_obj = api_get_local(z, regs[1]);
        if (zis_unlikely(!callable_obj))
            return ZIS_E_IDX;
        struct zis_object *packed_args_obj = api_get_local(z, regs[2]);
        if (zis_unlikely(!packed_args_obj))
            return ZIS_E_IDX;
        struct zis_type_obj *const packed_args_obj_type = zis_object_type_1(packed_args_obj);
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
            zis_context_set_reg0(z, zis_object_from(zis_exception_obj_format(
                z, "type", packed_args_obj, "wrong type of packed arguments"
            )));
            return ZIS_THR;
        }
        func_obj = zis_invoke_prepare_pa(z, callable_obj, ret_val_ref, packed_args_obj, argc);
        if (zis_unlikely(!func_obj))
            return ZIS_THR;
    } else {
        struct zis_object *callable_obj;
        if (regs[1] != (unsigned int)-1) {
            callable_obj = api_get_local(z, regs[1]);
            if (zis_unlikely(!callable_obj))
                return ZIS_E_IDX;
        } else {
            callable_obj = NULL;
        }
        if (argc > 1 && regs[3] == (unsigned int)-1) { // --- vector ---
            size_t index = regs[2];
            struct zis_object **const argv = z->callstack->frame + index;
            if (zis_unlikely(argv + argc - 1 > z->callstack->top))
                return ZIS_E_IDX;
            func_obj = zis_invoke_prepare_va(z, callable_obj, ret_val_ref, argv, argc);
            if (zis_unlikely(!func_obj))
                return ZIS_THR;
        } else { // --- one by one ---
            // FIXME: validate reg indices.
            func_obj = zis_invoke_prepare_da(z, callable_obj, ret_val_ref, regs + 2, argc);
            if (zis_unlikely(!func_obj))
                return ZIS_THR;
        }
    }

    // call.
    return zis_invoke_func(z, func_obj);
}

static int api_import_by_name(zis_t z, struct zis_object **res_ref, const char *name) {
    struct zis_symbol_obj *const name_sym =
        zis_symbol_registry_get(z, name, (size_t)-1);
    const int loader_flags = ZIS_MOD_LDR_SEARCH_LOADED | ZIS_MOD_LDR_UPDATE_LOADED;
    struct zis_module_obj *const mod =
        zis_module_loader_import(z, NULL, name_sym, NULL, loader_flags);
    if (!mod)
        return ZIS_THR;
    *res_ref = zis_object_from(mod);
    return ZIS_OK;
}

static int _api_import_by_path_fn(const zis_path_char_t *path, void *_z) {
    zis_t z = _z;
    struct zis_path_obj *const path_obj = zis_path_obj_new(z, path, (size_t)-1);
    struct zis_module_obj *const mod = zis_module_loader_import_file(z, NULL, path_obj);
    if (!mod)
        return ZIS_THR;
    z->callstack->frame[0] = zis_object_from(mod);
    return ZIS_OK;
}

static int api_import_by_path(zis_t z, struct zis_object **res_ref, const char *path) {
    const int status = zis_path_with_temp_path_from_str(path, _api_import_by_path_fn, z);
    if (status == ZIS_OK)
        *res_ref = z->callstack->frame[0];
    return status;
}

static int api_import_compile_code(zis_t z, struct zis_object **res_ref, const char *code) {
    struct zis_stream_obj *source_stream;
    if (code) {
        source_stream = zis_stream_obj_new_str(z, code, (size_t)-1, true);
    } else {
        struct zis_object *obj = zis_context_get_reg0(z);
        if (!zis_object_type_is(obj, z->globals->type_Stream))
            return ZIS_E_TYPE;
        source_stream = zis_object_cast(obj, struct zis_stream_obj);
    }
    struct zis_module_obj *const mod = zis_module_loader_import_source(z, NULL, source_stream);
    if (!mod)
        return ZIS_THR;
    *res_ref = zis_object_from(mod);
    return ZIS_OK;
}

static int _api_import_add_path_fn(const zis_path_char_t *path, void *_z) {
    zis_t z = _z;
    zis_module_loader_add_path(z, zis_path_obj_new(z, path, (size_t)-1));
    return ZIS_OK;
}

static int api_import_add_path(zis_t z, const char *path) {
    return zis_path_with_temp_path_from_str(path, _api_import_add_path_fn, z);
}

static int api_import_call_main(zis_t z, struct zis_object **res_ref) {
    assert(zis_object_type_is(*res_ref, z->globals->type_Module));
    struct zis_object *main_fn = zis_module_obj_get(
        zis_object_cast(*res_ref, struct zis_module_obj),
        zis_symbol_registry_get(z, "main", (size_t)-1)
    );
    if (!main_fn || !zis_object_type_is(main_fn, z->globals->type_Function)) {
        zis_debug_log(WARN, "API", "the main function is not defined");
        return ZIS_OK;
    }

    int status;

    const struct zis_func_obj_meta func_meta = zis_object_cast(main_fn, struct zis_func_obj)->meta;
    struct zis_func_obj *func_obj;
    if (!func_meta.na && !func_meta.no) {
        func_obj = zis_invoke_prepare_va(z, main_fn, NULL, NULL, 0);
    } else {
        int64_t argc_i64, argv_i64;
        status = zis_read_int(z, 1, &argc_i64);
        if (status != ZIS_OK)
            return status;
        status = zis_read_int(z, 2, &argv_i64);
        if (status != ZIS_OK)
            return status;
        int argc = (int)argc_i64;
        char **argv = (char **)(intptr_t)argv_i64;
        if (argc < 0 || argc > INT16_MAX)
            return ZIS_E_ARG;

        zis_locals_decl_1(z, var, struct zis_array_obj *args);
        var.args = zis_array_obj_new(z, NULL, (size_t)argc);
        for (int i = 0; i < argc; i++) {
            struct zis_string_obj *arg = zis_string_obj_new(z, argv[i], (size_t)-1);
            zis_array_obj_set(var.args, (size_t)i, zis_object_from(arg));
        }
        func_obj = zis_invoke_prepare_va(z, main_fn, NULL, (struct zis_object **)&var.args, 1);
        zis_locals_drop(z, var);
    }
    if (func_obj) {
        assert(zis_object_from(func_obj) == main_fn);
        status = zis_invoke_func(z, func_obj);
    } else {
        status = ZIS_THR;
    }

    return status;
}

#define ZIS_IMP_TYPE_MASK 0x0f

ZIS_API int zis_import(zis_t z, unsigned int reg, const char *what, int flags) {
    struct zis_object **obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;

    int status;
    switch (flags & ZIS_IMP_TYPE_MASK) {
    case ZIS_IMP_NAME:
        status = api_import_by_name(z, obj_ref, what);
        break;
    case ZIS_IMP_PATH:
        status = api_import_by_path(z, obj_ref, what);
        break;
    case ZIS_IMP_CODE:
        status = api_import_compile_code(z, obj_ref, what);
        break;
    case ZIS_IMP_ADDP:
        return api_import_add_path(z, what);
    default:
        return ZIS_E_ARG;
    }
    if (status != ZIS_OK)
        return status;

    if (flags & ZIS_IMP_MAIN)
        status = api_import_call_main(z, obj_ref);

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

zis_noinline static int api_load_global_err_not_found(zis_t z, const char *name, size_t name_len) {
    return api_format_exception_with_name(
        z, "key", NULL, "variable `%.*s' is not defined", name, name_len, 0
    );
}

ZIS_API int zis_load_global(zis_t z, unsigned int reg, const char *name, size_t name_len) {
    struct zis_symbol_obj *name_sym;
    if (name) {
        name_sym = zis_symbol_registry_find(z, name, name_len);
        if (zis_unlikely(!name_sym))
            return api_load_global_err_not_found(z, name, name_len);
    } else {
        struct zis_object *name_obj = z->callstack->frame[0];
        if (zis_unlikely(!zis_object_type_is(name_obj, z->globals->type_Symbol)))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object **const obj_ref = api_ref_local(z, reg);
    if (zis_unlikely(!obj_ref))
        return ZIS_E_IDX;
    struct zis_module_obj *mod = zis_func_obj_module(api_get_current_func(z));
    struct zis_object *obj = zis_module_obj_get(mod, name_sym);
    if (zis_unlikely(!obj))
        return api_load_global_err_not_found(z, name, name_len);
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
        if (zis_unlikely(!zis_object_type_is(name_obj, z->globals->type_Symbol)))
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

zis_noinline static int api_load_field_err_not_found(
    zis_t z, struct zis_object *obj, const char *name, size_t name_len
) {
    return api_format_exception_with_name(
        z, "key", obj, "no field named `%.*s'", name, name_len, 0
    );
}

ZIS_API int zis_load_field(
    zis_t z, unsigned int reg_obj,
    const char *name, size_t name_len, unsigned int reg_val
) {
    struct zis_symbol_obj *name_sym;
    if (name) {
        name_sym = zis_symbol_registry_find(z, name, name_len);
        if (zis_unlikely(!name_sym))
            return api_load_field_err_not_found(z, api_get_local(z, reg_obj), name, name_len);
    } else {
        struct zis_object *name_obj = z->callstack->frame[0];
        if (zis_unlikely(!zis_object_type_is(name_obj, z->globals->type_Symbol)))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object **const val_ref = api_ref_local(z, reg_val);
    if (zis_unlikely(!val_ref))
        return ZIS_E_IDX;
    struct zis_type_obj *const obj_type = zis_object_type_1(obj);
    if (obj_type == z->globals->type_Module) {
        struct zis_module_obj *const mod = zis_object_cast(obj, struct zis_module_obj);
        struct zis_object *const val = zis_module_obj_get(mod, name_sym);
        if (zis_unlikely(!val))
            return ZIS_E_ARG;
        *val_ref = val;
        return ZIS_OK;
    } else {
        const size_t index = zis_type_obj_find_field(obj_type, name_sym);
        if (zis_unlikely(index == (size_t)-1))
            return api_load_field_err_not_found(z, api_get_local(z, reg_obj), name, name_len);
        assert(index < zis_object_slot_count(obj));
        *val_ref = zis_object_get_slot(obj, index);
        return ZIS_OK;
    }
}

zis_noinline static int api_store_field_err_not_found(
    zis_t z, struct zis_object *obj, const char *name, size_t name_len
) {
    return api_format_exception_with_name(
        z, "key", obj, "no field named `%.*s'", name, name_len, 0
    );
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
        if (zis_unlikely(!zis_object_type_is(name_obj, z->globals->type_Symbol)))
            return ZIS_E_ARG;
        name_sym = zis_object_cast(name_obj, struct zis_symbol_obj);
    }
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const val = api_get_local(z, reg_val);
    if (zis_unlikely(!val))
        return ZIS_E_IDX;
    struct zis_type_obj *const obj_type = zis_object_type_1(obj);
    if (obj_type == z->globals->type_Module) {
        struct zis_module_obj *const mod = zis_object_cast(obj, struct zis_module_obj);
        zis_module_obj_set(z, mod, name_sym, val);
        return ZIS_OK;
    } else {
        const size_t index = zis_type_obj_find_field(obj_type, name_sym);
        if (zis_unlikely(index == (size_t)-1))
            return api_store_field_err_not_found(z, api_get_local(z, reg_obj), name, name_len);
        assert(index < zis_object_slot_count(obj));
        zis_object_set_slot(obj, index, val);
        return ZIS_OK;
    }
}

zis_noinline static int api_xxx_element_err_not_subscriptable(
    zis_t z, struct zis_object *obj
) {
    return api_format_exception_with_name(
        z, "type", obj, "not subscriptable", NULL, 0, (unsigned int)-1
    );
}

zis_noinline static int api_xxx_element_err_look_up(
    zis_t z, struct zis_object *obj, const char *restrict key_desc, struct zis_object *key
) {
    struct zis_object *data = NULL;
    if (obj) {
        zis_locals_decl_1(z, var, struct zis_object *vec[2]);
        var.vec[0] = obj, var.vec[1] = key ? key : zis_object_from(z->globals->val_nil);
        data = zis_object_from(zis_tuple_obj_new(z, var.vec, 2));
        zis_locals_drop(z, var);
    }
    return api_format_exception_with_name(
        z, "key", data, "invalid %.*s", key_desc, strlen(key_desc), 0
    );
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
    struct zis_type_obj *const obj_type = zis_object_type_1(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        struct zis_object *const val = zis_array_obj_Mx_get_element(z, array, key);
        if (zis_unlikely(!val))
            return api_xxx_element_err_look_up(z, obj, "index", key);
        *val_ref = val;
        return ZIS_OK;
    }
    if (obj_type == g->type_Tuple) {
        struct zis_tuple_obj *const tuple = zis_object_cast(obj, struct zis_tuple_obj);
        struct zis_object *const val = zis_tuple_obj_Mx_get_element(z, tuple, key);
        if (zis_unlikely(!val))
            return api_xxx_element_err_look_up(z, obj, "index", key);
        *val_ref = val;
        return ZIS_OK;
    }
    if (obj_type == g->type_Map) {
        struct zis_map_obj *const map = zis_object_cast(obj, struct zis_map_obj);
        const int status = zis_map_obj_get(z, map, key, val_ref);
        if (status != ZIS_OK)
            *val_ref = zis_object_from(g->val_nil);
        if (status == ZIS_E_ARG) // TODO: let `zis_map_obj_get()` not return ZIS_E_ARG
            return api_xxx_element_err_look_up(z, obj, "index", key);
        return status;
    }
    return api_xxx_element_err_not_subscriptable(z, obj);
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
    struct zis_type_obj *const obj_type = zis_object_type_1(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        const bool ok = zis_array_obj_Mx_set_element(z, array, key, val);
        if (!ok)
            return api_xxx_element_err_look_up(z, obj, "index", key);
        return ZIS_OK;
    }
    if (obj_type == g->type_Map) {
        struct zis_map_obj *const map = zis_object_cast(obj, struct zis_map_obj);
        return zis_map_obj_set(z, map, key, val);
    }
    return api_xxx_element_err_not_subscriptable(z, obj);
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
    struct zis_type_obj *const obj_type = zis_object_type_1(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        const bool ok = zis_array_obj_Mx_insert_element(z, array, key, val);
        if (!ok)
            return api_xxx_element_err_look_up(z, obj, "index", key);
        return ZIS_OK;
    }
    return api_xxx_element_err_not_subscriptable(z, obj);
}

ZIS_API int zis_remove_element(zis_t z, unsigned int reg_obj, unsigned int reg_key) {
    struct zis_object *const obj = api_get_local(z, reg_obj);
    if (zis_unlikely(!obj))
        return ZIS_E_IDX;
    struct zis_object *const key = api_get_local(z, reg_key);
    if (zis_unlikely(!key))
        return ZIS_E_IDX;
    struct zis_type_obj *const obj_type = zis_object_type_1(obj);
    const struct zis_context_globals *const g = z->globals;
    if (obj_type == g->type_Array) {
        struct zis_array_obj *const array = zis_object_cast(obj, struct zis_array_obj);
        const bool ok = zis_array_obj_Mx_remove_element(z, array, key);
        if (!ok)
            return api_xxx_element_err_look_up(z, obj, "index", key);
        return ZIS_OK;
    }
    if (obj_type == g->type_Map) {
        struct zis_map_obj *const map = zis_object_cast(obj, struct zis_map_obj);
        return zis_map_obj_unset(z, map, key); // FIXME: may returns ZIS_E_ARG.
    }
    return api_xxx_element_err_not_subscriptable(z, obj);
}
