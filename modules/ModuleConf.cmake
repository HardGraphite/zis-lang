include_guard(DIRECTORY)

include(ReadConf)

## zis_define_module_conf(<name> [SOURCE <src>...] [CONF <k>=<v> ...])
## Define a `module.conf` as a list in the "module.conf.cmake" file (top level).
function(zis_define_module_conf name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCE;CONF")
    if(DEFINED arg_SOURCE)
        set(__zis_module_conf_sources__ "${arg_SOURCE}" PARENT_SCOPE)
    endif()
    set(conf_strings "[module]" "name=${name}")
    if(DEFINED arg_CONF)
        foreach(entry IN LISTS)
            string(REPLACE ";" "||" entry "${entry}")
            list(APPEND conf_strings "${entry}")
        endforeach()
    endif()
    set(__zis_module_conf_strings__ "${conf_strings}" PARENT_SCOPE)
endfunction()

## Read `module.conf` from a C/C++ source file.
function(zis_read_module_conf_from_file file out_var)
    message(VERBOSE "Reading module.conf from ${file}")
    file(STRINGS ${file} conf_strings LIMIT_INPUT 1024 REGEX "^//%% .+$" ENCODING "UTF-8")
    if("${conf_strings}" STREQUAL "")
        message(FATAL_ERROR "No module.conf data in file ${file}")
    endif()
    string(REPLACE "//%% " "" conf_strings "${conf_strings}")
    set(${out_var} "${conf_strings}" PARENT_SCOPE)
endfunction()

function(__zis_read_module_conf_from_dir__read_cmake file)
    unset(__zis_module_conf_strings__)
    unset(__zis_module_conf_sources__)
    include("${file}")
    if(NOT DEFINED __zis_module_conf_strings__)
        message(FATAL_ERROR "No module.conf data in file ${file} (function zis_define_module_conf() never called)")
    endif()
    if(NOT DEFINED __zis_module_conf_sources__)
        set(__zis_module_conf_sources__ "NOTFOUND")
    endif()
    set(conf_strings "${__zis_module_conf_strings__}" PARENT_SCOPE)
    set(sources "${__zis_module_conf_sources__}" PARENT_SCOPE)
endfunction()

## Read `module.conf` and collect sources from a directory.
function(zis_read_module_conf_from_dir dir out_conf out_sources)
    message(VERBOSE "Reading module.conf from ${dir}")
    if(EXISTS "${dir}/module.conf")
        file(STRINGS "${file}/module.conf" conf_strings ENCODING "UTF-8")
        aux_source_directory("${dir}" sources)
    elseif(EXISTS "${dir}/module.conf.cmake")
        __zis_read_module_conf_from_dir__read_cmake("${dir}/module.conf.cmake")
        if(NOT "${sources}")
            aux_source_directory("${dir}" sources)
        endif()
    else()
        message(FATAL_ERROR "No module.conf[.cmake] in directory ${dir}")
    endif()
    if("${conf_strings}" STREQUAL "")
        message(FATAL_ERROR "Empty module.conf data in directory ${dir}")
    endif()
    set(${out_conf} "${conf_strings}" PARENT_SCOPE)
    set(${out_sources} "${sources}" PARENT_SCOPE)
endfunction()

file(STRINGS "module.conf" default_conf_strings ENCODING "UTF-8")
read_conf(__zis_module_conf_default OFF keys "[module];${default_conf_strings}")
unset(default_conf_strings)
message(VERBOSE "Loaded the default module.conf: keys = ${__zis_module_conf_default_module_keys}")

## Parse `module.conf` data. The values are read into `<out_var_prefix>_<key>` variables.
function(zis_parse_module_conf out_var_prefix conf_strings)
    read_conf(conf OFF keys "${conf_strings}")
    foreach(key IN LISTS __zis_module_conf_default_module_keys)
        if(DEFINED "conf_module_${key}")
            set(value "${conf_module_${key}}")
        else()
            set(value "${__zis_module_conf_default_module_${key}}")
        endif()
        string(REPLACE "||" ";" value "${value}")
        set("${out_var_prefix}_${key}" "${value}" PARENT_SCOPE)
    endforeach()
    list(REMOVE_ITEM conf_module_keys ${__zis_module_conf_default_module_keys})
    if(NOT ${conf_module_keys} STREQUAL "")
        message(WARNING "In the module.conf for ${conf_module_name}: unexpected keys: ${conf_module_keys}")
    endif()
endfunction()
