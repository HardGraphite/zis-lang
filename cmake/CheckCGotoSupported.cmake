###
### Checks whether the compiler supports the computed goto statements.
###

## check_computed_goto_supported( [RESULT <result_var>] [OUTPUT <output_var>] )
function(check_computed_goto_supported)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "RESULT;OUTPUT" "")

    set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/_CGotoTest-C")
    set(test_src "${test_dir}/test.c")

    if(NOT EXISTS ${test_dir})
        file(MAKE_DIRECTORY ${test_dir})
    endif()

    file(WRITE ${test_src} [[
#include <stdio.h>

static void test(int x) {
    // https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html
    static const int table[] = { &&L0 - &&L0, &&L1 - &&L0, &&Lx - &&L0 };
    goto *(&&L0 + table[(x >= 0 && x <= 2) ? x : 2]);
    L0: puts("0"); return;
    L1: puts("1"); return;
    Lx: puts("+"); return;
}

int main(int argc, char *argv[]) {
    test(argc);
}
]])

    try_compile(result "${test_dir}" "${test_src}" OUTPUT_VARIABLE outputs C_EXTENSIONS TRUE)
    if(DEFINED arg_RESULT)
        set(${arg_RESULT} ${result} PARENT_SCOPE)
    elseif(NOT result)
        message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} does not support the computed goto statements:\n${outputs}")
    endif()
    if(DEFINED arg_OUTPUT)
        set(${arg_OUTPUT} ${outputs} PARENT_SCOPE)
    endif()
endfunction()
