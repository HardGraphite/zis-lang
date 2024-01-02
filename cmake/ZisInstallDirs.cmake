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