/// Filesystem utilities.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h> // FILENAME_MAX

#include "attributes.h"
#include "platform.h"

#if ZIS_SYSTEM_WINDOWS
#    include <wchar.h> // wchar_t
#    define ZIS_FS_WINDOWS 1
#elif ZIS_SYSTEM_POSIX
#    define ZIS_FS_POSIX 1
#else
#    error "unknown system"
#endif

/* ----- path characters and strings ---------------------------------------- */

#define ZIS_PATH_MAX  FILENAME_MAX

#if ZIS_FS_POSIX

typedef char zis_path_char_t;

#define ZIS_PATH_CHR(C)   C
#define ZIS_PATH_STR(S)   S
#define ZIS_PATH_CHR_PRI  "c"
#define ZIS_PATH_STR_PRI  "s"

#define ZIS_PATH_PREFERRED_DIR_SEP      ZIS_PATH_CHR('/')
#define ZIS_PATH_PREFERRED_DIR_SEP_STR  ZIS_PATH_STR("/")

#elif ZIS_FS_WINDOWS

typedef wchar_t zis_path_char_t;

#define _ZIS_PATH_CHR(C)  L ## C
#define _ZIS_PATH_STR(S)  L ## S
#define ZIS_PATH_CHR(C)   _ZIS_PATH_CHR(C)
#define ZIS_PATH_STR(S)   _ZIS_PATH_STR(S)
#define ZIS_PATH_CHR_PRI  "lc"
#define ZIS_PATH_STR_PRI  "ls"

#define ZIS_PATH_PREFERRED_DIR_SEP      ZIS_PATH_CHR('\\')
#define ZIS_PATH_PREFERRED_DIR_SEP_STR  ZIS_PATH_STR("\\")

#endif

/// Get path string length (number of `zis_path_char_t` characters).
size_t zis_path_len(const zis_path_char_t *path);

/// Allocate a path string. `len` is the max number of characters (excluding terminating NUL).
zis_nodiscard zis_path_char_t *zis_path_alloc(size_t len);

/// Duplicate a path. Free the path with `zis_mem_free()` to avoid a memory leak.
zis_nodiscard zis_path_char_t *zis_path_dup(const zis_path_char_t *path);

/// Duplicate first `len` chars of a path. Free the path with `zis_mem_free()` to avoid a memory leak.
zis_nodiscard zis_path_char_t *zis_path_dup_n(const zis_path_char_t *path, size_t len);

/// Convert string `str` to path and call `fn`.
int zis_path_with_temp_path_from_str(
    const char *str,
    int (*fn)(const zis_path_char_t *, void *), void *fn_arg
);

/// Convert path `path` to UTF-8 string and call `fn`.
int zis_path_with_temp_str_from_path(
    const zis_path_char_t *path,
    int (*fn)(const char *, void *), void *fn_arg
);

/// Copy the path string including the terminating NUL. Returns the length of `src`.
size_t zis_path_copy(zis_path_char_t *dst, const zis_path_char_t *src);

/// Copy the path string. Parameter `len` is the number of chars to copy.
void zis_path_copy_n(zis_path_char_t *dst, const zis_path_char_t *src, size_t len);

/// Concatenate two path strings. Writes the result to `buf` and returns the result length.
size_t zis_path_concat(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, const zis_path_char_t *path2
);

/// Concatenate two path strings. Writes the result to `buf` and returns the result length.
size_t zis_path_concat_n(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, size_t path1_len,
    const zis_path_char_t *path2, size_t path2_len
);

/// Join two paths. Writes the result to `buf` and returns the result length.
size_t zis_path_join(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, const zis_path_char_t *path2
);

/// Join two paths. Writes the result to `buf` and returns the result length.
size_t zis_path_join_n(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, size_t path1_len,
    const zis_path_char_t *path2, size_t path2_len
);

/// Get the file name component of a path. Writes the result to `buf` and returns the result length.
size_t zis_path_filename(zis_path_char_t *buf, const zis_path_char_t *path);

/// Get the file name without the final extension of a path. Writes the result to `buf` and returns the result length.
size_t zis_path_stem(zis_path_char_t *buf, const zis_path_char_t *path);

/// Get the extension component of a path. Writes the result to `buf` and returns the result length.
size_t zis_path_extension(zis_path_char_t *buf, const zis_path_char_t *path);

/// Get the parent path. Writes the result to `buf` and returns the result length.
size_t zis_path_parent(zis_path_char_t *buf, const zis_path_char_t *path);

/// Replace the extension. Param `new_ext` is optional. Writes the result to `buf` and returns the result length.
size_t zis_path_with_extension(
    zis_path_char_t *buf,
    const zis_path_char_t *path, const zis_path_char_t *new_ext
);

/* ----- filesystem access -------------------------------------------------- */

/// Check whether a path exists.
bool zis_fs_exists(const zis_path_char_t *path);

/// Compose an absolute path. Writes the result to `buf` and returns the result length.
/// On failure, returns 0.
size_t zis_fs_absolute(zis_path_char_t *buf, const zis_path_char_t *path);

#define ZIS_FS_FT_REG  0x01 ///< Is a regular file.
#define ZIS_FS_FT_DIR  0x02 ///< Is a directory.
#define ZIS_FS_FT_LNK  0x04 ///< Is a symbolic link.

/// Get file type. On success, return `ZIS_FS_FT_XXX` values; on failure, return `-1`.
int zis_fs_filetype(const zis_path_char_t *path);

/// Visit files in a directory. Returns -1 on failure.
int zis_fs_iter_dir(
    const zis_path_char_t *dir_path,
    int (*fn)(const zis_path_char_t *file, void *arg), void *fn_arg
);

/// Get home directory of current user. Return NULL if unknown.
const zis_path_char_t *zis_fs_user_home_dir(void);
