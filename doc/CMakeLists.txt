########################################
##### C source code documentations #####
########################################

option(ZIS_DOC_SRC_API        "Doc: C public API"                           OFF)
option(ZIS_DOC_SRC_CORE       "Doc: source code in core/"                   OFF)
option(ZIS_DOC_SRC_START      "Doc: source code in start/"                  OFF)

if(ZIS_DOC_SRC_API OR ZIS_DOC_SRC_CORE OR ZIS_DOC_SRC_START)
    find_package(Doxygen)

    if(NOT DOXYGEN_FOUND)
        message(FATAL_ERROR "Cannot find doxygen.")
    endif()

    set(DOXYGEN_PROJECT_LOGO "${CMAKE_SOURCE_DIR}/misc/logo.svg")
    set(DOXYGEN_FILE_PATTERNS "*.h")
    set(DOXYGEN_EXCLUDE_PATTERNS ".*;_*")
    set(DOXYGEN_EXCLUDE_SYMBOLS "_*;E;ZIS_API")
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_EXTRACT_ALL YES) # ??
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_FULL_PATH_NAMES NO)
    set(DOXYGEN_DISABLE_INDEX YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_FULL_SIDEBAR YES)
    set(DOXYGEN_SHOW_NAMESPACES NO)
    set(DOXYGEN_SORT_MEMBER_DOCS NO)
    set(DOXYGEN_HTML_COLORSTYLE "LIGHT")
    set(DOXYGEN_HTML_COLORSTYLE_HUE "250")
    set(DOXYGEN_HTML_COLORSTYLE_SAT "72")
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_NO_PARAMDOC NO)
    set(DOXYGEN_QUIET YES)

    set(doxygen_in)
    if(ZIS_DOC_SRC_API)
        list(APPEND doxygen_in "${CMAKE_SOURCE_DIR}/include")
    endif()
    if(ZIS_DOC_SRC_CORE)
        list(APPEND doxygen_in "${CMAKE_SOURCE_DIR}/core")
    endif()
    if(ZIS_DOC_SRC_START)
        list(APPEND doxygen_in "${CMAKE_SOURCE_DIR}/start")
    endif()
    doxygen_add_docs(zis_doc_src ${doxygen_in} ALL)
    unset(doxygen_in)
endif()
