#include "test.h"

#include <assert.h>
#include <string.h>

#include "core/attributes.h" // zis_nodiscard

#define REG_MAX 10

// REG0 <- REG1 ( 1, 2, 3, ... )
zis_nodiscard static int call_func_with_int_seq_1(zis_t z, size_t argc) {
    assert(argc <= REG_MAX - 2);
    unsigned int regs[REG_MAX] = { 0, 1 };
    for (size_t i = 0; i < argc; i++) {
        regs[i + 2] = (unsigned int)i + 2;
        zis_make_int(z, (unsigned int)i + 2, (int64_t)i + 1);
    }
    return zis_invoke(z, regs, argc);
}

zis_nodiscard static int call_func_with_int_seq_2(zis_t z, size_t argc) {
    assert(argc <= REG_MAX - 2);
    const unsigned int regs[REG_MAX] = { 0, 1, 2, (unsigned int)-1 };
    for (size_t i = 0; i < argc; i++)
        zis_make_int(z, (unsigned int)i + 2, (int64_t)i + 1);
    return zis_invoke(z, regs, argc);
}

zis_nodiscard static int call_func_with_int_seq_3(zis_t z, size_t argc) {
    assert(argc <= REG_MAX - 2);
    const unsigned int regs[REG_MAX] = { 0, 1, 2 };
    zis_make_values(z, 2, "[*]", argc);
    for (size_t i = 0; i < argc; i++) {
        zis_make_int(z, 0, (int64_t)i + 1);
        zis_insert_element(z, 2, 0, 0);
    }
    return zis_invoke(z, regs, (size_t)-1);
}

static void check_tuple_int_seq(
    zis_t z,
    unsigned int reg_tuple, unsigned int reg_tmp,
    int64_t num_begin, size_t num_cnt, size_t trailing_nil_cnt
) {
    int status;
    int64_t v_size;
    status = zis_read_values(z, reg_tuple, "(*)", &v_size);
    zis_test_assert_eq(status, 1);
    zis_test_assert_eq(v_size, (int64_t)(num_cnt + trailing_nil_cnt));
    for (size_t i = 0; i < num_cnt; i++) {
        int64_t v_num;
        zis_make_int(z, reg_tmp, (int64_t)i + 1);
        status = zis_load_element(z, reg_tuple, reg_tmp, reg_tmp);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_int(z, reg_tmp, &v_num);
        zis_test_assert_eq(status, ZIS_OK);
        zis_test_assert_eq(v_num, (int64_t)i + num_begin);
    }
    for (size_t i = 0; i < trailing_nil_cnt; i++) {
        zis_make_int(z, reg_tmp, (int64_t)(i + num_cnt) + 1);
        status = zis_load_element(z, reg_tuple, reg_tmp, reg_tmp);
        zis_test_assert_eq(status, ZIS_OK);
        status = zis_read_nil(z, reg_tmp);
        zis_test_assert_eq(status, ZIS_OK);
    }
}

// REG0 -> (args, nil, nil) | (args, opt_args, nil) | (args, nil, variadic_args)
static void check_ret_val_int_seq(zis_t z, const struct zis_native_func_def *fd, size_t argc) {
    int status;
    const struct zis_native_func_meta fm = fd->meta;

    const unsigned int reg_a = REG_MAX - 3, reg_o = REG_MAX - 2, reg_v = REG_MAX - 1, reg_tmp = REG_MAX;

    int64_t v_size;
    status = zis_read_values(z, 0, "(*)", &v_size);
    zis_test_assert_eq(status, 1);
    zis_test_assert_eq(v_size, 3);

    if (!fm.no) {
        status = zis_read_values(z, 0, "(%nn)", reg_a);
        zis_test_assert_eq(status, 3);
        check_tuple_int_seq(z, reg_a, reg_tmp, 1, fm.na, 0);
    } else if (fm.no == -1) {
        status = zis_read_values(z, 0, "(%n%)", reg_a, reg_o);
        zis_test_assert_eq(status, 3);
        check_tuple_int_seq(z, reg_a, reg_tmp, 1, fm.na, 0);
        assert(argc >= fm.na);
        check_tuple_int_seq(z, reg_o, reg_tmp, 1 + fm.na, argc - fm.na, 0);
    } else if (fm.no > 0) {
        status = zis_read_values(z, 0, "(%%n)", reg_a, reg_o);
        zis_test_assert_eq(status, 3);
        check_tuple_int_seq(z, reg_a, reg_tmp, 1, fm.na, 0);
        assert(argc >= fm.na && argc <= fm.na + (uint8_t)fm.no);
        check_tuple_int_seq(z, reg_o, reg_tmp, 1 + fm.na, argc - fm.na, fm.na + fm.no - argc);
    } else {
        status = zis_read_values(z, 0, "(%%%)", reg_a, reg_o, reg_v);
        zis_test_assert_eq(status, 3);
        check_tuple_int_seq(z, reg_a, reg_tmp, 1, fm.na, 0);
        uint8_t no_abs_m1 = (uint8_t)-1 - (uint8_t)fm.no;
        if (argc <= fm.na + no_abs_m1) {
            check_tuple_int_seq(z, reg_o, reg_tmp, 1 + fm.na, argc - fm.na, fm.na + no_abs_m1 - argc);
            check_tuple_int_seq(z, reg_v, reg_tmp, 1 + fm.na + no_abs_m1, 0, 0);
        } else {
            check_tuple_int_seq(z, reg_o, reg_tmp, 1 + fm.na, no_abs_m1, 0);
            check_tuple_int_seq(z, reg_v, reg_tmp, 1 + fm.na + no_abs_m1, argc - fm.na - no_abs_m1, 0);
        }
    }
    zis_load_nil(z, REG_MAX - 2, 3);
}

