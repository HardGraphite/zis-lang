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

static int _print_1(zis_t z, struct zis_object *value, struct zis_stream_obj *stream) {
    if (zis_object_is_smallint(value)) {
        char buffer[24];
        size_t n = zis_smallint_to_str(zis_smallint_from_ptr(value), buffer, sizeof buffer, 10);
        assert(n != (size_t)-1);
        zis_stream_obj_write_chars(stream, buffer, n);
    } else  {
        struct zis_string_obj *str;
        if (zis_object_type(value) == z->globals->type_String) {
            str = zis_object_cast(value, struct zis_string_obj);
        } else {
            str = zis_object_to_string(z, value, false, NULL);
            if (!str)
                return ZIS_THR;
        }
        zis_string_obj_write_to_stream(str, stream);
    }
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(F_print, z, {0, -1, 1}) {
    /*#DOCSTR# func print(*values)
    Prints values. */

    // TODO: re-write this function.

    struct zis_stream_obj *stream = z->globals->val_stream_stdout;
    struct zis_object **reg1 = &z->callstack->frame[1];
    assert(zis_object_type_is(*reg1, z->globals->type_Tuple));
    for (size_t i = 0, n = zis_tuple_obj_length(zis_object_cast(*reg1, struct zis_tuple_obj)); i < n; i++) {
        if (i)
            zis_stream_obj_write_char(stream, ' ');
        const int status = _print_1(z, zis_tuple_obj_data(zis_object_cast(*reg1, struct zis_tuple_obj))[i], stream);
        if (status == ZIS_THR)
            return ZIS_THR;
    }
    zis_stream_obj_write_char(stream, '\n');
    zis_stream_obj_flush_chars(stream);

    z->callstack->frame[0] = zis_object_from(z->globals->val_nil);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(F_input, z, {0, 1, 1}) {
    /*#DOCSTR# input(?prompt :: String) -> line :: String
    The main function. */

    // TODO: re-write this function.

    struct zis_object **reg1 = &z->callstack->frame[1];

    if (zis_object_type_is(*reg1, z->globals->type_String)) {
        struct zis_stream_obj *stream = z->globals->val_stream_stdout;
        if (_print_1(z, *reg1, stream) == ZIS_THR)
            return ZIS_THR;
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
            str = zis_string_obj_concat2(z, zis_object_cast(*reg1, struct zis_string_obj), str);
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

ZIS_NATIVE_FUNC_DEF(F_init, z, {0, 0, 1}) {
    prelude_load_types(z);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF_LIST(
    D_functions,
    { NULL   , &F_init  },
    { "print", &F_print },
    { "input", &F_input },
);

ZIS_NATIVE_MODULE(prelude) = {
    .functions = D_functions,
    .types     = NULL,
    .variables = NULL,
};
