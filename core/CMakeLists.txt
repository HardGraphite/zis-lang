######################################
##### Options and configurations #####
######################################

option(ZIS_FEATURE_ASM       "Enable the assembly code support."             ON)
option(ZIS_FEATURE_DIS       "Enable the disassembling support."             ON)
option(ZIS_FEATURE_SRC       "Enable the source code support."               ON)

set(ZIS_MALLOC_INCLUDE "" CACHE FILEPATH
    "Path to a file to include, which defines malloc(), realloc(), and free().")
set(ZIS_MALLOC_LINK "" CACHE FILEPATH
    "Path to a library to link, which provides the functions in `ZIS_MALLOC_INCLUDE'.")

set(ZIS_FILENAME_EXTENSION_SRC ".zis" CACHE STRING "File name extension for source code.")
set(ZIS_FILENAME_EXTENSION_NDL ".zso" CACHE STRING "File name extension for native dynamic libraries.")
set(ZIS_FILENAME_EXTENSION_BYT ".zsc" CACHE STRING "File name extension for bytecode dumps.")
set(ZIS_FILENAME_EXTENSION_ASM ".zsa" CACHE STRING "File name extension for assembly code.")

string(TOUPPER ${ZIS_OUTPUT_NAME} zis_name_upper)
set(ZIS_ENVIRON_NAME_PATH "${zis_name_upper}_PATH" CACHE STRING
    "Environment variable name for search path. Optional.")
set(ZIS_ENVIRON_NAME_MEMS "${zis_name_upper}_MEMS" CACHE STRING
    "Environment variable name for memory configuration. Optional.")
set(ZIS_ENVIRON_NAME_DEBUG_LOG "${zis_name_upper}_DEBUG_LOG" CACHE STRING
    "Environment variable name for debug logging configuration. Optional.")
unset(zis_name_upper)

##########################
##### Generate files #####
##########################

set(zis_core_conf_header_dir "${CMAKE_CURRENT_BINARY_DIR}")
set(zis_core_conf_header_dir ${zis_core_conf_header_dir} PARENT_SCOPE)

file(CONFIGURE OUTPUT "${zis_core_conf_header_dir}/zis_config.h" @ONLY CONTENT [==[
#pragma once
#define         ZIS_VERSION_MAJOR   @ZIS_VERSION_MAJOR@
#define         ZIS_VERSION_MINOR   @ZIS_VERSION_MINOR@
#define         ZIS_VERSION_PATCH   @ZIS_VERSION_PATCH@
#define         ZIS_FILENAME_EXTENSION_SRC "@ZIS_FILENAME_EXTENSION_SRC@"
#define         ZIS_FILENAME_EXTENSION_NDL "@ZIS_FILENAME_EXTENSION_NDL@"
#define         ZIS_FILENAME_EXTENSION_BYT "@ZIS_FILENAME_EXTENSION_BYT@"
#define         ZIS_FILENAME_EXTENSION_ASM "@ZIS_FILENAME_EXTENSION_ASM@"
#cmakedefine    ZIS_ENVIRON_NAME_PATH "@ZIS_ENVIRON_NAME_PATH@"
#cmakedefine    ZIS_ENVIRON_NAME_MEMS "@ZIS_ENVIRON_NAME_MEMS@"
#cmakedefine    ZIS_ENVIRON_NAME_DEBUG_LOG "@ZIS_ENVIRON_NAME_DEBUG_LOG@"
#cmakedefine    ZIS_DISPLAY_NAME    "@ZIS_DISPLAY_NAME@"
#cmakedefine    ZIS_MALLOC_INCLUDE  "@ZIS_MALLOC_INCLUDE@"
#cmakedefine01  ZIS_USE_COMPUTED_GOTO
#cmakedefine01  ZIS_DEBUG
#cmakedefine01  ZIS_DEBUG_LOGGING
#cmakedefine01  ZIS_FEATURE_ASM
#cmakedefine01  ZIS_FEATURE_DIS
#cmakedefine01  ZIS_FEATURE_SRC
]==])

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/astdef.h"
    COMMAND ${CMAKE_COMMAND} -P "astdef.cmake"
    MAIN_DEPENDENCY "astdef.txt"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating astdef.h"
    VERBATIM
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/oplist.h"
    COMMAND ${CMAKE_COMMAND} -P "oplist.cmake"
    MAIN_DEPENDENCY "oplist.txt"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating oplist.h"
    VERBATIM
)

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
    target_compile_definitions(
        zis_core_static
        PRIVATE "ZIS_EXPORT_API=1"
        INTERFACE "ZIS_EXPORT_API=1"
    )
    if(ZIS_MALLOC_LINK)
        target_link_libraries(zis_core_static INTERFACE ${ZIS_MALLOC_LINK})
    endif()
    set(zis_core_tgt zis_core_static)
endif()
set(zis_core_tgt ${zis_core_tgt} PARENT_SCOPE)
# Extra dependencies.
target_sources(${zis_core_tgt} PRIVATE "astdef.h" "oplist.h")
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
    target_link_libraries(${zis_core_tgt} ${scope} "m" "dl")
    unset(scope)
endif()

if (WIN32 AND ZIS_BUILD_SHARED)
    include(ZisWinUtil)
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

include(ZisInstallDirs)

### Install rules.
install(TARGETS ${zis_core_tgt} DESTINATION ${zis_install_lib_dir})
if(ZIS_PACK_HEADER)
    file(GLOB zis_install_headers "${CMAKE_SOURCE_DIR}/include/*.h")
    install(FILES ${zis_install_headers} DESTINATION ${zis_install_inc_dir})
endif()
