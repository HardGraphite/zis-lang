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

set(zis_core_conf_header_dir "${CMAKE_CURRENT_BINARY_DIR}")

file(CONFIGURE OUTPUT "${zis_core_conf_header_dir}/zis_config.h" @ONLY CONTENT [==[
#pragma once
#define         ZIS_VERSION_MAJOR   @ZIS_VERSION_MAJOR@
#define         ZIS_VERSION_MINOR   @ZIS_VERSION_MINOR@
#define         ZIS_VERSION_PATCH   @ZIS_VERSION_PATCH@
#cmakedefine    ZIS_DISPLAY_NAME    "@ZIS_DISPLAY_NAME@"
#cmakedefine    ZIS_MALLOC_INCLUDE  "@ZIS_MALLOC_INCLUDE@"
#cmakedefine01  ZIS_DEBUG
#cmakedefine01  ZIS_DEBUG_LOGGING
]==])

################################
##### Collect source files #####
################################

aux_source_directory(. zis_core_src)

################################
##### Define build targets #####
################################

if(ZIS_BUILD_SHARED)
    add_library(zis_core_shared SHARED ${zis_core_src})
    target_compile_definitions(
        zis_core_shared
        PRIVATE "ZIS_EXPORT_API=1"
        INTERFACE "ZIS_IMPORT_API=1"
    )
    if(ZIS_MALLOC_LINK)
        target_link_libraries(zis_core_shared PRIVATE ${ZIS_MALLOC_LINK})
    endif()
    set(zis_core_tgt zis_core_shared)
else()
    add_library(zis_core_static STATIC ${zis_core_src})
    if(ZIS_MALLOC_LINK)
        target_link_libraries(zis_core_static INTERFACE ${ZIS_MALLOC_LINK})
    endif()
    set(zis_core_tgt zis_core_static)
endif()
set(zis_core_tgt ${zis_core_tgt} PARENT_SCOPE)
# Output name and prefix.
set_target_properties(${zis_core_tgt} PROPERTIES OUTPUT_NAME "${ZIS_OUTPUT_NAME}")
get_target_property(prefix ${zis_core_tgt} PREFIX)
if(NOT prefix)
    set_target_properties(${zis_core_tgt} PROPERTIES PREFIX "lib")
endif()
# Include directory.
target_include_directories(
    ${zis_core_tgt}
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
    PRIVATE "${zis_core_conf_header_dir}"
)
# Link libraries.
if(UNIX)
    if(ZIS_BUILD_SHARED)
        set(scope PRIVATE)
    else()
        set(scope INTERFACE)
    endif()
    target_link_libraries(${zis_core_tgt} ${scope} "m")
    unset(scope)
endif()

if (WIN32 AND ZIS_BUILD_SHARED)
    include(winutil)
    zis_win_target_add_rc(
        ${zis_core_tgt}
        "${ZIS_DISPLAY_NAME} programming language runtime core"
        "${ZIS_DISPLAY_NAME}LangCore"
        OFF
    )
endif()

#######################################
##### Installation configurations #####
#######################################

include(installdir)

### Install rules.
install(TARGETS ${zis_core_tgt} DESTINATION ${zis_install_lib_dir})
if(ZIS_PACK_HEADER)
    file(GLOB zis_install_headers "${CMAKE_SOURCE_DIR}/include/*.h")
    install(FILES ${zis_install_headers} DESTINATION ${zis_install_inc_dir})
endif()
