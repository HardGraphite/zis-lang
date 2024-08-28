###
### Checks whether the compiler supports GNUC's built-in functions
### to perform arithmetic with overflow checking.
###

## check_gnuc_overflow_arithmetic_supported( [RESULT <result_var>] [OUTPUT <output_var>] )
function(check_gnuc_overflow_arithmetic_supported)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "RESULT;OUTPUT" "")

    set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/_OvArithTest-C")
    set(test_src "${test_dir}/test.c")

    if(NOT EXISTS ${test_dir})
        file(MAKE_DIRECTORY ${test_dir})
    endif()

    file(WRITE ${test_src} [[
#include <stdio.h>

static void test(int a, int b) {
    // https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html
    int res, ov;
    ov = __builtin_add_overflow(a, b, &res);
    printf("%d, %d\n", res, ov);
    ov = __builtin_sub_overflow(a, b, &res);
    printf("%d, %d\n", res, ov);
    ov = __builtin_mul_overflow(a, b, &res);
    printf("%d, %d\n", res, ov);
}

int main(int argc, char **argv) {
    test(argc, (int)argv);
}
]])

    try_compile(result "${test_dir}" "${test_src}" OUTPUT_VARIABLE outputs C_EXTENSIONS TRUE)
    if(DEFINED arg_RESULT)
        set(${arg_RESULT} ${result} PARENT_SCOPE)
    elseif(NOT result)
        message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} does not support GNUC's built-in functions to perform arithmetic with overflow checking:\n${outputs}")
    endif()
    if(DEFINED arg_OUTPUT)
        set(${arg_OUTPUT} ${outputs} PARENT_SCOPE)
    endif()
endfunction()
