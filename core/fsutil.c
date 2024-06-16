#include "fsutil.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

#if ZIS_FS_POSIX

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
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

int zis_path_compare(const zis_path_char_t *path1, const zis_path_char_t *path2) {
#if ZIS_FS_POSIX
    return strcmp(path1, path2);
#elif ZIS_FS_WINDOWS
    return _wcsicmp(path1, path2);
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

    int path_len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    assert(path_len >= 0);
    wchar_t *path = zis_path_alloc(path_len);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, path, path_len);

    const int fn_ret = fn(path, fn_arg);
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

    int u8str_sz = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    assert(u8str_sz >= 0);
    char *const u8str = zis_mem_alloc(u8str_sz);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, u8str, u8str_sz, NULL, NULL);

    const int fn_ret = fn(u8str, fn_arg);
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

static size_t _zis_path_stem_len(const zis_path_char_t *path) {
    // Adapted from `zis_path_stem()`.

    const zis_path_char_t *last_dir_sep = rfind_dir_sep(path);
    if (last_dir_sep) {
        if (zis_unlikely(last_dir_sep[1] == 0))
            return 0;
        path = last_dir_sep + 1;
    }
    const zis_path_char_t *const last_dot = rfind_ext_dot(path);
    if (last_dot)
        return (size_t)(last_dot - path);
    return zis_path_len(path);
}

size_t zis_path_stem(zis_path_char_t *buf, const zis_path_char_t *path) {
    if (!buf)
        return _zis_path_stem_len(path);

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
        if (buf)
            buf[0] = 0;
        return 0;
    }
    return buf ? zis_path_copy(buf, last_dot) : zis_path_len(last_dot);
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
        return 3;
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

enum zis_fs_filetype zis_fs_filetype(const zis_path_char_t *path) {
#if ZIS_FS_POSIX

    struct stat file_stat;
    if (zis_unlikely(stat(path, &file_stat)))
        return ZIS_FS_FT_ERROR;
    if (S_ISREG(file_stat.st_mode))
        return ZIS_FS_FT_REG;
    if (S_ISDIR(file_stat.st_mode))
        return ZIS_FS_FT_DIR;
    if (S_ISLNK(file_stat.st_mode))
        return ZIS_FS_FT_LNK;
    return ZIS_FS_FT_OTHER;

#elif ZIS_FS_WINDOWS

    const DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return ZIS_FS_FT_ERROR;
    if (attr == FILE_ATTRIBUTE_NORMAL)
        return ZIS_FS_FT_REG;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return  ZIS_FS_FT_DIR;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
        return  ZIS_FS_FT_LNK;
    return ZIS_FS_FT_REG;

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

    zis_path_char_t *const path_buffer = zis_path_alloc(ZIS_PATH_MAX);
    zis_path_concat(path_buffer, dir_path, L"\\*");

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    if ((hFind = FindFirstFileW(path_buffer, &ffd)) == INVALID_HANDLE_VALUE) {
        zis_mem_free(path_buffer);
        return -1;
    }
    int ret = 0;
    do {
        const DWORD attr = ffd.dwFileAttributes;
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY || attr & FILE_ATTRIBUTE_NORMAL))
            continue;
        if ((ret = fn(fn_arg, ffd.cFileName)) != 0)
            break;
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);

    zis_mem_free(path_buffer);
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

/* ----- file I/O ----------------------------------------------------------- */

zis_nodiscard zis_dl_handle_t zis_dl_open(const zis_path_char_t *file) {
#if ZIS_FS_POSIX
    static_assert(sizeof(void *) == sizeof(zis_dl_handle_t), "");
    return dlopen(file, RTLD_LAZY);
#elif ZIS_FS_WINDOWS
    static_assert(sizeof(HMODULE) == sizeof(zis_dl_handle_t), "");
    return LoadLibraryW(file);
#endif
}

void zis_dl_close(zis_dl_handle_t lib) {
#if ZIS_FS_POSIX
    dlclose(lib);
#elif ZIS_FS_WINDOWS
    FreeLibrary(lib);
#endif
}

void *zis_dl_get(zis_dl_handle_t lib, const char *name) {
#if ZIS_FS_POSIX
    return dlsym(lib, name);
#elif ZIS_FS_WINDOWS
    return (void *)GetProcAddress(lib, name);
#endif
}

zis_nodiscard zis_file_handle_t zis_file_open(const zis_path_char_t *path, int mode) {
#if ZIS_FS_POSIX

    int o_mode, create_mode = 0;
    if (mode & ZIS_FILE_MODE_WR) {
        if (mode & ZIS_FILE_MODE_RD)
            o_mode = O_RDWR;
        else if (mode & ZIS_FILE_MODE_APP)
            o_mode = O_WRONLY;
        else
            o_mode = O_WRONLY | O_CREAT, create_mode = S_IRUSR | S_IWUSR;
    } else {
        o_mode = O_RDONLY;
    }
    int fd = open(path, o_mode, create_mode);
    return fd == -1 ? NULL : (void *)(intptr_t)fd;

#elif ZIS_FS_WINDOWS

    DWORD access = 0;
    if (mode & ZIS_FILE_MODE_WR)
        access |= GENERIC_WRITE;
    if (mode & ZIS_FILE_MODE_RD)
        access |= GENERIC_READ;
    HANDLE h = CreateFileW(
        path, //lpFileName
        access, //dwDesiredAccess
        FILE_SHARE_READ | FILE_SHARE_WRITE, //dwShareMode
        NULL, //lpSecurityAttributes
        (mode & ZIS_FILE_MODE_APP) ? OPEN_EXISTING : OPEN_ALWAYS, //dwCreationDisposition
        FILE_ATTRIBUTE_NORMAL, //dwFlagsAndAttributes
        NULL //hTemplateFile
    );
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    if (mode == 2)
        SetFilePointer(h, 0, NULL, FILE_END);
    return (void *)h;

#endif
}

