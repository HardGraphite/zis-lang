# The test utility lib.
add_library(zis_test_util STATIC "test.c")

## zis_add_test("*.{c,cc}" [LINK_CORE] [ENABLE_EXPORTS] [INCLUDE ...] [LINK ...])
## zis_add_test("*.cmake")
## zis_add_test("*.zis")
function(zis_add_test filename)
    get_filename_component(filename_stem "${filename}" NAME_WLE)
    set(test_name "zis_test_${filename_stem}")
    if(filename MATCHES ".+\\.cc?$")
        cmake_parse_arguments(PARSE_ARGV 1 arg "LINK_CORE;ENABLE_EXPORTS" "" "INCLUDE;LINK")
        add_executable(${test_name} ${filename})
        target_link_libraries(${test_name} PRIVATE zis_test_util)
        if(${arg_LINK_CORE})
            target_link_libraries(${test_name} PRIVATE ${zis_core_tgt})
        endif()
        if(${arg_ENABLE_EXPORTS})
            set_target_properties(${test_name} PROPERTIES ENABLE_EXPORTS TRUE)
        endif()
        if(DEFINED arg_INCLUDE)
            target_include_directories(${test_name} PRIVATE ${arg_INCLUDE})
        endif()
        if(DEFINED arg_LINK)
            target_link_libraries(${test_name} PRIVATE ${arg_LINK})
        endif()
        add_test(
            NAME ${test_name}
            COMMAND "$<TARGET_FILE:${test_name}>"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )
    elseif(filename MATCHES ".+\\.cmake$")
        include("${filename}")
    elseif(filename MATCHES ".+\\.zis$")
        add_test(
            NAME ${test_name}
            COMMAND "$<TARGET_FILE:${zis_start_tgt}>" "@testing" "${CMAKE_CURRENT_SOURCE_DIR}/${filename}"
        )
        if(TARGET zis_mod_testing)
            set_tests_properties(
                ${test_name} PROPERTIES
                ENVIRONMENT "ZIS_PATH=$<TARGET_FILE_DIR:zis_mod_testing>"
            )
        endif()
    else()
        message(SEND_ERROR "Unexpected file for tests: ${filename}")
        return()
    endif()
    message(VERBOSE "Add test: ${test_name}")
endfunction()


if(ZIS_BUILD_CORE)
    zis_add_test(core_bits.c       )
    zis_add_test(core_fsutil.c     )
    zis_add_test(core_instr.c      )
    zis_add_test(core_api.c        LINK_CORE)
    zis_add_test(core_api_cxx.cc   LINK_CORE ENABLE_EXPORTS LINK "$<$<BOOL:${UNIX}>:dl>")
    zis_add_test(core_invoke.c     LINK_CORE)
    zis_add_test(core_gc.c         LINK_CORE)
    zis_add_test(core_compile.c    LINK_CORE)
endif()
if(ZIS_BUILD_MODULES)
    zis_add_test(core_modlist.c    INCLUDE ${zis_modules_header_dir})
endif()
if(ZIS_BUILD_START)
    zis_add_test(start_cliutil.c   )
    zis_add_test(start_run.cmake   )
endif()
if(ZIS_MOD_TESTING)
    zis_add_test(core_builtins.zis )
endif()
