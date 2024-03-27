###
### Checks whether the compiler supports the computed goto statements.
###


## check_computed_goto_supported( [RESULT <var>] )
function(check_computed_goto_supported)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "RESULT" "")
    set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/_CGotoTest")
    set(test_src "${test_dir}/main.c")
    set(test_proj "cgoto-test")
    if(EXISTS ${test_dir})
        file(REMOVE_RECURSE ${test_dir})
    endif()
    file(MAKE_DIRECTORY ${test_dir})
    file(WRITE ${test_src} [[
#include <stdio.h>

void foo(int x) {
    // https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html
    static const int table[] = { &&L0 - &&L0, &&L1 - &&L0, &&Lx - &&L0 };
    goto *(&&L0 + table[(x >= 0 && x <= 2) ? x : 2]);
    L0: puts("0");
    L1: puts("1");
    Lx: puts("+");
}

int main(int argc, char **) {
    foo(argc);
}
    ]])
    file(WRITE "${test_dir}/CMakeLists.txt"
        "cmake_minimum_required(VERSION \"${CMAKE_VERSION}\")\n"
        "project(${test_proj} LANGUAGES C)\n"
        "add_executable(main \"${test_src}\")\n"
    )
    try_compile(result PROJECT ${test_proj} SOURCE_DIR ${test_dir} BINARY_DIR ${test_dir})
    if(DEFINED arg_RESULT)
        set(${arg_RESULT} ${result} PARENT_SCOPE)
    else()
        message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} does not support the computed goto statements.")
    endif()
endfunction()
