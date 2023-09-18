#include "test.h"

zis_test_define(test_noop, z) {
    zis_test_log(0, "ZIS context created at %p", (void *)z);
}

zis_test_list(
    test_noop,
)
