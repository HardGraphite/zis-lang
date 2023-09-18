################################
##### Collect source files #####
################################

aux_source_directory(. zis_start_src)

################################
##### Define build targets #####
################################

if(ZIS_BUILD_SHARED)
    add_executable(zis_start_shared ${zis_start_src})
    target_link_libraries(zis_start_shared PRIVATE zis_core_shared)
    set(zis_start_tgt zis_start_shared)
else()
    add_executable(zis_start_static ${zis_start_src})
    set_target_properties(zis_start_static PROPERTIES ENABLE_EXPORTS TRUE)
    target_link_libraries(zis_start_static PRIVATE zis_core_static)
    set(zis_start_tgt zis_start_static)
endif()
set_target_properties(${zis_start_tgt} PROPERTIES OUTPUT_NAME "${ZIS_NAME}")

#######################################
##### Installation configurations #####
#######################################

include(installdir)

### RPATH.
if(DEFINED zis_start_shared AND ZIS_PACK_RELA_RPATH AND UNIX)
    set_target_properties(
        zis_start_shared PROPERTIES INSTALL_RPATH
        "$<IF:$<PLATFORM_ID:Darwin>,@executable_path,$ORIGIN>/../${zis_install_lib_dir}"
    )
endif()

### Install rules.
install(TARGETS ${zis_start_tgt} DESTINATION ${zis_install_exe_dir})
