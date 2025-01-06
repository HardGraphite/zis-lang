/// Platform information macros.

#pragma once

#if defined(_WIN32)
#    define ZIS_SYSTEM_NAME "Windows"
#    define ZIS_SYSTEM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
#    define ZIS_SYSTEM_NAME "Apple"
#    define ZIS_SYSTEM_APPLE 1
#    define ZIS_SYSTEM_POSIX 1
#elif defined(__ANDROID__)
#    define ZIS_SYSTEM_NAME "Android"
#    define ZIS_SYSTEM_ANDROID 1
#    define ZIS_SYSTEM_LINUX 1
#    define ZIS_SYSTEM_POSIX 1
#elif defined(__linux__)
#    define ZIS_SYSTEM_NAME "Linux"
#    define ZIS_SYSTEM_LINUX 1
#    define ZIS_SYSTEM_POSIX 1
#elif defined(__FreeBSD__) || defined(__FreeBSD)
#    define ZIS_SYSTEM_NAME "FreeBSD"
#    define ZIS_SYSTEM_FREEBSD 1
#    define ZIS_SYSTEM_POSIX 1
#elif defined(__unix__) || defined(__unix)
#    define ZIS_SYSTEM_NAME "UNIX"
#    define ZIS_SYSTEM_UNIX 1
#    define ZIS_SYSTEM_POSIX 1
#else
#    define ZIS_SYSTEM_NAME ""
#endif

#if defined(_M_IA64)
#    define ZIS_ARCH_NAME "ia64"
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) ||          \
        defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
#    define ZIS_ARCH_NAME "x86_64"
#    define ZIS_ARCH_AMD64 1
#elif defined(__x86) || defined(__x86__) || defined(_M_IX86)
#    define ZIS_ARCH_NAME "x86"
#    define ZIS_ARCH_X86 1
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#    define ZIS_ARCH_NAME "arm64"
#    define ZIS_ARCH_ARM64 1
#elif defined(__arm__) || defined(__arm) || defined(_M_ARM)
#    define ZIS_ARCH_NAME "arm"
#    define ZIS_ARCH_ARM 1
#elif defined(__mips__) || defined(_M_MIPS)
#    define ZIS_ARCH_NAME "mips"
#    define ZIS_ARCH_MIPS 1
#else
#    define ZIS_ARCH_NAME ""
#endif

#if ZIS_ARCH_AMD64 || ZIS_ARCH_ARM64
#    define ZIS_WORDSIZE 64
#elif ZIS_ARCH_X86 || ZIS_ARCH_ARM
#    define ZIS_WORDSIZE 32
#elif defined(__has_include) && __has_include(<bits/wordsize.h>)
#    include <bits/wordsize.h>
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
