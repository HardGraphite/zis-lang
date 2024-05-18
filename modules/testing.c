//%% [module]
//%% name = testing
//%% description = Provides basic testing support.

#include <stdio.h>
#include <string.h>

#include <zis.h>

#define FAIL_EXC_TYPE "testing_failure"

static int F_check_equal(zis_t z) {
#define Fd_check_equal { "check_equal", {2, 1, 5}, F_check_equal }
    /*#DOCSTR# func check_equal(actual, expected, ?message) */
    zis_make_symbol(z, 0, "==", 2);
    zis_if_thr (zis_invoke(z, (unsigned int []){0, (unsigned)-1, 1, 2}, 2))
        return ZIS_THR;
    bool equals;
    zis_if_thr (zis_read_bool(z, 0, &equals)) {
        return ZIS_THR;
    } else if (!equals) {
        zis_load_global(z, 4, "print", (size_t)-1);
        zis_make_string(z, 5, "!! FAIL", (size_t)-1);
        zis_invoke(z, (unsigned int []){0, 4, 5, 3}, 2);
        zis_make_string(z, 5, "!=", (size_t)-1);
        zis_invoke(z, (unsigned int []){0, 4, 1, 5, 2}, 3);
        zis_make_exception(z, 0, FAIL_EXC_TYPE, (unsigned int)-1, "test failed");
        return ZIS_THR;
    }
    zis_load_nil(z, 0, 1);
    return ZIS_OK;
}

static int F_call_test_func(zis_t z) {
#define Fd_call_test_func { "call_test_func", {2, 0, 4}, F_call_test_func }
    /*#DOCSTR# func call_test_func(f, name) :: Bool */
    zis_load_global(z, 0, "print", (size_t)-1);
    zis_make_string(z, 4, "-- TEST", (size_t)-1);
    zis_invoke(z, (unsigned int[]){0, 0, 4, 2}, 2);
    const int status = zis_invoke(z, (unsigned int[]){0, 1}, 0);
    if (status == ZIS_OK) {
        zis_load_bool(z, 0, true);
        return ZIS_OK;
    }
    zis_move_local(z, 3, 0);
    if (zis_read_exception(z, 3, ZIS_RDE_TEST, 4) == ZIS_OK) {
        zis_make_stream(z, 0, ZIS_IOS_STDX, 1);
        zis_read_exception(z, 3, ZIS_RDE_DUMP, 0);
    } else {
        zis_load_global(z, 0, "print", (size_t)-1);
        zis_make_string(z, 4, "test failed", (size_t)-1);
        zis_invoke(z, (unsigned int []){0, 0, 4}, 1);
    }
    zis_load_bool(z, 0, false);
    return ZIS_OK;
}

static int F_main(zis_t z) {
#define Fd_main { "main", {1, 0, 4}, F_main }
    /*#DOCSTR# func main(args :: Array[String])
    The main function. The arguments should be paths to test script files, where
    the global functions named `test_*` will be called in order. */
    unsigned int passed_count = 0, failed_count = 0;
    char text_buffer[256];
    size_t text_size;
    for (int i = 2; ; i++) {
        zis_make_int(z, 0, i);
        zis_if_err (zis_load_element(z, 1, 0, 2))
            break;
        text_size = sizeof text_buffer - 1;
        zis_if_thr (zis_read_string(z, 2, text_buffer, &text_size))
            continue;
        text_buffer[text_size] = 0;
        zis_if_thr (zis_import(z, 3, text_buffer, ZIS_IMP_PATH))
            return ZIS_THR;
        zis_load_global(z, 0, "print", (size_t)-1);
        zis_make_string(z, 4, "++ FILE ", (size_t)-1);
        zis_invoke(z, (unsigned int []){0, 0, 4, 2}, 2);
        zis_make_symbol(z, 0, "list_vars", (size_t)-1);
        zis_if_thr (zis_invoke(z, (unsigned int []){4, (unsigned)-1, 3}, 1))
            return ZIS_THR;
        for (int j = i; ; j++) {
            zis_make_int(z, 0, j);
            zis_if_err (zis_load_element(z, 4, 0, 0))
                break;
            if (zis_read_values(z, 0, "(%%)", 1, 2) != 2)
                break;
            text_size = sizeof text_buffer;
            zis_if_err (zis_read_symbol(z, 1, text_buffer, &text_size))
                continue;
            if (!(text_size > 5 && memcmp(text_buffer, "test_", 5) == 0))
                continue;
            zis_load_global(z, 0, "call_test_func", (size_t)-1);
            zis_if_thr (zis_invoke(z, (unsigned int []){0, 0, 2, 1}, 2))
                return ZIS_THR;
            bool passed = true;
            zis_read_bool(z, 0, &passed);
            if (passed)
                passed_count++;
            else
                failed_count++;
        }
    }
    {
        char msg_buffer[80];
        snprintf(
            msg_buffer, sizeof msg_buffer,
            "++ DONE  %u passed, %u failed", passed_count, failed_count
        );
        zis_load_global(z, 0, "print", (size_t)-1);
        zis_make_string(z, 4, msg_buffer, (size_t)-1);
        zis_invoke(z, (unsigned int []){0, 0, 4}, 1);
    }
    zis_make_int(z, 0, failed_count ? 1 : 0);
    return ZIS_OK;
}

static const struct zis_native_func_def funcs[] = {
    Fd_check_equal,
    Fd_call_test_func,
    Fd_main,
    { NULL, {0, 0, 0}, NULL },
};

ZIS_NATIVE_MODULE(testing) = {
    .name = "testing",
    .functions = funcs,
    .types = NULL,
};
