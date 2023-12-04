/**
 * @file zis.h
 * @brief ZIS public APIs.
 */

#ifndef ZIS_H
#define ZIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
#define ZIS_E_TYPE      (-13)  ///< Type mismatched.
#define ZIS_E_BUF       (-14)  ///< Buffer is not big enough.

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

/**
 * Call a C function within an isolated zis frame.
 *
 * Enter a new frame with `reg_max + 1` registers and call C function `fn`.
 * REG-0 is copied from the previous frame and will be copied back to there.
 *
 * @param z zis instance
 * @param reg_max maximum register index
 * @param fn the function to call
 * @param arg argument to pass to `fn`
 * @return Return the return value from function `fn`. If the stack overflows,
 * `ZIS_E_ARG` will be returned and C function `fn` will not be called.
 */
ZIS_API int zis_native_block(zis_t z, size_t reg_max, int(*fn)(zis_t, void *), void *arg);

/** @} */

/** @defgroup zis-api-values API: build and read values */
/**  @{ */

/**
 * Load `nil` to registers from `reg`-th to (`reg+n-1`)-th.
 *
 * @param z zis instance
 * @param reg register index
 * @param n number of registers to fill with `nil`
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 *
 * @warning If `n` is too big, it is going to be adjusted automatically.
 */
ZIS_API int zis_load_nil(zis_t z, unsigned int reg, size_t n);

/**
 * Check whether a variable is `nil`.
 *
 * @param z zis instance
 * @param reg register index
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (`reg` is not `nil`).
 */
ZIS_API int zis_read_nil(zis_t z, unsigned int reg);

/**
 * Load a `Bool` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val the boolean value
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_load_bool(zis_t z, unsigned int reg, bool val);

/**
 * Read value of a `Bool` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val pointer to a variable where the value will be stored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`).
 */
ZIS_API int zis_read_bool(zis_t z, unsigned int reg, bool *val);

/**
 * Create an `Int` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val the value of the integer
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_int(zis_t z, unsigned int reg, int64_t val);

/**
 * Read value of an `Int` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val pointer to a variable where the value will be stored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`),
 * `ZIS_E_BUF` (`int64_t` is not big enough to hold the value).
 */
ZIS_API int zis_read_int(zis_t z, unsigned int reg, int64_t *val);

/**
 * Create an `Float` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val the value of the floating-point number
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_float(zis_t z, unsigned int reg, double val);

/**
 * Read value of a `Float` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val pointer to a variable where the value will be stored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`).
 */
ZIS_API int zis_read_float(zis_t z, unsigned int reg, double *val);

/**
 * Create an `String` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param str pointer to the UTF-8 string
 * @param sz size in bytes of the string `str`, or `-1` to take `str` as a NUL-terminated string
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_ARG` (illegal UTF-8 string `str`).
 */
ZIS_API int zis_make_string(zis_t z, unsigned int reg, const char *str, size_t sz);

/**
 * Get content of a `String` object as a UTF-8 string.
 *
 * @param z zis instance
 * @param reg register index
 * @param buf pointer to a buffer to store UTF-8 string, or `NULL` to get expected buffer size
 * @param sz pointer to a `size_t` value that tells the buffer size and receives written size
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`),
* `ZIS_E_BUF` (`buf` is not big enough).
 */
ZIS_API int zis_read_string(zis_t z, unsigned int reg, char *buf, size_t *sz);

/** @} */

/** @defgroup zis-api-variables API: access and manipulate variables */
/**  @{ */

/**
 * Copy object between registers (local variables).
 *
 * @param z zis instance
 * @param dst destination register index
 * @param src source register index
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `dst` or `src`).
 */
ZIS_API int zis_move_local(zis_t z, unsigned int dst, unsigned int src);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ZIS_NOEXCEPT

#endif /* ZIS_H */
