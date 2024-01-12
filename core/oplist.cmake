## Generate "oplist.h" from "oplist.txt"

set(zis_oplist_line_regex
    "^(0x[0-9a-f][0-9a-f])[ \t]+([A-Z]+)[ \t]+([a-zA-Z0-9_:,]+)[ \t]+#(.+)$")
#    OPCODE                    OP-NAME       OPERANDS             DESCRIPTION

set(zis_oplist_max_code 127)

function(_zis_oplist_string_ljust var n)
    set(s "${${var}}")
    string(LENGTH "${s}" s_len)
    if (s_len LESS n)
        math(EXPR padding_n "${n} - ${s_len}")
        string(REPEAT " " ${padding_n} padding)
        set(${var} "${s}${padding}" PARENT_SCOPE)
    endif()
endfunction()

function(_zis_oplist_number_to_hex num out_var width)
    math(EXPR hex ${num} OUTPUT_FORMAT HEXADECIMAL)
    string(LENGTH ${hex} n)
    if (n LESS width)
        math(EXPR padding_n "${width} - ${n}")
        string(REPEAT "0" ${padding_n} padding)
        string(SUBSTRING ${hex} 2 -1 hex)
        set(hex "0x${padding}${hex}")
    endif()
    set(${out_var} ${hex} PARENT_SCOPE)
endfunction()

function(zis_update_oplist input_file output_file)
    set(op_name_list)

    file(STRINGS ${input_file} op_list_lines REGEX "^[^#].+$" ENCODING "UTF-8")
    foreach(op_def_line IN LISTS op_list_lines)
        string(STRIP ${op_def_line} op_def_line)
        if(NOT op_def_line MATCHES "${zis_oplist_line_regex}")
            message(FATAL_ERROR "In \"${input_file}\": illegal line: ${op_def_line}")
        endif()
        set(this_op_code ${CMAKE_MATCH_1})
        set(this_op_name ${CMAKE_MATCH_2})
        set(this_op_operands ${CMAKE_MATCH_3})
        list(APPEND op_name_list ${this_op_name})
        set("OP_${this_op_name}_CODE" ${this_op_code})
        set("OP_${this_op_code}_NAME" ${this_op_name})
    endforeach()

    set(op_name_list_sorted "${op_name_list}")
    list(SORT op_name_list_sorted COMPARE STRING)

    set(f_name "E")

    set(result_list_string "\n#define ZIS_OP_LIST \\\n")
    foreach(name IN LISTS op_name_list)
        set(opname_text ${name})
        _zis_oplist_string_ljust(opname_text 8)
        string(APPEND result_list_string "    ${f_name}(${OP_${name}_CODE}, ${opname_text}) \\\n")
    endforeach()
    string(APPEND result_list_string "// ^^^ ZIS_OP_LIST ^^^\n")

    set(result_list_full_string "\n#define ZIS_OP_LIST_FULL \\\n")
    foreach(opcode RANGE ${zis_oplist_max_code})
        _zis_oplist_number_to_hex(${opcode} opcode 4)
        set(opname_var "OP_${opcode}_NAME")
        if(DEFINED ${opname_var})
            set(opname_text ${${opname_var}})
        else()
            set(opname_text)
        endif()
        _zis_oplist_string_ljust(opname_text 8)
        string(APPEND result_list_full_string "    ${f_name}(${opcode}, ${opname_text}) \\\n")
    endforeach()
    string(APPEND result_list_full_string "// ^^^ ZIS_OP_LIST_FULL ^^^\n")

    set(result_list_by_name_string "\n#define ZIS_OP_LIST_BY_NAME \\\n")
    foreach(name IN LISTS op_name_list_sorted)
        set(opname_text ${name})
        _zis_oplist_string_ljust(opname_text 8)
        string(APPEND result_list_by_name_string "    ${f_name}(${opname_text}, ${OP_${name}_CODE}) \\\n")
    endforeach()
    string(APPEND result_list_by_name_string "// ^^^ ZIS_OP_LIST_BY_NAME ^^^\n")

    message(NOTICE "${input_file} -> ${output_file}")
    get_filename_component(input_file_stem ${input_file} NAME)
    file(WRITE "${output_file}"
        "// Generated from \"${input_file_stem}\".\n\n"
        "#pragma once\n"
        ${result_list_string}
        ${result_list_full_string}
        ${result_list_by_name_string}
    )
endfunction()

if(CMAKE_SCRIPT_MODE_FILE) # cmake -P <this-file>
    get_filename_component(dir ${CMAKE_SCRIPT_MODE_FILE} DIRECTORY)
    zis_update_oplist("${dir}/oplist.txt" "${dir}/oplist.h")
endif()
