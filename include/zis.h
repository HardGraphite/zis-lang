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
 * @return Return the return value from function `fn`.
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

/**
 * Create values and store them to `REG[reg_begin ...]`.
 *
 * @param z zis instance
 * @param reg_begin first register to store values to
 * @param fmt a NUL-terminated string specifying how to interpret the data
 * @param ... the data
 * @return Number of stored values (zero or positive);
 * `ZIS_E_IDX` (invalid `reg_begin`), `ZIS_E_ARG` (invalid `fmt` or `...`).
 *
 * @details The format string `fmt` consists of the following specifiers:
 * + Equivalent to `zis_load_*` or `zis_make_*` functions.
 *   - `%`: register (local variable); the argument is `unsigned int reg`.
 *   - `n`: `nil` value; no argument.
 *   - `x`: `Bool` value; see `zis_load_bool()`.
 *   - `i`: `Int` value; see `zis_make_int()`.
 *   - `f`: `Float` value; see `zis_make_float()`.
 *   - `s`: `String` value; see `zis_make_string()`.
 * + A collection of other values, inside which other specifiers can be used.
 *   However, **nested collections are not allowed**.
 *   - `(` [`<spec>`...] `)`: `Tuple` value.
 *   - `[` [`*`] [`<spec>`...] `]`: `Array` value.
 * + Others.
 *   - `-`: skip one value; no argument.
 *   - `*`: reserve storage for collection types; the argument is `size_t n`.
 *
 * @warning No error will be reported if write pointer reaches the end of
 * current frame during interpretation.
 *
 * @warning REG-0 may be used by this function. The value in it can be modified.
 */
ZIS_API int zis_make_values(zis_t z, unsigned int reg_begin, const char *fmt, ...);

/**
 * Read values from `REG[reg_begin ...]`.
 *
 * @param z zis instance
 * @param reg_begin first register to read values from
 * @param fmt a NUL-terminated string specifying how to interpret the data
 * @param ... the data
 * @return Number of read values (zero or positive);  `ZIS_E_IDX` (invalid `reg_begin`),
 * `ZIS_E_ARG` (invalid `fmt` or `...`), `ZIS_E_TYPE` (type mismatched).
 *
 * @details The format string `fmt` consists of the following specifiers:
 * + Equivalent to `zis_load_*` or `zis_read_*` functions.
 *   - `%`: register (local variable); the argument is `unsigned int reg`.
 *   - `n`: check if it is a `nil`; no argument.
 *   - `x`: `Bool` value; see `zis_read_bool()`.
 *   - `i`: `Int` value; see `zis_read_int()`.
 *   - `f`: `Float` value; see `zis_read_float()`.
 *   - `s`: `String` value; see `zis_read_string()`.
 * + A collection of other values, inside which other specifiers can be used.
 *   However, **nested collections are not allowed**.
 *   - `(` [`*`] [`<spec>`...] `)`: read `Tuple`.
 *   - `[` [`*`] [`<spec>`...] `]`: read `Array`.
 * + Others.
 *   - `-`: skip one value; no argument.
 *   - `*`: get length of a collection type value; the argument is `size_t *n`.
 *   - `?`: ignore type errors if the actual type is `Nil`; no argument.
 *
 * @warning REG-0 may be used by this function. The value in it can be modified.
 */
ZIS_API int zis_read_values(zis_t z, unsigned int reg_begin, const char *fmt, ...);

/** @} */

/** @defgroup zis-api-variables API: access and manipulate variables */
/**  @{ */

/**
 * Copy object between registers (local variables).
 *
 * `REG[dst] <- REG[src]`
 *
 * @param z zis instance
 * @param dst destination register index
 * @param src source register index
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `dst` or `src`).
 */
ZIS_API int zis_move_local(zis_t z, unsigned int dst, unsigned int src);

/**
 * Get element from an object.
 *
 * `REG[reg_elem] <- ( REG[reg_obj] )[ REG[reg_key] ]`
 *
 * @param z zis instance
 * @param reg_obj register where the object is
 * @param reg_key register where the key is
 * @param reg_val register to load the element to
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index); `ZIS_E_ARG` (key does not exist);
 * `ZIS_E_TYPE` (`reg_obj` does not allow elements).
 */
ZIS_API int zis_load_element(zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val);

/**
 * Set object element.
 *
 * `( REG[reg_obj] )[ REG[reg_key] ] <- REG[reg_elem]`
 *
 * @param z zis instance
 * @param reg_obj register where the object is
 * @param reg_key register where the key is
 * @param reg_val register where the new value of the element is
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index); `ZIS_E_ARG` (key does not exist);
 * `ZIS_E_TYPE` (`reg_obj` does not allow elements).
 */
ZIS_API int zis_store_element(zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val);

/**
 * Insert value to an object as a new element.
 *
 * `( REG[reg_obj] )[ REG[reg_key] ] <- REG[reg_elem]`
 *
 * @param z zis instance
 * @param reg_obj register where the object is
 * @param reg_key register where the key is
 * @param reg_val register where the value is
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index); `ZIS_E_ARG` (key not valid);
 * `ZIS_E_TYPE` (`reg_obj` does not allow elements).
 */
ZIS_API int zis_insert_element(zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val);

/**
 * Remove an element from an object.
 *
 * `delete ( REG[reg_obj] )[ REG[reg_key] ]`
 *
 * @param z zis instance
 * @param reg_obj register where the object is
 * @param reg_key register where the key is
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index); `ZIS_E_ARG` (key not valid);
 * `ZIS_E_TYPE` (`reg_obj` does not allow elements).
 */
ZIS_API int zis_remove_element(zis_t z, unsigned int reg_obj, unsigned int reg_key);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ZIS_NOEXCEPT

#endif /* ZIS_H */
