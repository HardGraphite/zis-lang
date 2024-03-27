//%% [module]
//%% name = hello
//%% description = Say hello.

#include <stdio.h>
#include <stdlib.h>

#include <zis.h>

// hello(who :: String)
// # Prints "Hello, {who}!\n" to stdout.
static int F_hello(zis_t z) {
    size_t who_sz;
    char *who;
    if (zis_read_string(z, 1, NULL, &who_sz) != ZIS_OK) {
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

// main(args).
static int F_main(zis_t z) {
    if (zis_load_global(z, 2, "hello", (size_t)-1) != ZIS_OK) {
        zis_make_exception(z, 0, NULL, (unsigned)-1, "cannot say hello");
        return ZIS_THR;
    }
    size_t argc;
    if (!zis_read_values(z, 1, "[*]", &argc))
        return ZIS_OK;
    for (size_t i = 1; i < argc; i++) {
        zis_make_int(z, 0, (int64_t)i + 1);
        zis_load_element(z, 1, 0, 3);
        if (zis_invoke(z, (unsigned[]){0, 2, 3}, 1) == ZIS_THR)
            return ZIS_THR;
    }
    return ZIS_OK;
}

// <module_init>()
// # Prints "Hello, World!\n".
static int F_init(zis_t z) {
    if (zis_load_global(z, 0, "hello", (size_t)-1) != ZIS_OK) {
        zis_make_exception(z, 0, NULL, (unsigned)-1, "cannot say hello");
        return ZIS_THR;
    }
    zis_make_string(z, 1, "World", (size_t)-1);
    return zis_invoke(z, (unsigned[]){0, 0, 1}, 1);
}

static const struct zis_native_func_def funcs[] = {
    { NULL   , {0, 0, 1}, F_init  },
    { "main" , {1, 0, 3}, F_main  },
    { "hello", {1, 0, 1}, F_hello },
    { NULL   , {0, 0, 0}, NULL    },
};

ZIS_NATIVE_MODULE(hello) = {
    .name = "hello",
    .functions = funcs,
    .types = NULL,
};
