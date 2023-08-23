aux_source_directory(. zis_test_src_list)
set(zis_test_link_lib ${zis_lib_tgt})

foreach(zis_test_src IN LISTS zis_test_src_list)
    get_filename_component(zis_test_name "${zis_test_src}" NAME_WLE)
    set(zis_test_name "test-${zis_test_name}")
    add_executable(${zis_test_name} ${zis_test_src})
    target_link_libraries(${zis_test_name} PRIVATE ${zis_test_link_lib})
    add_test(
        NAME ${zis_test_name}
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/${zis_test_name}"
    )
endforeach()
