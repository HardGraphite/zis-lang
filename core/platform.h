/// Platform information macros.

#pragma once

#if defined(_WIN32)
#    define ZIS_SYSTEM_NAME "Windows"
#    define ZIS_SYSTEM_WINDOWS 1
#elif defined(__linux__)
#    define ZIS_SYSTEM_NAME "Linux"
#    define ZIS_SYSTEM_POSIX 1
#elif defined(__APPLE__) && defined(__MACH__)
#    define ZIS_SYSTEM_NAME "Apple"
#    define ZIS_SYSTEM_POSIX 1
#elif defined(__FreeBSD__) || defined(__FreeBSD)
#    define ZIS_SYSTEM_NAME "FreeBSD"
#    define ZIS_SYSTEM_POSIX 1
#else
#    define ZIS_SYSTEM_NAME "?"
#endif

#if defined(_M_IA64)
#    define ZIS_ARCH_NAME "ia64"
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) ||          \
        defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
#    define ZIS_ARCH_NAME "x86_64"
#elif defined(__x86) || defined(__x86__) || defined(_M_IX86)
#    define ZIS_ARCH_NAME "x86"
#elif defined(_M_ARM64)
#    define ZIS_ARCH_NAME "arm64"
#elif defined(_M_ARM)
#    define ZIS_ARCH_NAME "arm"
#elif defined(_M_MIPS)
#    define ZIS_ARCH_NAME "mips"
#else
#    define ZIS_ARCH_NAME "?"
#endif

#if defined(__has_include) && __has_include(<bits/wordsize.h>)
#    include <bits/wordsize.h>
#    if !(__WORDSIZE == 32 || __WORDSIZE == 64)
#        error "unexpected __WORDSIZE value"
#    endif
#    define ZIS_WORDSIZE __WORDSIZE
#elif defined(_WIN32)
#    ifdef _WIN64
#        define ZIS_WORDSIZE 64
#    else
#        define ZIS_WORDSIZE 32
#    endif
#else
#    include <stdint.h>
#    if SIZE_MAX == UINT64_MAX
#        define ZIS_WORDSIZE 64
#    elif SIZE_MAX == UINT32_MAX
#        define ZIS_WORDSIZE 32
#    else
#        error "unexpected SIZE_MAX value"
#    endif
#endif
