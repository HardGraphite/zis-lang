######################################
##### Options and configurations #####
######################################

set(ZIS_MALLOC_INCLUDE "" CACHE FILEPATH
    "Path to a file to include, which defines malloc(), realloc(), and free().")
set(ZIS_MALLOC_LINK "" CACHE FILEPATH
    "Path to a library to link, which provides the functions in `ZIS_MALLOC_INCLUDE'.")

##########################
##### Generate files #####
##########################

set(zis_conf_header_dir "${CMAKE_CURRENT_BINARY_DIR}")

file(CONFIGURE OUTPUT "${zis_conf_header_dir}/zis_config.h" @ONLY CONTENT [==[
#pragma once
#cmakedefine  ZIS_MALLOC_INCLUDE  "@ZIS_MALLOC_INCLUDE@"
]==])

################################
##### Collect source files #####
################################

aux_source_directory(. zis_lib_src)

################################
##### Define build targets #####
################################

if(ZIS_BUILD_SHARED)
    add_library(zis_lib_shared SHARED ${zis_lib_src})
    target_compile_definitions(
        zis_lib_shared
        PRIVATE "ZIS_EXPORT_API=1"
        INTERFACE "ZIS_IMPORT_API=1"
    )
    if(ZIS_MALLOC_LINK)
        target_link_libraries(zis_lib_shared PRIVATE ${ZIS_MALLOC_LINK})
    endif()
    set(zis_lib_tgt zis_lib_shared)
else()
    add_library(zis_lib_static STATIC ${zis_lib_src})
    if(ZIS_MALLOC_LINK)
        target_link_libraries(zis_lib_static INTERFACE ${ZIS_MALLOC_LINK})
    endif()
    set(zis_lib_tgt zis_lib_static)
endif()
# Output name and prefix.
set_target_properties(${zis_lib_tgt} PROPERTIES OUTPUT_NAME "${ZIS_NAME}")
get_target_property(prefix ${zis_lib_tgt} PREFIX)
if(NOT prefix)
    set_target_properties(${zis_lib_tgt} PROPERTIES PREFIX "lib")
endif()
# Include directory.
target_include_directories(
    ${zis_lib_tgt}
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
    PRIVATE "${zis_conf_header_dir}"
)

#######################################
##### Installation configurations #####
#######################################

include(installdir)

### Install rules.
install(TARGETS ${zis_lib_targets} DESTINATION ${zis_install_lib_dir})
if(ZIS_PACK_HEADER)
    file(GLOB zis_install_headers "${CMAKE_SOURCE_DIR}/include/*.h")
    install(FILES ${zis_install_headers} DESTINATION ${zis_install_inc_dir})
endif()
