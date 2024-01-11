/// Utilities for programs on Windows.

#pragma once

#ifdef _WIN32

#include <stddef.h>
#include <wchar.h> // wchar_t

/// Convert a wide-character string to UTF-8 string.
char *win_wstr_to_utf8(const wchar_t *wstr);

/// Convert a vector of wide-character strings or NULLs to a UTF-8 one.
char **win_wstrv_to_utf8(const wchar_t **wstrv, size_t len);

/// Initialize UTF-8 environment.
void win_utf8_init(void);

/// Initialize terminal (console).
void win_term_init(void);

#endif // _WIN32