// REG0 -> exception
static void check_exception(zis_t z) {
    int status;
    char buffer[128];
    size_t size;

    status = zis_read_exception(z, 0, ZIS_RDE_TYPE, REG_MAX - 3);
    zis_test_assert_eq(status, ZIS_OK);
    size = sizeof buffer;
    status = zis_read_symbol(z, REG_MAX - 3, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_assert_eq(size, 4);
    zis_test_assert_eq(memcmp(buffer, "type", 4), 0);

    status = zis_read_exception(z, 0, ZIS_RDE_WHAT, REG_MAX - 1);
    zis_test_assert_eq(status, ZIS_OK);
    size = sizeof buffer;
    status = zis_read_string(z, REG_MAX - 1, buffer, &size);
    zis_test_assert_eq(status, ZIS_OK);
    zis_test_log(ZIS_TEST_LOG_TRACE, "exception: %.*s", (int)size, buffer);

    zis_load_nil(z, REG_MAX - 3, 3);
}

// call (1/2/3) + check_ret_val
static void call_and_check_int_seq(
    zis_t z,
    const struct zis_native_func_def *fd,
    size_t argc,
    bool ok
) {
    int status;

    status = call_func_with_int_seq_1(z, argc);
    if (ok) {
        zis_test_assert_eq(status, ZIS_OK);
        check_ret_val_int_seq(z, fd, argc);
    } else {
        zis_test_assert_eq(status, ZIS_THR);
        check_exception(z);
    }

    status = call_func_with_int_seq_2(z, argc);
    if (ok) {
        zis_test_assert_eq(status, ZIS_OK);
        check_ret_val_int_seq(z, fd, argc);
    } else {
        zis_test_assert_eq(status, ZIS_THR);
        check_exception(z);
    }

    status = call_func_with_int_seq_3(z, argc);
    if (ok) {
        zis_test_assert_eq(status, ZIS_OK);
        check_ret_val_int_seq(z, fd, argc);
    } else {
        zis_test_assert_eq(status, ZIS_THR);
        check_exception(z);
    }
}

// REG1 <- func
static void make_func(
    zis_t z,
    const struct zis_native_func_def *fd
) {
    int status;
    status = zis_make_function(z, 1, fd, (unsigned int)-1);
    zis_test_assert_eq(status, ZIS_OK);
}

ZIS_NATIVE_FUNC_DEF(F_a3, z, {3, 0, 4}) { // func(a1, a2, a3) -> ((a1, a2, a3), nil, nil)
    zis_make_values(z, 4, "(%%%)", 1, 2, 3);
    zis_make_values(z, 0, "(%nn)", 4);
    return ZIS_OK;
}

zis_test_define(test_F_a3, z) {
    make_func(z, &F_a3);

    call_and_check_int_seq(z, &F_a3, 3, true); // F(1, 2, 3)
    for (size_t i = 0; i <= 2; i++)
        call_and_check_int_seq(z, &F_a3, i, false);
    for (size_t i = 4; i <= 7; i++)
        call_and_check_int_seq(z, &F_a3, i, false);
}

ZIS_NATIVE_FUNC_DEF(F_a2o2, z, {2, 2, 6}) { // func(a1, a2, ?o1, ?o2) -> ((a1, a2), (o1, o2), nil)
    zis_make_values(z, 5, "(%%)", 1, 2);
    zis_make_values(z, 6, "(%%)", 3, 4);
    zis_make_values(z, 0, "(%%n)", 5, 6);
    return ZIS_OK;
}

zis_test_define(test_F_a2o2, z) {
    make_func(z, &F_a2o2);

    for (size_t i = 2; i <= 4; i++)
        call_and_check_int_seq(z, &F_a2o2, i, true); // F(1, 2, ?3, ?4)
    for (size_t i = 0; i <= 1; i++)
        call_and_check_int_seq(z, &F_a2o2, i, false);
    for (size_t i = 5; i <= 7; i++)
        call_and_check_int_seq(z, &F_a2o2, i, false);
}

ZIS_NATIVE_FUNC_DEF(F_a2v, z, {2, -1, 4}) { // func(a1, a2, *v) -> ((a1, a2), nil, v)
    zis_make_values(z, 4, "(%%)", 1, 2);
    zis_make_values(z, 0, "(%n%)", 4, 3);
    return ZIS_OK;
}

zis_test_define(test_F_a2v, z) {
    make_func(z, &F_a2v);

    for (size_t i = 2; i <= 5; i++)
        call_and_check_int_seq(z, &F_a2v, i, true); // F(1, 2, ...)
    for (size_t i = 0; i <= 1; i++)
        call_and_check_int_seq(z, &F_a2v, i, false);
}

ZIS_NATIVE_FUNC_DEF(F_a2o2v, z, {2, -2-1, 7}) { // func(a1, a2, ?o1, ?o2, *v) -> ((a1, a2), (o1, o2), v)
    zis_make_values(z, 6, "(%%)", 1, 2);
    zis_make_values(z, 7, "(%%)", 3, 4);
    zis_make_values(z, 0, "(%%%)", 6, 7, 5);
    return ZIS_OK;
}

zis_test_define(test_F_a2o2v, z) {
    make_func(z, &F_a2o2v);

    for (size_t i = 2; i <= 8; i++)
        call_and_check_int_seq(z, &F_a2o2v, i, true); // F(1, 2, ...)
    for (size_t i = 0; i <= 1; i++)
        call_and_check_int_seq(z, &F_a2o2v, i, false);
}

zis_test_list(
    core_invoke,
    REG_MAX,
    zis_test_case(test_F_a3),
    zis_test_case(test_F_a2o2),
    zis_test_case(test_F_a2v),
    zis_test_case(test_F_a2o2v),
)
