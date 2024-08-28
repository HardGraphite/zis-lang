##
## Use a CheckXxxSupported module.
##

## check_support(check_module check_function description [FORCE] [RESULT <res_var>] [CACHE <res_cache_var>] [OTUPUT <out_var>] [OPTIONS ...])
function(check_support check_module check_function description)
    cmake_parse_arguments(PARSE_ARGV 3 arg "FORCE" "RESULT;CACHE;OUTPUT" "OPTIONS")

    if(DEFINED arg_CACHE AND DEFINED CACHE{${arg_CACHE}})
        if(NOT arg_FORCE)
            message(STATUS "Check for ${description} support: ${${arg_CACHE}} - skipped")
            return()
        endif()
    endif()

    message(CHECK_START "Checking ${description} support")

    if(check_module)
        include(${check_module})
    endif()
    cmake_language(CALL ${check_function} RESULT res OUTPUT out ${arg_OPTIONS})

    if(DEFINED arg_RESULT)
        set(${arg_RESULT} ${res} PARENT_SCOPE)
    endif()
    if(DEFINED arg_CACHE)
        set(${arg_CACHE} ${res} CACHE BOOL "")
        mark_as_advanced(${arg_CACHE})
    endif()
    if(DEFINED arg_OUTPUT)
        set(${arg_OUTPUT} ${out} PARENT_SCOPE)
    endif()

    if(res)
        message(CHECK_PASS "YES")
    else()
        message(CHECK_FAIL "NO")
        message(VERBOSE "${out}")
    endif()
endfunction()

## disable_if_unsupported(switch_var check_result description)
function(disable_if_unsupported switch_var check_result description)
    if(${${switch_var}})
        if(${check_result})
            message(STATUS "${switch_var} enabled")
        else()
            message(WARNING "${description} is not supported; ${switch_var} disabled")
            set(${switch_var} OFF PARENT_SCOPE)
        endif()
    endif()
endfunction()
