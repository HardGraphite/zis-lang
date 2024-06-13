//%% [module]
//%% name = hello
//%% description = Say hello.

#include <stdio.h>
#include <stdlib.h>

#include <zis.h>

ZIS_NATIVE_FUNC_DEF(F_hello, z, {1, 0, 1}) {
    /*#DOCSTR# func hello(who :: String)
    Prints "Hello, {who}!\n" to stdout. */
    size_t who_sz;
    char *who;
    zis_if_err (zis_read_string(z, 1, NULL, &who_sz)) {
        zis_make_exception(z, 0, NULL, 1, "not a string");
        return ZIS_THR;
    }
    who = malloc(who_sz);
    zis_read_string(z, 1, who, &who_sz);
    printf("Hello, %.*s!\n", (int)who_sz, who);
    free(who);
    zis_load_nil(z, 0, 1);
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(F_main, z, {1, 0, 3}) {
    /*#DOCSTR# func main(args :: Array[String])
    The main function. */
    zis_if_err (zis_load_global(z, 2, "hello", (size_t)-1)) {
        zis_make_exception(z, 0, NULL, (unsigned)-1, "cannot say hello");
        return ZIS_THR;
    }
    size_t argc;
    if (!zis_read_values(z, 1, "[*]", &argc))
        return ZIS_OK;
    for (size_t i = 1; i < argc; i++) {
        zis_make_int(z, 0, (int64_t)i + 1);
        zis_load_element(z, 1, 0, 3);
        zis_if_thr (zis_invoke(z, (unsigned[]){0, 2, 3}, 1))
            return ZIS_THR;
    }
    return ZIS_OK;
}

ZIS_NATIVE_FUNC_DEF(F_init, z, {0, 0, 1}) {
    /*#DOCSTR# func <module_init>()
    Prints "Hello, World!\n". */
    zis_if_err (zis_load_global(z, 0, "hello", (size_t)-1)) {
        zis_make_exception(z, 0, NULL, (unsigned)-1, "cannot say hello");
        return ZIS_THR;
    }
    zis_make_string(z, 1, "World", (size_t)-1);
    return zis_invoke(z, (unsigned[]){0, 0, 1}, 1);
}

ZIS_NATIVE_FUNC_DEF_LIST(
    D_functions,
    { NULL   , &F_init  },
    { "main" , &F_main  },
    { "hello", &F_hello },
);

ZIS_NATIVE_MODULE(hello) = {
    .functions = D_functions,
    .types     = NULL,
    .variables = NULL,
};
