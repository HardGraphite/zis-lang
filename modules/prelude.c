//%% [module]
//%% name = prelude
//%% description = The prelude module.
//%% required = YES
//%% force-embedded = YES

#include <stddef.h>
#include <string.h>

#include <zis.h>

#include <core/attributes.h>
#include <core/context.h>
#include <core/globals.h>
#include <core/locals.h>
#include <core/object.h>
#include <core/stack.h>

#include <core/arrayobj.h>
#include <core/floatobj.h>
#include <core/intobj.h>
#include <core/streamobj.h>
#include <core/stringobj.h>
#include <core/symbolobj.h>
#include <core/tupleobj.h>

static void F_print__print_1(zis_t z, struct zis_object *value, struct zis_stream_obj *stream) {
    char buffer[80];
    struct zis_context_globals *g = z->globals;
    struct zis_type_obj *type = zis_object_type_1(value);
    if (!type) {
        snprintf(buffer, sizeof buffer, "%lli", (long long)zis_smallint_from_ptr(value));
    } else if (type == g->type_Nil) {
        strcpy(buffer, "nil");
    } else if (type == g->type_Bool) {
        strcpy(buffer, value == zis_object_from(g->val_true) ? "true" : "false");
    } else if (type == g->type_Int) {
        struct zis_int_obj *v = zis_object_cast(value, struct zis_int_obj);
        const size_t n = zis_int_obj_value_s(v, buffer, sizeof buffer - 1, 10);
        if (n == (size_t)-1)
            snprintf(buffer, sizeof buffer, "%f", zis_int_obj_value_f(v));
        else
            buffer[n] = 0;
    } else if (type == g->type_Float) {
        struct zis_float_obj *v = zis_object_cast(value, struct zis_float_obj);
        snprintf(buffer, sizeof buffer, "%f", zis_float_obj_value(v));
    } else if (type == g->type_String) {
        struct zis_string_obj *v = zis_object_cast(value, struct zis_string_obj);
        const size_t n = zis_string_obj_value(v, buffer, sizeof buffer - 1);
        if (n == (size_t)-1)
            strcpy(buffer, "\"...\"");
        else
            buffer[n] = 0;
    } else if (type == g->type_Symbol) {
        struct zis_symbol_obj *v = zis_object_cast(value, struct zis_symbol_obj);
        size_t n = zis_symbol_obj_data_size(v);
        if (n >= sizeof buffer)
            n = sizeof buffer;
        memcpy(buffer, zis_symbol_obj_data(v), n);
        buffer[n] = 0;
    } else if (type == g->type_Tuple) {
        zis_locals_decl_1(z, var, struct zis_tuple_obj *v);
        var.v = zis_object_cast(value, struct zis_tuple_obj);
        zis_stream_obj_write_char(stream, '(');
        for (size_t i = 0; i < zis_tuple_obj_length(var.v) ; i++) {
            F_print__print_1(z, zis_tuple_obj_data(var.v)[i], stream);
            zis_stream_obj_write_char(stream, ',');
            zis_stream_obj_write_char(stream, ' ');
        }
        zis_stream_obj_write_char(stream, ')');
        zis_locals_drop(z, var);
        buffer[0] = 0;
    } else if (type == g->type_Array) {
        zis_locals_decl_1(z, var, struct zis_array_obj *v);
        var.v = zis_object_cast(value, struct zis_array_obj);
        zis_stream_obj_write_char(stream, '[');
        for (size_t i = 0; ; i++) {
            struct zis_object *elem = zis_array_obj_get(var.v, i);
            if (!elem)
                break;
            F_print__print_1(z, elem, stream);
            zis_stream_obj_write_char(stream, ',');
            zis_stream_obj_write_char(stream, ' ');
        }
        zis_stream_obj_write_char(stream, ']');
        zis_locals_drop(z, var);
        buffer[0] = 0;
    } else {
        strcpy(buffer, "<?>");
    }
    zis_stream_obj_write_chars(stream, buffer, strlen(buffer));
}

