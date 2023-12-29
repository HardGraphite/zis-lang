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
option(ZIS_BUILD_SHARED  "Build and link shared library (static otherwise)" OFF)
option(ZIS_BUILD_CORE    "Build `core', the ZIS library"                     ON)
option(ZIS_BUILD_START   "Build `start', an executable entry"                ON)
option(ZIS_DEBUG         "Enable debugging"                                 OFF)
option(ZIS_DEBUG_LOGGING "Enable debug logging"                             OFF)
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
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_res OUTPUT ipo_out LANGUAGES C)
    if(ipo_res)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO not supported: ${ipo_out}.")
    endif()
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC") # MSVC
    add_compile_options(
        /W4 /utf-8 /Zc:inline,preprocessor
        $<$<COMPILE_LANGUAGE:C>:/wd4200> # warning C4200: nonstandard extension used : zero-sized array in struct/union
        /wd4706 # warning C4706: assignment within conditional expression
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS) # Disable warnings for "unsafe" functions.
else() # GCC, Clang, ...
    add_compile_options(
        -Wall -Wextra -Wpedantic -pipe
        $<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>
        $<$<COMPILE_LANGUAGE:C>:-Werror=return-type>
    )
endif()

if(ZIS_BUILD_CORE)
    add_subdirectory("core")
endif()

if(ZIS_BUILD_START)
    add_subdirectory("start")
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
