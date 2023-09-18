################################
##### Collect source files #####
################################

aux_source_directory(. zis_exe_src)

################################
##### Define build targets #####
################################

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

#######################################
##### Installation configurations #####
#######################################

include(installdir)

### RPATH.
if(DEFINED zis_exe_shared AND ZIS_PACK_RELA_RPATH AND UNIX)
    set_target_properties(
        zis_exe_shared PROPERTIES INSTALL_RPATH
        "$<IF:$<PLATFORM_ID:Darwin>,@executable_path,$ORIGIN>/../${zis_install_lib_dir}"
    )
endif()

### Install rules.
install(TARGETS ${zis_exe_targets} DESTINATION ${zis_install_exe_dir})
