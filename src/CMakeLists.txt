################################
##### Collect source files #####
################################

set(zis_exe_src "start.c")

aux_source_directory(. zis_lib_src)
list(REMOVE_ITEM zis_lib_src ${zis_exe_src})

################################
##### Define build targets #####
################################

### Library. Target: `${zis_lib_tgt}`.
if(ZIS_BUILD_SHARED)
    add_library(zis_lib_shared SHARED ${zis_lib_src})
    target_compile_definitions(
        zis_lib_shared
        PRIVATE "ZIS_EXPORT_API=1"
        INTERFACE "ZIS_IMPORT_API=1"
    )
    set(zis_lib_tgt zis_lib_shared)
else()
    add_library(zis_lib_static STATIC ${zis_lib_src})
    set(zis_lib_tgt zis_lib_static)
endif()
set_target_properties(${zis_lib_tgt} PROPERTIES OUTPUT_NAME "${ZIS_NAME}")
get_target_property(prefix ${zis_lib_tgt} PREFIX)
if(NOT prefix)
    set_target_properties(${zis_lib_tgt} PROPERTIES PREFIX "lib")
endif()

### Executable. Target: `${zis_exe_tgt}`.
if(ZIS_BUILD_SHARED)
    add_executable(zis_exe_shared ${zis_exe_src})
    target_link_libraries(zis_exe_shared PRIVATE zis_lib_shared)
    set(zis_exe_tgt zis_exe_shared)
else()
    add_executable(zis_exe_static ${zis_exe_src})
    set_target_properties(zis_exe_static PROPERTIES ENABLE_EXPORTS TRUE)
    target_link_libraries(zis_exe_static PRIVATE zis_lib_static)
    set(zis_exe_tgt zis_exe_static)
endif()
set_target_properties(${zis_exe_tgt} PROPERTIES OUTPUT_NAME "${ZIS_NAME}")

### Common.
foreach(tgt IN LISTS zis_lib_tgt zis_exe_tgt)
    target_include_directories(${tgt} PRIVATE "${CMAKE_SOURCE_DIR}/include")
endforeach()

#######################################
##### Installation configurations #####
#######################################

### Install directories.
if(WIN32)
    set(zis_install_exe_dir ".")
    set(zis_install_lib_dir ".")
    set(zis_install_inc_dir "include")
    set(zis_install_mod_dir "lib")
else()
    include(GNUInstallDirs)
    set(zis_install_exe_dir ${CMAKE_INSTALL_BINDIR})
    set(zis_install_lib_dir ${CMAKE_INSTALL_LIBDIR})
    set(zis_install_inc_dir ${CMAKE_INSTALL_INCLUDEDIR})
    set(zis_install_mod_dir "${CMAKE_INSTALL_LIBDIR}/${ZIS_NAME}")
endif()

### RPATH.
if(DEFINED zis_exe_shared AND ZIS_PACK_RELA_RPATH AND UNIX)
    set_target_properties(
        zis_exe_shared PROPERTIES INSTALL_RPATH
        "$<IF:$<PLATFORM_ID:Darwin>,@executable_path,$ORIGIN>/../${zis_install_lib_dir}"
    )
endif()

### Install rules.
install(TARGETS ${zis_exe_targets} DESTINATION ${zis_install_exe_dir})
install(TARGETS ${zis_lib_targets} DESTINATION ${zis_install_lib_dir})
install(TARGETS ${zis_mod_targets} DESTINATION ${zis_install_mod_dir})
if(ZIS_PACK_HEADER)
    file(GLOB zis_install_headers "${CMAKE_SOURCE_DIR}/include/*.h")
    install(FILES ${zis_install_headers} DESTINATION ${zis_install_inc_dir})
endif()
