option(ZIS_BUILD_CORE    "Build `core', the ZIS library"                     ON)
option(ZIS_BUILD_START   "Build `start', an executable entry"                ON)
option(ZIS_BUILD_MODULES "Build `modules', the modules"                      ON)

if(ZIS_BUILD_CORE)
    add_subdirectory("core")
endif()

if(ZIS_BUILD_START)
    add_subdirectory("start")
endif()

if(ZIS_BUILD_MODULES)
    add_subdirectory("modules")
endif()

if(DEFINED zis_start_tgt)
    set(zis_start_tgt ${zis_start_tgt} PARENT_SCOPE) # test/CMakeLists.txt will use this
endif()
if(DEFINED zis_core_tgt)
    set(zis_core_tgt ${zis_core_tgt} PARENT_SCOPE) # test/CMakeLists.txt will use this
endif()
if(DEFINED zis_core_conf_header_dir)
    set(zis_core_conf_header_dir ${zis_core_conf_header_dir} PARENT_SCOPE)
endif()
if(DEFINED zis_modules_header_dir)
    set(zis_modules_header_dir ${zis_modules_header_dir} PARENT_SCOPE)
endif()