// print(*value)
static int F_print(zis_t z) {
    // TODO: re-write this function.

    struct zis_stream_obj *stream = z->globals->val_stream_stdout;
    struct zis_object **reg1 = &z->callstack->frame[1];
    assert(zis_object_type_is(*reg1, z->globals->type_Tuple));
    for (size_t i = 0, n = zis_tuple_obj_length(zis_object_cast(*reg1, struct zis_tuple_obj)); i < n; i++) {
        if (i)
            zis_stream_obj_write_char(stream, ' ');
        F_print__print_1(z, zis_tuple_obj_data(zis_object_cast(*reg1, struct zis_tuple_obj))[i], stream);
    }
    zis_stream_obj_write_char(stream, '\n');
    zis_stream_obj_flush_chars(stream);

    z->callstack->frame[0] = zis_object_from(z->globals->val_nil);
    return ZIS_OK;
}

// input(?prompt :: String) -> line :: String
static int F_input(zis_t z) {
    // TODO: re-write this function.

    struct zis_object **reg1 = &z->callstack->frame[1];

    if (zis_object_type_is(*reg1, z->globals->type_String)) {
        struct zis_stream_obj *stream = z->globals->val_stream_stdout;
        F_print__print_1(z, *reg1, stream);
        zis_stream_obj_flush_chars(stream);
    }

    struct zis_stream_obj *stream = z->globals->val_stream_stdin;
    for (bool first_read = true;;) {
        char buffer[128];
        size_t n = zis_stream_obj_read_line(stream, buffer, sizeof buffer);
        if (!n) {
            if (first_read) {
                zis_make_exception(z, 0, NULL, 0, "read on a closed stream");
                return ZIS_THR;
            }
            break;
        }
        const bool found_lf = buffer[n - 1] == '\n';
        if (found_lf)
            n--;
        struct zis_string_obj *str = zis_string_obj_new(z, buffer, n);
        if (first_read) {
            first_read = false;
        } else {
            assert(zis_object_type_is(*reg1, z->globals->type_String));
            str = zis_string_obj_concat(z, zis_object_cast(*reg1, struct zis_string_obj), str);
        }
        *reg1 = zis_object_from(str);
        if (found_lf)
            break;
    }

    z->callstack->frame[0] = *reg1;
    return ZIS_OK;
}

/* ----- builtin types ------------------------------------------------------ */

#pragma pack(push, 1)

static const char *const _prelude_types_name[] = {
#define E(X)  #X ,
    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
#undef E
};

static const uint8_t _prelude_types_index[] = {
#define E(X)  offsetof(struct zis_context_globals, type_##X) / sizeof(void *),
    _ZIS_BUILTIN_TYPE_LIST0
    _ZIS_BUILTIN_TYPE_LIST1
#undef E
};

static_assert(sizeof(struct zis_context_globals) / sizeof(void *) <= UINT8_MAX, "_prelude_types_off");

#pragma pack(pop)

zis_cold_fn static void prelude_load_types(struct zis_context *z) {
    const unsigned int type_count = sizeof _prelude_types_name / sizeof _prelude_types_name[0];
    struct zis_context_globals *const g = z->globals;
    struct zis_object **const fp = z->callstack->frame;
    for (unsigned int i = 0; i < type_count; i++) {
        const char *name = _prelude_types_name[i];
        struct zis_type_obj *obj = ((struct zis_type_obj **)g)[_prelude_types_index[i]];
        fp[0] = zis_object_from(obj);
        zis_store_global(z, 0, name, (size_t)-1);
    }
}

/* ----- define the module -------------------------------------------------- */

zis_cold_fn static int F_init(zis_t z) {
    prelude_load_types(z);
    return ZIS_OK;
}

static const struct zis_native_func_def M_funcs[] = {
    { NULL    , {0, 0, 1}, F_init     },
    { "print" , {0, (unsigned char)-1, 1}, F_print    },
    { "input" , {0, 1, 1}, F_input    },
    { NULL    , {0, 0, 0}, NULL       },
};

ZIS_NATIVE_MODULE(prelude) = {
    .name = "prelude",
    .functions = M_funcs,
    .types = NULL,
};
