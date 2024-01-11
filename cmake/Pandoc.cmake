###
### Pandoc doc conversion support
###

## `pandoc_executable` and `pandoc_found`
if(NOT DEFINED pandoc_executable)
    find_program(
        pandoc_executable
        NAMES pandoc
        HOST
    )
    if(pandoc_executable)
        message(VERBOSE "Found pandoc: ${pandoc_executable}")
    endif()
endif()

## pandoc_add_doc(<target> <source> [ALL] [OUTPUT_FILE <path>| OUTPUT_FORMAT <format>] [STANDALONE] [OPTION <pandoc-options>])
function(pandoc_add_doc target source)
    if(NOT pandoc_executable)
        message(WARNING "Pandoc executable not found; target will fail")
    endif()

    cmake_parse_arguments(PARSE_ARGV 2 arg "ALL;STANDALONE" "OUTPUT_FILE;OUTPUT_FORMAT" "OPTION")

    set(command)
    list(APPEND command ${pandoc_executable} ${source})
    if(arg_ALL)
        set(_all ALL)
    endif()
    if(DEFINED arg_OUTPUT_FILE)
        set(output ${arg_OUTPUT_FILE})
        list(APPEND command "-o" ${output})
    elseif(DEFINED arg_OUTPUT_FORMAT)
        get_filename_component(source_filename_stem ${source} NAME_WLE)
        set(output "${CMAKE_CURRENT_BINARY_DIR}/${source_filename_stem}.${arg_OUTPUT_FORMAT}")
        list(APPEND command "-o" ${output} "-t" ${arg_OUTPUT_FORMAT})
    else()
        message(FATAL_ERROR "argument OUTPUT_FILE/OUTPUT_FORMAT is not given")
    endif()
    if(arg_STANDALONE)
        list(APPEND command "--standalone")
    endif()
    if(DEFINED arg_OPTION)
        list(APPEND command ${arg_OPTION})
    endif()

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${command}
        VERBATIM
        COMMENT "Run pandoc for ${target}"
        MAIN_DEPENDENCY ${source}
    )
    add_custom_target(
        ${target} ${_all}
        DEPENDS ${output}
        SOURCES ${source}
    )
endfunction()
