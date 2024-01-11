###
### ZiS: MS Windows build utilities
###

## Add resource script file to target.
function(zis_win_target_add_rc target_name description product_name icon_file)
    # Get target file type.
    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
        set(rc_filetype "VFT_APP")
    elseif(target_type STREQUAL "SHARED_LIBRARY")
        set(rc_filetype "VFT_DLL")
    elseif(target_type STREQUAL "STATIC_LIBRARY")
        set(rc_filetype "VFT_STATIC_LIB")
    else()
        set(rc_filetype "VFT_UNKNOWN")
    endif()

    # Get original filename (including prefix and suffix).
    get_target_property(rc_original_filename ${target_name} OUTPUT_NAME)
    get_target_property(target_prefix ${target_name} PREFIX)
    if(target_prefix)
        set(rc_original_filename "${target_prefix}${rc_original_filename}")
    endif()
    get_target_property(target_suffix ${target_name} SUFFIX)
    if(NOT target_suffix)
        if(target_type STREQUAL "EXECUTABLE")
            set(target_suffix ".exe")
        elseif(target_type STREQUAL "SHARED_LIBRARY")
            set(target_suffix ".dll")
        elseif(target_type STREQUAL "STATIC_LIBRARY")
            set(target_suffix ".lib")
        else()
            set(target_suffix "")
        endif()
    endif()
    set(rc_original_filename "\"${rc_original_filename}${target_suffix}\"")

    # Icon.
    if(icon_file)
        set(rc_icon_line "1 ICON \"${icon_file}\"")
    endif()

    # Other info.
    set(rc_version_num "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(rc_version_str "\"${PROJECT_VERSION}\"")
    set(rc_company_name "\"\"")
    set(rc_file_description "\"${description}\"")
    set(rc_product_name "\"${product_name}\"")

    # Calculate RC file path.
    get_target_property(target_bin_dir ${target_name} BINARY_DIR)
    set(rc_file_path "${target_bin_dir}/${target_name}.rc")

    # Generate RC file.
    file(CONFIGURE OUTPUT "${rc_file_path}" @ONLY CONTENT [==[
#include <winver.h>

VS_VERSION_INFO VERSIONINFO
FILEVERSION     @rc_version_num@
PRODUCTVERSION  @rc_version_num@
FILEOS          VOS__WINDOWS32
FILETYPE        @rc_filetype@
FILESUBTYPE     VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName",      @rc_company_name@
            VALUE "FileDescription",  @rc_file_description@
            VALUE "FileVersion",      @rc_version_str@
            VALUE "InternalName",     "@target_name@"
            VALUE "LegalCopyright",   ""
            VALUE "OriginalFilename", @rc_original_filename@
            VALUE "ProductName",      @rc_product_name@
            VALUE "ProductVersion",   @rc_version_str@
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END

@rc_icon_line@
    ]==])

    # Add RC to target.
    target_sources(${target_name} PRIVATE ${rc_file_path})
endfunction()
