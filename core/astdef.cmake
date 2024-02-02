## Generate "astdef.h" from "astdef.txt"

function(_zis_astdef_string_ljust var n)
    set(s "${${var}}")
    string(LENGTH "${s}" s_len)
    if (s_len LESS n)
        math(EXPR padding_n "${n} - ${s_len}")
        string(REPEAT " " ${padding_n} padding)
        set(${var} "${s}${padding}" PARENT_SCOPE)
    endif()
endfunction()

function(zis_update_astdef input_file output_file)
    set(result_structs_string)
    set(result_name_list_string "#define ZIS_AST_NODE_LIST \\\n")

    file(STRINGS ${input_file} ast_def_lines REGEX "^[^#].+$" ENCODING "UTF-8")
    foreach(ast_def_line IN LISTS ast_def_lines)
        string(STRIP ${ast_def_line} ast_def_line)
        if(NOT ast_def_line MATCHES "^([0-9a-zA-Z]+)[ \t]+\\([ \t]*(.+)[ \t]*\\)$")
            message(FATAL_ERROR "Illegal line: ${ast_def_line}")
        endif()
        set(node_name ${CMAKE_MATCH_1})
        string(STRIP ${CMAKE_MATCH_2} node_field_list)
        string(REGEX REPLACE "[ \t]*,[ \t]*" ";" node_field_list ${node_field_list})

        set(field_name_list)
        string(APPEND result_structs_string "struct zis_ast_node_${node_name}_data {\n")
        foreach(node_field IN LISTS node_field_list)
            if(NOT node_field MATCHES "([0-9a-zA-Z<>]+)[ \t]+([0-9a-z_]+)")
                message(FATAL_ERROR "Illegal field list (${node_name}): ${node_field_list}")
            endif()
            set(field_type ${CMAKE_MATCH_1})
            set(field_name ${CMAKE_MATCH_2})
            set(field_type_node_type_name)

            list(APPEND field_name_list ${field_name})

            if (field_type STREQUAL "Node")
                set(field_c_type "struct zis_ast_node_obj *")
            elseif (field_type MATCHES "Node<(.+)>")
                set(field_type_node_type_name ${CMAKE_MATCH_1})
                set(field_c_type "struct zis_ast_node_obj *")
            elseif(field_type STREQUAL "Object")
                set(field_c_type "struct zis_object *")
            else()
                string(TOLOWER ${field_type} field_c_type)
                set(field_c_type "struct zis_${field_c_type}_obj *")
            endif()
            if(field_type_node_type_name)
                set(field_comment " // ${field_type_node_type_name}")
            else()
                set(field_comment)
            endif()
            string(APPEND result_structs_string "    ${field_c_type}${field_name};${field_comment}\n")
        endforeach()
        string(APPEND result_structs_string "};\n\n")

        set(node_name_formatted ${node_name})
        _zis_astdef_string_ljust(node_name_formatted 15)
        string(REPLACE ";" "," field_names_formatted "${field_name_list}")
        string(APPEND result_name_list_string "    E(${node_name_formatted}, \"${field_names_formatted}\") \\\n")
    endforeach()

    string(APPEND result_name_list_string "// ^^^ ZIS_AST_NODE_LIST ^^^\n")

    message(NOTICE "${input_file} -> ${output_file}")
    get_filename_component(input_file_stem ${input_file} NAME)
    file(WRITE "${output_file}"
        "// Generated from \"${input_file_stem}\".\n\n"
        "#pragma once\n\n"
        "${result_structs_string}"
        "${result_name_list_string}"
    )
endfunction()

if(CMAKE_SCRIPT_MODE_FILE) # cmake -P <this-file>
    get_filename_component(dir ${CMAKE_SCRIPT_MODE_FILE} DIRECTORY)
    zis_update_astdef("${dir}/astdef.txt" "${dir}/astdef.h")
endif()
