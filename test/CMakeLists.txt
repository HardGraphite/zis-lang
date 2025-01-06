#####################
##### Utilities #####
#####################

add_library(zis_test_util STATIC "test.c")

# For platforms where RPATH is not supported, add the directory of the shared
# core library to the test's PATH environment variable.
if(WIN32 AND ZIS_BUILD_CORE AND ZIS_BUILD_SHARED)
    set(_zis_test_path_modification "PATH=path_list_append:$<TARGET_FILE_DIR:zis_core_tgt>")
    function(_zis_test_path_setup test)
        set_tests_properties(
            ${test} PROPERTIES
            ENVIRONMENT_MODIFICATION ${_zis_test_path_modification}
        )
    endfunction()
else()
    function(_zis_test_path_setup test)
    endfunction()
endif()

function(_zis_test_filename_to_test_name filename out_test_name)
    file(REAL_PATH ${filename} filename)
    file(RELATIVE_PATH filename "${CMAKE_SOURCE_DIR}/test" ${filename})
    get_filename_component(base_name ${filename} NAME_WE)
    get_filename_component(dir ${filename} DIRECTORY)
    string(REPLACE "/" "-" dir ${dir})
    set(${out_test_name} "${dir}-${base_name}" PARENT_SCOPE)
endfunction()

## zis_test_add_c("filename" [OBJECT_ONLY] [LINK_CORE] [ENABLE_EXPORTS] [INCLUDE_DIR ...] [LINK ...] [PROPERTIES ...])
## Add test from a C source file.
function(zis_test_add_c filename)
    _zis_test_filename_to_test_name("${filename}" test_name)
    set(target_name "zis_test_${test_name}")
    cmake_parse_arguments(PARSE_ARGV 1 arg "OBJECT_ONLY;LINK_CORE;ENABLE_EXPORTS" "" "INCLUDE_DIR;LINK;PROPERTIES")
    if(${arg_OBJECT_ONLY})
        add_library(${target_name} OBJECT "${filename}")
        target_include_directories(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    else()
        add_executable(${target_name} "${filename}")
        target_link_libraries(${target_name} PRIVATE zis_test_util)
        add_test(NAME ${test_name} COMMAND "$<TARGET_FILE:${target_name}>")
        _zis_test_path_setup(${test_name})
    endif()
    if(${arg_LINK_CORE})
        target_link_libraries(${target_name} PRIVATE zis_core_tgt)
    endif()
    if(${arg_ENABLE_EXPORTS})
        set_target_properties(${target_name} PROPERTIES ENABLE_EXPORTS TRUE)
    endif()
    if(DEFINED arg_INCLUDE_DIR)
        target_include_directories(${target_name} PRIVATE ${arg_INCLUDE_DIR})
    endif()
    if(DEFINED arg_LINK)
        target_link_libraries(${target_name} PRIVATE ${arg_LINK})
    endif()
    if(DEFINED arg_PROPERTIES)
        set_target_properties(${target_name} PROPERTIES ${arg_PROPERTIES})
    endif()
endfunction()

## zis_test_add_c_bundle(<name> FILES ... [LINK_CORE] [INCLUDE_DIR ...] [LINK ...])
## Add tests from C source files as a bundle.
function(zis_test_add_c_bundle bundle_name)
    set(bundle_tgt_name "zis_test_${bundle_name}")
    cmake_parse_arguments(PARSE_ARGV 1 arg "LINK_CORE" "" "FILES;INCLUDE_DIR;LINK")
    create_test_sourcelist(src_list "${bundle_name}_main.c" ${arg_FILES})
    add_executable(${bundle_tgt_name} ${src_list})
    target_link_libraries(${bundle_tgt_name} PRIVATE zis_test_util "$<$<BOOL:${UNIX}>:m>")
    if(DEFINED arg_INCLUDE_DIR)
        target_include_directories(${bundle_tgt_name} PRIVATE ${arg_INCLUDE_DIR})
    endif()
    if(DEFINED arg_LINK)
        target_link_libraries(${bundle_tgt_name} PRIVATE ${arg_LINK})
    endif()
    if(${arg_LINK_CORE})
        target_link_libraries(${bundle_tgt_name} PRIVATE zis_core_tgt)
    endif()
    foreach(filename IN LISTS arg_FILES)
        get_filename_component(filename_stem ${filename} NAME_WE)
        _zis_test_filename_to_test_name("${filename}" test_name)
        add_test(NAME ${test_name} COMMAND "$<TARGET_FILE:${bundle_tgt_name}>" ${filename_stem})
        _zis_test_path_setup(${test_name})
    endforeach()
endfunction()

## Add test from a ZiS script.
function(zis_test_add_script filename)
    _zis_test_filename_to_test_name("${filename}" test_name)
    set(filename "${CMAKE_CURRENT_SOURCE_DIR}/${filename}")
    add_test(
        NAME ${test_name}
        COMMAND "$<TARGET_FILE:zis_start_tgt>" "@testing" "${filename}"
    )
    _zis_test_path_setup(${test_name})
    set_tests_properties(
        ${test_name} PROPERTIES
        ENVIRONMENT "ZIS_PATH=$<TARGET_FILE_DIR:zis_mod_testing>"
        DEPENDS zis_start_tgt
    )
endfunction()


#################
##### Tests #####
#################

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB sub_dirs "*")
foreach(dir IN LISTS sub_dirs)
    if(IS_DIRECTORY ${dir})
        if(EXISTS "${dir}/CMakeLists.txt")
            add_subdirectory(${dir})
        else()
            message(WARNING "${dir} does not contain a CMakeLists.txt")
        endif()
    endif()
endforeach()

