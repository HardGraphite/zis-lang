//%% [module]
//%% name = hello

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
    { "hello", {1, 0, 0}, F_hello },
    { NULL   , {0, 0, 0}, NULL    },
};

ZIS_NATIVE_MODULE(hello) = {
    .name = "hello",
    .functions = funcs,
    .types = NULL,
};