zis_file_handle_t zis_file_stdio(int file_std_xxx) {
    if (
        file_std_xxx != ZIS_FILE_STDIN &&
        file_std_xxx != ZIS_FILE_STDOUT &&
        file_std_xxx != ZIS_FILE_STDERR
    ) {
        return NULL;
    }

#if ZIS_FS_POSIX

    static_assert(ZIS_FILE_STDIN  ==  STDIN_FILENO, "");
    static_assert(ZIS_FILE_STDOUT == STDOUT_FILENO, "");
    static_assert(ZIS_FILE_STDERR == STDERR_FILENO, "");

    return (void *)(intptr_t)file_std_xxx;

#elif ZIS_FS_WINDOWS

    static_assert((DWORD)(-ZIS_FILE_STDIN  - 10) ==  STD_INPUT_HANDLE, "");
    static_assert((DWORD)(-ZIS_FILE_STDOUT - 10) == STD_OUTPUT_HANDLE, "");
    static_assert((DWORD)(-ZIS_FILE_STDERR - 10) ==  STD_ERROR_HANDLE, "");

    HANDLE h = GetStdHandle((DWORD)(-file_std_xxx - 10));
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    return (void *)h;

#endif
}

void zis_file_close(zis_file_handle_t f) {
#if ZIS_FS_POSIX

    const int fd = (int)(intptr_t)f;
    close(fd);

#elif ZIS_FS_WINDOWS

    HANDLE h = (HANDLE)f;
    CloseHandle(h);

#endif
}

intptr_t zis_file_seek(zis_file_handle_t f, intptr_t offset, int whence) {
#if ZIS_FS_POSIX

    const int fd = (int)(intptr_t)f;
    return (intptr_t)lseek(fd, (off_t)offset, whence);

#elif ZIS_FS_WINDOWS

    HANDLE h = (HANDLE)f;
    LONG pos = 0;
    static_assert(FILE_BEGIN == SEEK_SET, "");
    static_assert(FILE_CURRENT == SEEK_CUR, "");
    static_assert(FILE_END == SEEK_END, "");
    const BOOL ok = SetFilePointer(
        h, // hFile
        (LONG)offset, // liDistanceToMove
        &pos, // lpNewFilePointer
        whence  // dwMoveMethod
    );
    if (!ok)
        return -1;
    return (intptr_t)pos;

#endif
}

size_t zis_file_read(zis_file_handle_t f, char *restrict buffer, size_t size) {
    if (zis_unlikely(!size))
        return 0;

#if ZIS_FS_POSIX

    const int fd = (int)(intptr_t)f;
    if (zis_unlikely(size > SSIZE_MAX))
        size = SSIZE_MAX;
    const ssize_t n = read(fd, buffer, (ssize_t)size);
    if (n == -1)
        return (size_t)-1;
    return (size_t)n;

#elif ZIS_FS_WINDOWS

    HANDLE h = (HANDLE)f;
    DWORD read_n;
    const BOOL ok = ReadFile(
        h, // hFile
        buffer, // lpBuffer
        (DWORD)size, // nNumberOfBytesToRead
        &read_n, // lpNumberOfBytesRead
        NULL // lpOverlapped
    );
    if (!ok)
        return (size_t)-1;
    return (size_t)read_n;

#endif
}

int zis_file_write(zis_file_handle_t f, const char *restrict data, size_t size) {
    if (zis_unlikely(!size))
        return 0;

#if ZIS_FS_POSIX

    const int fd = (int)(intptr_t)f;
    while (size) {
        ssize_t n = zis_unlikely(size > SSIZE_MAX) ? SSIZE_MAX : (ssize_t)size;
        n = write(fd, data, n);
        if (zis_unlikely(n == -1))
            return -1;
        assert(n >= 0 && size >= (size_t)n);
        size -= (size_t)n;
        data += n;
    }
    return 0;

#elif ZIS_FS_WINDOWS

    HANDLE h = (HANDLE)f;
    const DWORD dword_max = (DWORD)-1;
    while (size) {
        DWORD n = zis_unlikely(size > dword_max) ? dword_max : (DWORD)size;
        const BOOL ok = WriteFile(
            h, // hFile
            data, // lpBuffer
            n, // nNumberOfBytesToWrite
            &n, // lpNumberOfBytesWritten
            NULL // lpOverlapped
        );
        if (!ok)
            return -1;
        assert(size >= (size_t)n);
        size -= (size_t)n;
        data += (size_t)n;
    }
    return 0;

#endif
}
