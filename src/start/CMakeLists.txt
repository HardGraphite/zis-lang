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

add_executable(zis_start_tgt ${zis_start_src})
target_link_libraries(zis_start_tgt PRIVATE zis_core_tgt)
set_target_properties(zis_start_tgt PROPERTIES OUTPUT_NAME "${ZIS_OUTPUT_NAME}")
target_include_directories(zis_start_tgt PRIVATE ${zis_src_generated_code_dir})
if(NOT ZIS_BUILD_SHARED)
    set_target_properties(zis_start_tgt PROPERTIES ENABLE_EXPORTS TRUE)
endif()

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
        zis_start_tgt "${desc}" "${prod}"
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
if(ZIS_PACK_RELA_RPATH AND TARGET zis_start_shared)
    zis_install_set_rela_rpath(zis_start_shared ${zis_install_exe_dir} ${zis_install_lib_dir})
endif()

### Install rules.
install(TARGETS zis_start_tgt DESTINATION ${zis_install_exe_dir})
