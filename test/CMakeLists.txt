function(zis_test_available file result)
    if(${file} MATCHES "core_.*")
        set(${result} ${ZIS_BUILD_CORE} PARENT_SCOPE)
    elseif(${file} MATCHES "start_.*")
        set(${result} ${ZIS_BUILD_START} PARENT_SCOPE)
    else()
        set(${result} TRUE PARENT_SCOPE)
        message(WARNING "test file `${file}': unknown group")
    endif()
endfunction()

set(
    zis_test_src_no_core_lib_list
    "core_bits"
    "core_fsutil"
    "start_clopts"
)

aux_source_directory(. zis_test_src_list)
list(REMOVE_ITEM zis_test_src_list "test.c" "./test.c" ".\\test.c")
add_library(zis_test_util STATIC "test.c")

foreach(zis_test_src IN LISTS zis_test_src_list)
    zis_test_available("${zis_test_src}" ok)
    if(NOT ok)
        continue()
    endif()
    get_filename_component(zis_test_src_stem "${zis_test_src}" NAME_WLE)
    set(zis_test_name "zis_test_${zis_test_src_stem}")
    add_executable(${zis_test_name} ${zis_test_src})
    target_link_libraries(${zis_test_name} PRIVATE zis_test_util)
    list(FIND zis_test_src_no_core_lib_list ${zis_test_src_stem} i)
    if(${i} EQUAL -1)
        target_link_libraries(${zis_test_name} PRIVATE ${zis_core_tgt})
    endif()
    add_test(
        NAME ${zis_test_name}
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/${zis_test_name}"
    )
endforeach()
