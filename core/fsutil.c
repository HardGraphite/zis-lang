#include "fsutil.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

#if ZIS_FS_POSIX

#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>

#if defined PATH_MAX
static_assert(ZIS_PATH_MAX == PATH_MAX, "");
#elif defined MAXPATHLEN
static_assert(ZIS_PATH_MAX == MAXPATHLEN, "");
#else
#   error "PATH_MAX is not defined"
#endif

#elif ZIS_FS_WINDOWS

#include <Windows.h>

static_assert(ZIS_PATH_MAX == MAX_PATH, "");

#endif

/* ----- internal functions and variables ----------------------------------- */

static bool is_dir_sep(zis_path_char_t ch) {
#if ZIS_FS_POSIX
    return ch == '/';
#elif ZIS_FS_WINDOWS
    return ch == L'\\' || ch == L'/';
#endif
}

static zis_path_char_t *rfind_dir_sep(const zis_path_char_t *path) {
#if ZIS_FS_POSIX
    return strrchr(path, '/');
#elif ZIS_FS_WINDOWS
    zis_path_char_t *const p1 = wcsrchr(path, L'\\');
    if (!p1)
        return wcsrchr(path, L'/');
    zis_path_char_t *const p2 = wcsrchr(p1, L'/');
    if (!p2)
        return p1;
    return p2;
#endif
}

static zis_path_char_t *rfind_ext_dot(const zis_path_char_t *path) {
    zis_path_char_t *const last_dir_sep = rfind_dir_sep(path);
    if (last_dir_sep) {
        if (zis_unlikely(last_dir_sep[1] == 0))
            return NULL;
        path = last_dir_sep + 1;
    }
    zis_path_char_t *last_dot =
#if ZIS_FS_POSIX
    strrchr(path, '.');
#elif ZIS_FS_WINDOWS
    wcsrchr(path, L'.');
#endif
    if (zis_unlikely(!last_dot || last_dot == path))
        return NULL;
    if (zis_unlikely(path[0] == ZIS_PATH_CHR('.') && last_dot == path + 1 && !path[2]))
        return NULL;
    return last_dot;
}

/* ----- public functions: path string -------------------------------------- */

size_t zis_path_len(const zis_path_char_t *path) {
#if ZIS_FS_POSIX
    return strlen(path);
#elif ZIS_FS_WINDOWS
    return wcslen(path);
#endif
}

zis_nodiscard zis_path_char_t *zis_path_alloc(size_t len) {
    const size_t size = (len + 1) * sizeof(zis_path_char_t);
    return zis_mem_alloc(size);
}

zis_path_char_t *zis_path_dup(const zis_path_char_t *path) {
    const size_t size = (zis_path_len(path) + 1) * sizeof(zis_path_char_t);
    zis_path_char_t *s = zis_mem_alloc(size);
    return memcpy(s, path, size);
}

zis_path_char_t *zis_path_dup_n(const zis_path_char_t *path, size_t len) {
    const size_t size = (len + 1) * sizeof(zis_path_char_t);
    zis_path_char_t *s = zis_mem_alloc(size);
    s[len] = 0;
    return memcpy(s, path, size - sizeof(zis_path_char_t));
}

int zis_path_with_temp_path_from_str(
    const char *str,
    int (*fn)(const zis_path_char_t *, void *), void *fn_arg
) {
#if ZIS_FS_POSIX

    return fn(str, fn_arg);

#elif ZIS_FS_WINDOWS

    int path_len = MultiByteToWideChar(CP_UTF8, 0, utf8String, -1, NULL, 0);
    assert(path_len >= 0);
    wchar_t *path = zis_path_alloc(path_len);
    MultiByteToWideChar(CP_UTF8, 0, utf8String, -1, path, path_len);

    const in fn_ret = fn(path, fn_arg);
    zis_mem_free(path);
    return fn_ret;

#endif
}

int zis_path_with_temp_str_from_path(
    const zis_path_char_t *path,
    int (*fn)(const char *, void *), void *fn_arg
) {
#if ZIS_FS_POSIX

    return fn(path, fn_arg);

#elif ZIS_FS_WINDOWS

    int u8str_sz = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    assert(u8str_sz >= 0);
    char *const u8str = zis_mem_alloc(u8str_sz);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, u8str, u8str_sz, NULL, NULL);

    const in fn_ret = fn(u8str, fn_arg);
    zis_mem_free(u8str);
    return fn_ret;

#endif
}

