#ifndef _WIN32
#    error "not on Windows"
#endif // _WIN32

#include "winutil.h"

#include <assert.h>
#include <locale.h>
#include <stdlib.h>

#include <Windows.h>

static_assert(sizeof(wchar_t) == 2, "");

char *win_wstr_to_utf8(const wchar_t *wstr) {
    int u8str_sz = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (u8str_sz < 0)
        return NULL;
    char *const u8str = malloc(u8str_sz);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, u8str, u8str_sz, NULL, NULL);
    return u8str;
}

char **win_wstrv_to_utf8(const wchar_t **wstrv, size_t len) {
    char **const u8strv = malloc(len * sizeof(char *));
    for (size_t i = 0; i < len; i++) {
        const wchar_t *const wstr = wstrv[i];
        u8strv[i] = wstr ? win_wstr_to_utf8(wstr) : NULL;
    }
    return u8strv;
}

void win_utf8_init(void) {
    // console code pages
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    // UCRT (universal C runtime); Windows 10 version 1803 (10.0.17134.0) required
    setlocale(LC_ALL, ".UTF8");
}

static void enable_term_modes(DWORD std_out_name, DWORD extra_modes) {
    const HANDLE handle = GetStdHandle(std_out_name);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
        return;
    mode |= extra_modes;
    SetConsoleMode(handle, mode);
}

void win_term_init(void) {
    enable_term_modes(STD_OUTPUT_HANDLE, ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    enable_term_modes(STD_ERROR_HANDLE , ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
