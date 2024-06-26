cmake_minimum_required(VERSION 3.18)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(ZisVersion)

project(
    zis-lang
    VERSION ${ZIS_VERSION_STRING}
    DESCRIPTION "The ZiS programming language"
    LANGUAGES C CXX
)

option(ZIS_BUILD_IPO     "Enable inter-procedural optimization."             ON)
option(ZIS_BUILD_CGOTO   "Use the computed goto statements if possible."     ON)
option(ZIS_BUILD_SHARED  "Build and link shared library (static otherwise)" OFF)
option(ZIS_BUILD_CORE    "Build `core', the ZIS library"                     ON)
option(ZIS_BUILD_START   "Build `start', an executable entry"                ON)
option(ZIS_BUILD_MODULES "Build `modules', the modules"                      ON)
option(ZIS_DEBUG         "Enable debugging"                                 OFF)
option(ZIS_DEBUG_LOGGING "Enable debug logging"                             OFF)
option(ZIS_DEBUG_DUMPBT  "Dump stack backtrace when crashing"               OFF)
option(ZIS_DOC           "Generate documentation. See ZIS_DOC_* options."   OFF)
option(ZIS_TEST          "Build test files and prepare for ctest command."  OFF)
option(ZIS_PACK          "Configure for cpack command."                     OFF)
option(ZIS_PACK_HEADER   "Include header files when installing."            OFF)
option(ZIS_PACK_RELA_RPATH "Use relative RPTAH."                             ON)

set(ZIS_DISPLAY_NAME   "ZiS" CACHE STRING "Display name.")
set(ZIS_OUTPUT_NAME    "zis" CACHE STRING "Output name.")

set(CMAKE_C_STANDARD                  11)
set(CMAKE_C_EXTENSIONS                ON)
set(CMAKE_C_STANDARD_REQUIRED         ON)
set(CMAKE_C_VISIBILITY_PRESET     hidden)
set(CMAKE_CXX_STANDARD                17)
set(CMAKE_CXX_EXTENSIONS             OFF)
set(CMAKE_CXX_STANDARD_REQUIRED       ON)
set(CMAKE_CXX_VISIBILITY_PRESET   hidden)

if(NOT CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "Set build type to ${CMAKE_BUILD_TYPE}")
endif()

if(ZIS_BUILD_IPO)
    if(NOT DEFINED CACHE{CMAKE_INTERPROCEDURAL_OPTIMIZATION})
        include(CheckIPOSupported)
        message(STATUS "Testing inter-procedural optimization support")
        check_ipo_supported(RESULT res LANGUAGES C)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${res} CACHE BOOL "")
        mark_as_advanced(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        if(NOT res)
            message(WARNING "IPO is not supported")
        endif()
    endif()
else()
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION CACHE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
endif()

if(ZIS_BUILD_CGOTO)
    if(NOT DEFINED CACHE{ZIS_USE_COMPUTED_GOTO})
        include(CheckCGotoSupported)
        message(STATUS "Testing computed goto statement support")
        check_computed_goto_supported(RESULT res)
        set(ZIS_USE_COMPUTED_GOTO ${res} CACHE BOOL "")
        mark_as_advanced(ZIS_USE_COMPUTED_GOTO)
        if(NOT res)
            message(WARNING "Computed goto is not supported")
        endif()
    endif()
else()
    unset(ZIS_USE_COMPUTED_GOTO CACHE)
    set(ZIS_USE_COMPUTED_GOTO OFF)
endif()

if(MSVC)
    add_compile_options(
        /W4 /utf-8 /Zc:inline,preprocessor
        $<$<COMPILE_LANGUAGE:C>:/Zc:__STDC__>
        $<$<COMPILE_LANGUAGE:C>:/wd4200> # warning C4200: nonstandard extension used : zero-sized array in struct/union
        /wd4706 # warning C4706: assignment within conditional expression
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS) # Disable warnings for "unsafe" functions.
else() # GCC, Clang, ...
    add_compile_options(
        -Wall -Wextra -Wpedantic -pipe
        $<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>
        $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
        $<$<COMPILE_LANGUAGE:C>:-Werror=return-type>
    )
    if(CMAKE_C_COMPILER_ID MATCHES ".*Clang")
        add_compile_options(
            -Wno-string-plus-int
        )
    endif()
endif()

if(ZIS_BUILD_CORE)
    add_subdirectory("core")
endif()

if(ZIS_BUILD_START)
    add_subdirectory("start")
endif()

if(ZIS_BUILD_MODULES)
    add_subdirectory("modules")
endif()

if(ZIS_TEST)
    enable_testing()
    add_subdirectory("test")
    message(STATUS "`ZIS_TEST` is ${ZIS_TEST}. Use `ctest` to run tests.")
endif()

if(ZIS_DOC)
    add_subdirectory("doc")
    message(STATUS "`ZIS_DOC` is ${ZIS_DOC}.")
endif()

if(ZIS_PACK)
    set(CPACK_PACKAGE_CHECKSUM   "MD5")
    set(CPACK_STRIP_FILES         TRUE)
    include(CPack)
    message(STATUS "`ZIS_PACK` is ${ZIS_PACK}. Use `cpack` to generate packages.")
endif()