size_t zis_path_copy(zis_path_char_t *dst, const zis_path_char_t *src) {
    const size_t src_len = zis_path_len(src);
    zis_path_copy_n(dst, src, src_len + 1);
    return src_len;
}

void zis_path_copy_n(zis_path_char_t *dst, const zis_path_char_t *src, size_t len) {
    memmove(dst, src, len * sizeof(zis_path_char_t));
}

size_t zis_path_concat(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, const zis_path_char_t *path2
) {
    const size_t path1_len = zis_path_len(path1);
    const size_t path2_len = zis_path_len(path2);
    return zis_path_concat_n(buf, path1, path1_len, path2, path2_len);
}

size_t zis_path_concat_n(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, size_t path1_len,
    const zis_path_char_t *path2, size_t path2_len
) {
    assert(path1_len + path2_len < ZIS_PATH_MAX);
    zis_path_copy_n(buf, path1, path1_len);
    zis_path_copy_n(buf + path1_len, path2, path2_len + 1);
    return path1_len + path2_len;
}

size_t zis_path_join(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, const zis_path_char_t *path2
) {
    const size_t path1_len = zis_path_len(path1);
    const size_t path2_len = zis_path_len(path2);
    return zis_path_join_n(buf, path1, path1_len, path2, path2_len);
}

size_t zis_path_join_n(
    zis_path_char_t *buf,
    const zis_path_char_t *path1, size_t path1_len,
    const zis_path_char_t *path2, size_t path2_len
) {
    if (zis_unlikely(is_dir_sep(path2[0]))) {
        zis_path_copy_n(buf, path2, path2_len);
        return path2_len;
        // FIXME: handle the driver prefix ("C:", "D:", ...) in Windows paths.
    }
    if (zis_unlikely(is_dir_sep(path1[path1_len - 1]))) {
        return zis_path_concat_n(buf, path1, path1_len, path2, path2_len);
    }
    assert(path1_len + path2_len + 1 < ZIS_PATH_MAX);
    zis_path_concat_n(buf, path1, path1_len + 1, path2, path2_len);
    buf[path1_len] = ZIS_PATH_PREFERRED_DIR_SEP;
    return path1_len + path2_len + 1;
}

size_t zis_path_filename(zis_path_char_t *buf, const zis_path_char_t *path) {
    const zis_path_char_t *const last_dir_sep = rfind_dir_sep(path);
    if (zis_unlikely(!last_dir_sep)) {
        return zis_path_copy(buf, path);
    }
    if (zis_unlikely(last_dir_sep[1] == 0)) {
        buf[0] = 0; // empty filename
        return 0;
    }
    const zis_path_char_t *const s = last_dir_sep + 1;
    return zis_path_copy(buf, s);
}

size_t zis_path_stem(zis_path_char_t *buf, const zis_path_char_t *path) {
    const zis_path_char_t *last_dir_sep = rfind_dir_sep(path);
    size_t res_len;
    if (last_dir_sep) {
        if (zis_unlikely(last_dir_sep[1] == 0)) {
            buf[0] = 0; // empty filename
            return 0;
        }
        res_len = zis_path_copy(buf, last_dir_sep + 1);
    } else {
        res_len = zis_path_copy(buf, path);
    }
    zis_path_char_t *const last_dot = rfind_ext_dot(buf);
    if (last_dot) {
        *last_dot = 0;
        res_len = (size_t)(last_dot - buf);
    }
    return res_len;
}

size_t zis_path_extension(zis_path_char_t *buf, const zis_path_char_t *path) {
    const zis_path_char_t *const last_dot = rfind_ext_dot(path);
    if (zis_unlikely(!last_dot)) {
        buf[0] = 0;
        return 0;
    }
    return zis_path_copy(buf, last_dot);
}

size_t zis_path_parent(zis_path_char_t *buf, const zis_path_char_t *path) {
    const zis_path_char_t *last_dir_sep = rfind_dir_sep(path);
    if (zis_unlikely(!last_dir_sep)) {
        return zis_path_join(buf, path, ZIS_PATH_STR(".."));
    }
    if (zis_unlikely(last_dir_sep[1] == 0)) {
        for (; last_dir_sep > path && is_dir_sep(*last_dir_sep); last_dir_sep--);
    }
    const size_t n = (size_t)(last_dir_sep - path);
#if ZIS_FS_WINDOWS
    if (n == 2 && path[1] == L':' && isalpha(path[0])) { // "X:/"
        buf[2] = L'\\', buf[3] = 0;
        return path_buffer;
    }
#endif // ZIS_FS_WINDOWS
    if (zis_unlikely(!n)) {
        buf[0] = ZIS_PATH_PREFERRED_DIR_SEP, buf[1] = 0;
        return 1;
    }
    zis_path_copy_n(buf, path, n);
    buf[n] = 0;
    return n;
}

