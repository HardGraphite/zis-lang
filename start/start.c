#include <zis.h>

int main(int argc, char *argv[]) {
    struct zis_context *const z = zis_create();
    zis_destroy(z);
}
