################################
##### Collect source files #####
################################

set(zis_start_src "start.c" "cliutil.c")

if(WIN32)
    list(APPEND zis_start_src "winutil.c")
endif()

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
set_target_properties(${zis_start_tgt} PROPERTIES OUTPUT_NAME "${ZIS_OUTPUT_NAME}")
target_include_directories(${zis_start_tgt} PRIVATE ${zis_core_conf_header_dir})
set(zis_start_tgt ${zis_start_tgt} PARENT_SCOPE)

if(WIN32)
    include(ZisWinUtil)
    if(ZIS_BUILD_SHARED)
        set(desc "${ZIS_DISPLAY_NAME} programming language executable")
        set(prod "${ZIS_DISPLAY_NAME}LangStart")
    else()
        set(desc "${ZIS_DISPLAY_NAME} programming language executable with runtime core")
        set(prod "${ZIS_DISPLAY_NAME}Lang")
    endif()
    zis_win_target_add_rc(
        ${zis_start_tgt} "${desc}" "${prod}"
        "${CMAKE_SOURCE_DIR}/misc/icon_win.ico"
    )
    unset(desc)
    unset(prod)
endif()

#######################################
##### Installation configurations #####
#######################################

include(ZisInstallDirs)

### RPATH.
if(DEFINED zis_start_shared AND ZIS_PACK_RELA_RPATH AND UNIX)
    set_target_properties(
        zis_start_shared PROPERTIES INSTALL_RPATH
        "$<IF:$<PLATFORM_ID:Darwin>,@executable_path,$ORIGIN>/../${zis_install_lib_dir}"
    )
endif()

### Install rules.
install(TARGETS ${zis_start_tgt} DESTINATION ${zis_install_exe_dir})
