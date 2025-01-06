#include "test.h"

#include "core/strutil.c"

zis_test0_define(str_icmp) {
    zis_test_assert_eq(zis_str_icmp("abc", "abc"), 0);
    zis_test_assert_eq(zis_str_icmp("Abc", "abc"), 0);
    zis_test_assert(zis_str_icmp("abc", "ab") != 0);
}

static void do_u8char_from_code_test(zis_wchar_t codepoint, const char *expected) {
    zis_char8_t buf[8];
    size_t len;
    len = zis_u8char_from_code(codepoint, buf);
    zis_test_assert_eq(len, strlen(expected));
    zis_test_assert(memcmp(buf, expected, len) == 0);
}

zis_test0_define(u8char_from_code) {
    do_u8char_from_code_test(0x10, "\x10");
    do_u8char_from_code_test(0x100, "\xc4\x80");
    do_u8char_from_code_test(0x1000, "\xe1\x80\x80");
    do_u8char_from_code_test(0x10000, "\xf0\x90\x80\x80");
}

static void do_u8char_to_code_test(const char *input, zis_wchar_t expected) {
    const size_t input_n = strlen(input);
    zis_wchar_t codepoint;
    const size_t n =
        zis_u8char_to_code(&codepoint, (const zis_char8_t *)input, (const zis_char8_t *)input + input_n + 1);
    zis_test_assert_eq(n, input_n);
    zis_test_assert_eq(codepoint, expected);
}

zis_test0_define(u8char_to_code) {
    do_u8char_to_code_test("\x10", 0x10);
    do_u8char_to_code_test("\xc4\x80", 0x100);
    do_u8char_to_code_test("\xe1\x80\x80", 0x1000);
    do_u8char_to_code_test("\xf0\x90\x80\x80", 0x10000);
}

zis_test0_define(u8char_len_from_code) {
    zis_test_assert_eq(zis_u8char_len_from_code(0x10), 1);
    zis_test_assert_eq(zis_u8char_len_from_code(0x100), 2);
    zis_test_assert_eq(zis_u8char_len_from_code(0x1000), 3);
    zis_test_assert_eq(zis_u8char_len_from_code(0x10000), 4);
}

zis_test0_define(u8char_len_1) {
    zis_test_assert_eq(zis_u8char_len_1(0x10), 1);
    zis_test_assert_eq(zis_u8char_len_1(0xc4), 2);
    zis_test_assert_eq(zis_u8char_len_1(0xe1), 3);
    zis_test_assert_eq(zis_u8char_len_1(0xf0), 4);
}

zis_test0_define(u8str_len) {
    zis_test_assert_eq(zis_u8str_len((const zis_char8_t *)"\x10", (size_t)-1), 1);
    zis_test_assert_eq(zis_u8str_len((const zis_char8_t *)"\xc4\x80", (size_t)-1), 1);
    zis_test_assert_eq(zis_u8str_len((const zis_char8_t *)"\xe1\x80\x80", (size_t)-1), 1);
    zis_test_assert_eq(zis_u8str_len((const zis_char8_t *)"\xf0\x90\x80\x80", (size_t)-1), 1);
}

static void do_u8str_find_pos_test(const char *s, size_t n, size_t off) {
    const zis_char8_t *s1 = (const zis_char8_t *)s;
    const zis_char8_t *s2 = zis_u8str_find_pos(s1, n);
    zis_test_assert_eq(s1 + off, s2);
}

zis_test0_define(u8str_find_pos) {
    do_u8str_find_pos_test("abcd", 2, 2);
    do_u8str_find_pos_test(u8"你好", 1, 3);
}

static void do_u8str_find_test(const char *s, const char *sub_str, size_t index) {
    const zis_char8_t *s1 = (const zis_char8_t *)s;
    const zis_char8_t *ss1 = (const zis_char8_t *)sub_str;
    const zis_char8_t *s2 = zis_u8str_find(s1, strlen(s), ss1, strlen(sub_str));
    if (index == (size_t)-1)
        zis_test_assert_eq(s2, NULL);
    else
        zis_test_assert_eq(s2, zis_u8str_find_pos(s1, index));
}

zis_test0_define(u8str_find) {
    do_u8str_find_test("", "", 0);
    do_u8str_find_test("", "1", (size_t)-1);
    do_u8str_find_test("123", "2", 1);
    do_u8str_find_test("123", "12", 0);
    do_u8str_find_test("123", "23", 1);
    do_u8str_find_test("123", "123", 0);
    do_u8str_find_test("123", "1234", (size_t)-1);
    do_u8str_find_test("123", "0", (size_t)-1);
    do_u8str_find_test(u8"你好", u8"你", 0);
    do_u8str_find_test(u8"你好", u8"好", 1);
}

zis_test0_list(
    core_strutil,
    zis_test0_case(str_icmp),
    zis_test0_case(u8char_from_code),
    zis_test0_case(u8char_to_code),
    zis_test0_case(u8char_len_from_code),
    zis_test0_case(u8char_len_1),
    zis_test0_case(u8str_len),
    zis_test0_case(u8str_find_pos),
    zis_test0_case(u8str_find),
)
