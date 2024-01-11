###
### Unix config format / Windows INI format parser.
###

function(read_conf var_prefix section_list_suffix key_list_suffix input_strings)
    set(section_list)
    set(current_section_name)
    set(current_section_keys)

    macro(end_section)
        if(NOT current_section_keys STREQUAL "")
            list(APPEND section_list ${current_section_name})
            if(key_list_suffix)
                set("${var_prefix}_${current_section_name}_${key_list_suffix}"
                    "${current_section_keys}" PARENT_SCOPE)
            endif()
        endif()
        set(current_section_name)
        set(current_section_keys)
    endmacro()

    foreach(line IN LISTS input_strings)
        string(STRIP "${line}" line)
        if(line MATCHES "\\[(.+)\\]")
            end_section()
            string(STRIP "${CMAKE_MATCH_1}" current_section_name)
        elseif(line MATCHES "(.+)[ \t]*=[ \t]*(.*)")
            string(STRIP "${CMAKE_MATCH_1}" current_key)
            string(STRIP "${CMAKE_MATCH_2}" current_val)
            list(APPEND current_section_keys "${current_key}")
            set("${var_prefix}_${current_section_name}_${current_key}"
                "${current_val}" PARENT_SCOPE)
        elseif(NOT ${line} OR ${line} MATCHES "^[#;].+")
            # Ignore this line.
        else()
            message(SEND_ERROR "Illegal config line: ${line}")
        endif()
    endforeach()
    end_section()

    if(section_list_suffix)
        set("${var_prefix}_${section_list_suffix}"
            "${section_list}" PARENT_SCOPE)
    endif()
endfunction()