size_t zis_path_with_extension(
    zis_path_char_t *buf,
    const zis_path_char_t *path, const zis_path_char_t *new_ext
) {
    const zis_path_char_t *last_dot = rfind_ext_dot(path);
    size_t prefix_len = last_dot ? (size_t)(last_dot - path) : zis_path_len(path);
    zis_path_copy_n(buf, path, prefix_len);
    if (!new_ext) {
        buf[prefix_len] = 0;
        return prefix_len;
    }
    size_t ext_len = zis_path_len(new_ext);
    assert(prefix_len + ext_len < ZIS_PATH_MAX);
    zis_path_copy_n(buf + prefix_len, new_ext, ext_len + 1);
    return prefix_len + ext_len;
}

/* ----- public functions: filesystem access -------------------------------- */

bool zis_fs_exists(const zis_path_char_t *path) {
#if ZIS_FS_POSIX
    return access(path, F_OK) == 0;
#elif ZIS_FS_WINDOWS
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
#endif
}

size_t zis_fs_absolute(zis_path_char_t *buf, const zis_path_char_t *path) {
#if ZIS_FS_POSIX
    if (realpath(path, buf))
        return zis_path_len(buf);
    return 0;
#elif ZIS_FS_WINDOWS
    const DWORD n = GetFullPathNameW(path, ZIS_PATH_MAX, buf, NULL);
    assert(n <= ZIS_PATH_MAX);
    return (size_t)n;
#endif
}

int zis_fs_filetype(const zis_path_char_t *path) {
#if ZIS_FS_POSIX

    struct stat file_stat;
    if (zis_unlikely(stat(path, &file_stat)))
        return -1;
    int result = 0;
    if (S_ISREG(file_stat.st_mode))
        result |= ZIS_FS_FT_REG;
    if (S_ISDIR(file_stat.st_mode))
        result |= ZIS_FS_FT_DIR;
    if (S_ISLNK(file_stat.st_mode))
        result |= ZIS_FS_FT_LNK;
    return result;

#elif ZIS_FS_WINDOWS

    const DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return -1;
    int result = 0;
    if (attr & FILE_ATTRIBUTE_NORMAL)
        result |= ZIS_FS_FT_REG;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        result |= ZIS_FS_FT_DIR;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
        result |= ZIS_FS_FT_LNK;
    return result;

#endif
}

int zis_fs_iter_dir(
    const zis_path_char_t *dir_path,
    int (*fn)(const zis_path_char_t *file, void *arg), void *fn_arg
) {
#if ZIS_FS_POSIX

    DIR *dir;
    struct dirent *dir_entry;
    if (!(dir = opendir(dir_path)))
        return -1;

    int ret = 0;
    while ((dir_entry = readdir(dir))) {
        const zis_path_char_t *const name = dir_entry->d_name;
        if (zis_unlikely(name[0] == '.')) {
            const char c = name[1];
            if (zis_unlikely(c == '\0' || (c == '.' && name[2] == '\0')))
                continue;
        }
        if ((ret = fn(name, fn_arg)) != 0)
            break;
    }
    closedir(dir);
    return ret;

#elif ZIS_FS_WINDOWS

    size_t dir_len = zis_path_len(dir_path);
    if (zis_unlikely(dir_len > (MAX_PATH - 3)))
        return -1; // Too long.
    zis_path_char_t *const dir = zis_path_copy_n(path_buffer, dir_path, dir_len);
    dir[dir_len] = L'\\', dir[dir_len + 1] = L'*', dir[dir_len + 2] = 0;

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    if ((hFind = FindFirstFileW(dir, &ffd)) == INVALID_HANDLE_VALUE)
        return -1;
    int ret = 0;
    do {
        const DWORD attr = ffd.dwFileAttributes;
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY || attr & FILE_ATTRIBUTE_NORMAL))
            continue;
        if ((ret = func(arg, ffd.cFileName)) != 0)
            break;
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
    return ret;

#endif
}

const zis_path_char_t *zis_fs_user_home_dir(void) {
#if ZIS_FS_POSIX
    return getenv("HOME");
#elif ZIS_FS_WINDOWS
    return _wgetenv(L"USERPROFILE");
#endif
}
