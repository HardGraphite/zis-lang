###
### ZiS: install directories
###

include_guard(DIRECTORY)

if(WIN32)
    set(zis_install_exe_dir ".")
    set(zis_install_lib_dir ".")
    set(zis_install_inc_dir "include")
    set(zis_install_mod_dir "lib")
else()
    include(GNUInstallDirs)
    if(NOT DEFINED ZIS_OUTPUT_NAME)
        message(FATAL_ERROR "`ZIS_OUTPUT_NAME' is not defined")
    endif()
    set(zis_install_exe_dir ${CMAKE_INSTALL_BINDIR})
    set(zis_install_lib_dir ${CMAKE_INSTALL_LIBDIR})
    set(zis_install_inc_dir ${CMAKE_INSTALL_INCLUDEDIR})
    set(zis_install_mod_dir "${CMAKE_INSTALL_LIBDIR}/${ZIS_OUTPUT_NAME}")
endif()

function(zis_install_set_rela_rpath target target_dir library_dir)
    if(UNIX)
        set(gen_expr_origin "$<IF:$<PLATFORM_ID:Darwin>,@executable_path,$ORIGIN>")
        file(RELATIVE_PATH rela_path "/${target_dir}" "/${library_dir}")
        message(VERBOSE "Set ${target}'s INSTALL_RPATH to {ORIGIN}/${rela_path}")
        set_target_properties(
            ${target} PROPERTIES INSTALL_RPATH
            "${gen_expr_origin}/${rela_path}"
        )
    endif()
endfunction()
