#include "test.h"

#include <string.h>

#include "zis_modules.h"

#if !ZIS_EMBEDDED_MODULE_LIST_EMPTY

static const char *const mod_name_list[] = {
#define E(NAME)  #NAME ,
    ZIS_EMBEDDED_MODULE_LIST
#undef E
};

static const size_t mod_name_count = sizeof mod_name_list / sizeof mod_name_list[0];

zis_test0_define(mod_list_order) {
#if ZIS_EMBEDDED_MODULE_LIST_SORTED

    for (size_t i = 1; i < mod_name_count; i++) {
        const int x = strcmp(mod_name_list[i - 1], mod_name_list[i]);
        zis_test_assert(x < 0);
    }

#endif
}

static size_t strings_bin_search(
    const char *const strings[], size_t string_count,
    const char *target
) {
    if (!string_count)
        return SIZE_MAX;

    for (size_t index_l = 0, index_r = string_count - 1; (int)index_l <= (int)index_r; ) {
        const size_t index_m = index_l + (index_r - index_l) / 2;
        int diff = strcmp(strings[index_m], target);
        if (diff == 0)
            return index_m;
        if (diff < 0)
            index_l = index_m + 1;
        else
            index_r = index_m - 1;
    }

    return SIZE_MAX;
}

static size_t strings_seq_search(
    const char *const strings[], size_t string_count,
    const char *target
) {
    for (size_t i = 0; i < string_count; i++) {
        if (strcmp(strings[i], target) == 0)
            return i;
    }
    return SIZE_MAX;
}

zis_test0_define(mod_list_search) {
    for (size_t i = 1; i < mod_name_count; i++) {
        const char *name = mod_name_list[i];
        zis_test_assert_eq(strings_seq_search(mod_name_list, mod_name_count, name), i);
#if ZIS_EMBEDDED_MODULE_LIST_SORTED
        zis_test_assert_eq(strings_bin_search(mod_name_list, mod_name_count, name), i);
#endif
    }

    zis_test_assert_eq(strings_seq_search(mod_name_list, mod_name_count, "??"), SIZE_MAX);
#if ZIS_EMBEDDED_MODULE_LIST_SORTED
    zis_test_assert_eq(strings_bin_search(mod_name_list, mod_name_count, "??"), SIZE_MAX);
#endif
}

zis_test0_list(
    core_modlist,
    mod_list_order,
    mod_list_search,
)

#else // ZIS_EMBEDDED_MODULE_LIST_EMPTY

zis_test_list(
    core_modlist,
    0,
)

#endif // !ZIS_EMBEDDED_MODULE_LIST_EMPTY
