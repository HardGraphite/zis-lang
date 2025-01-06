#include <zis.h>

#include <stdio.h>
#include <stdlib.h>

static int hello(zis_t z, void *_arg) {
    char *msg = "Hello, World!";
    char buffer[32];
    size_t size;
    (void)_arg;
    zis_make_string(z, 1, msg, (size_t)-1);
    size = sizeof buffer;
    zis_read_string(z, 1, buffer, &size);
    buffer[size] = 0;
    puts(buffer);
    return 0;
}

int main(void) {
    zis_t z = zis_create();
    zis_native_block(z, 10, hello, NULL);
    zis_destroy(z);
    return EXIT_SUCCESS;
}
