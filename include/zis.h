#ifndef ZIS_H
#define ZIS_H

#include <stddef.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#    if ZIS_EXPORT_API
#        define ZIS_API __declspec(dllexport)
#    elif ZIS_IMPORT_API
#        define ZIS_API __declspec(dllimport)
#    endif
#elif (__GNUC__ + 0 >= 4) || defined(__clang__)
#    if ZIS_EXPORT_API
#        define ZIS_API __attribute__((used, visibility("default")))
#    else
#        define ZIS_API
#    endif
#else
#    define ZIS_API
#endif

#if !defined(__cplusplus) /* C */
#    define ZIS_NOEXCEPT
#elif __cplusplus >= 201103L /* >= C++11 */
#    define ZIS_NOEXCEPT noexcept
#else /* < C++11 */
#    define ZIS_NOEXCEPT throw()
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @defgroup zis-api-status API: status code */
/**  @{ */

#define ZIS_OK          0      ///< Succeeded.

#define ZIS_E_EXC       (-1)   ///< Exception raised.

#define ZIS_E_ARG       (-11)  ///< Illegal argument.
#define ZIS_E_IDX       (-12)  ///< Index out of range.

/** @} */

/** @defgroup zis-api-context API: runtime instance */
/**  @{ */

/**
 * Runtime instance.
 */
typedef struct zis_context *zis_t;

/**
 * Create a runtime instance.
 *
 * @return Returns the newly created instance.
 *
 * @warning To avoid a memory leak, the instance must be finalized with `zis_destroy()`.
 */
ZIS_API zis_t zis_create(void) ZIS_NOEXCEPT;

/**
 * Delete a runtime instance.
 *
 * @param z The instance to delete.
 */
ZIS_API void zis_destroy(zis_t z) ZIS_NOEXCEPT;

/** @} */

/** @defgroup zis-api-natives API: native functions, types, and modules */
/**  @{ */

/**
 * Implementation of a native function.
 */
typedef int (*zis_native_func_t)(zis_t);

/**
 * Metadata of a native function.
 */
struct zis_native_func_meta {
    unsigned char  na; ///< Number of arguments.
    unsigned char  va; ///< Whether this is a variadic function (accepts any number of extra arguments).
    unsigned short nl; ///< Number of local variables (excluding arguments).
};

/**
 * Definition of a native function.
 */
struct zis_native_func_def {
    const char                 *name;
    struct zis_native_func_meta meta;
    zis_native_func_t           code;
};

/**
 * Definition of a native type (struct).
 */
struct zis_native_type_def {
    const char                       *name;      ///< Type name.
    size_t                            slots_num; ///< Number of slots in object SLOTS part.
    size_t                            bytes_size;///< Size of object BYTES part.
    const char *const                *slots;     ///< An array of slot names, the length of which must be `slots_num`. Optional.
    const struct zis_native_func_def *methods;   ///< A zero-terminated array of functions that define methods. Optional.
    const struct zis_native_func_def *statics;   ///< Static methods definitions like `methods`. Optional.
};

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ZIS_NOEXCEPT

#endif /* ZIS_H */
