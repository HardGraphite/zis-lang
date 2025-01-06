include("ModuleConf.cmake")

#################################
##### Define module targets #####
#################################

set(zis_embedded_modules) # List of names
set(zis_module_targets)   # List of targets

function(zis_add_module_target mod_name conf_strings sources)
    zis_parse_module_conf(mod_conf "${conf_strings}")
    string(TOUPPER ${mod_name} mod_name_upper)
    message(VERBOSE "Found ZiS module `${mod_name}': ${mod_conf_name} -- ${mod_conf_description}")

    function(send_error_about_conf msg)
        message(SEND_ERROR "In the module.conf for `${mod_name}': ${msg}")
    endfunction()

    # KEY name
    if(NOT mod_conf_name STREQUAL mod_name)
        send_error_about_conf("module name mismatches")
    endif()
    # KEY description
    if(NOT mod_conf_description)
        send_error_about_conf("no description")
    endif()
    set(mod_desc "${mod_conf_description}")
    # KEY when
    if(NOT "${mod_conf_when}" STREQUAL "")
        cmake_language(EVAL CODE "
            if(${mod_conf_when})
                set(mod_available TRUE)
            else()
                set(mod_available FALSE)
            endif()
        ")
        if(NOT mod_available)
            message(VERBOSE "Module ${mod_name} is not available: (${mod_conf_when}) -> ${mod_available}")
            return()
        endif()
        unset(mod_available)
    endif()
    # KEY link-libraries
    set(mod_link_libs ${mod_conf_link-libraries})
    # KEY required, default-enabled
    set(opt_name_enabled "ZIS_MOD_${mod_name_upper}")
    if(${mod_conf_required})
        set(${opt_name_enabled} TRUE)
    else()
        option(${opt_name_enabled} "Module `${mod_name}' -- ${mod_desc}" "${mod_conf_default-enabled}")
    endif()
    # KEY force-embedded, default-embedded
    set(opt_name_embedded "ZIS_MOD_${mod_name_upper}_EMBED")
    if(${mod_conf_force-embedded})
        set(${opt_name_embedded} TRUE)
    else()
        option(${opt_name_embedded} "Module `${mod_name}': embedded into the core" "${mod_conf_default-embedded}")
    endif()

    if(${opt_name_enabled})
        message(STATUS "Add ZiS module `${mod_name}' (embedded=${${opt_name_embedded}})")
        if(${opt_name_embedded}) # static
            if(zis_embedded_modules)
                set(zis_embedded_modules "${zis_embedded_modules};${mod_name}" PARENT_SCOPE)
            else()
                set(zis_embedded_modules ${mod_name} PARENT_SCOPE)
            endif()
            message(VERBOSE "Add module `${mod_name}' sources to target zis_core_tgt")
            target_sources(zis_core_tgt PRIVATE ${sources})
            if(mod_link_libs)
                if(ZIS_BUILD_SHARED)
                    set(scope PRIVATE)
                else()
                    set(scope INTERFACE)
                endif()
                target_link_libraries(zis_core_tgt ${scope} ${mod_link_libs})
            endif()
            if(${mod_conf_force-embedded})
                target_include_directories(zis_core_tgt PRIVATE "${CMAKE_SOURCE_DIR}/src")
            endif()
        else() # shared
            set(mod_tgt "zis_mod_${mod_name}")
            if(zis_module_targets)
                set(zis_module_targets "${zis_module_targets};${mod_tgt}" PARENT_SCOPE)
            else()
                set(zis_module_targets ${mod_tgt} PARENT_SCOPE)
            endif()
            message(VERBOSE "Add target ${mod_tgt} for module `${mod_name}'")
            add_library(${mod_tgt} MODULE ${sources})
            target_include_directories(${mod_tgt} PRIVATE "${CMAKE_SOURCE_DIR}/include")
            if(WIN32)
                if(ZIS_BUILD_SHARED)
                    target_link_libraries(${mod_tgt} PRIVATE zis_core_tgt)
                else()
                    target_link_libraries(${mod_tgt} PRIVATE zis_start_tgt)
                endif()
            elseif(ANDROID)
                if(ZIS_BUILD_SHARED)
                    target_link_libraries(${mod_tgt} PRIVATE zis_core_tgt)
                endif()
            endif()
            target_link_libraries(${mod_tgt} PRIVATE ${mod_link_libs})
            set_target_properties(
                ${mod_tgt} PROPERTIES
                PREFIX ""
                OUTPUT_NAME ${mod_name}
                SUFFIX ${ZIS_FILENAME_EXTENSION_NDL}
            )
        endif()
    endif()
endfunction()

###########################
##### Collect modules #####
###########################

file(GLOB all_files "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(file IN LISTS all_files)
    get_filename_component(name ${file} NAME_WLE)
    if(IS_DIRECTORY ${file})
        zis_read_module_conf_from_dir("${file}" conf_strings sources)
        zis_add_module_target(${name} "${conf_strings}" "${sources}")
    elseif(file MATCHES ".*\\.cc?$")
        zis_read_module_conf_from_file("${file}" conf_strings)
        zis_add_module_target(${name} "${conf_strings}" "${file}")
    endif()
endforeach()
unset(all_files)

################################
##### Generate module list #####
################################

if(NOT IS_DIRECTORY ${zis_src_generated_code_dir})
    message(FATAL_ERROR "${zis_src_generated_code_dir} is not a directory")
endif()

if(zis_embedded_modules)
    list(SORT zis_embedded_modules COMPARE STRING)
    set(ZIS_EMBEDDED_MODULE_LIST_SORTED TRUE)
    set(f_name "E")
    string(REPLACE ";" ") \\\n${f_name}(" ZIS_EMBEDDED_MODULE_LIST "${zis_embedded_modules}")
    set(ZIS_EMBEDDED_MODULE_LIST "${f_name}(${ZIS_EMBEDDED_MODULE_LIST})")
    unset(f_name)
else()
    set(ZIS_EMBEDDED_MODULE_LIST_EMPTY TRUE)
    set(ZIS_EMBEDDED_MODULE_LIST "// (empty)")
endif()
file(CONFIGURE OUTPUT "${zis_src_generated_code_dir}/zis_modules.h" @ONLY CONTENT [==[
#pragma once

#define ZIS_EMBEDDED_MODULE_LIST \
@ZIS_EMBEDDED_MODULE_LIST@

#cmakedefine01 ZIS_EMBEDDED_MODULE_LIST_SORTED
#cmakedefine01 ZIS_EMBEDDED_MODULE_LIST_EMPTY
]==])
unset(ZIS_EMBEDDED_MODULE_LIST)
unset(ZIS_EMBEDDED_MODULE_LIST_SORTED)
unset(ZIS_EMBEDDED_MODULE_LIST_EMPTY)

target_include_directories(zis_core_tgt PRIVATE ${zis_src_generated_code_dir})

#######################################
##### Installation configurations #####
#######################################

include(ZisInstallDirs)

foreach(tgt IN LISTS zis_module_targets)
    ### RPATH.
    if(ZIS_PACK_RELA_RPATH AND ANDROID)
        zis_install_set_rela_rpath(${tgt} ${zis_install_mod_dir} ${zis_install_lib_dir})
    endif()
    ### Install rules.
    install(TARGETS ${tgt} DESTINATION ${zis_install_mod_dir})
endforeach()
