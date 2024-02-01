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
#    else
#        define ZIS_API
#    endif
#    define ZIS_NATIVE_MODULE__EXPORT __declspec(dllexport)
#elif (__GNUC__ + 0 >= 4) || defined(__clang__)
#    if ZIS_EXPORT_API
#        define ZIS_API __attribute__((visibility("default")))
#    else
#        define ZIS_API
#    endif
#    define ZIS_NATIVE_MODULE__EXPORT __attribute__((used, visibility("default")))
#else
#    define ZIS_API
#    define ZIS_NATIVE_MODULE__EXPORT
#endif

#if !defined(__cplusplus) /* C */
#    define ZIS_NOEXCEPT
#elif __cplusplus >= 201103L /* >= C++11 */
#    define ZIS_NOEXCEPT noexcept
#else /* < C++11 */
#    define ZIS_NOEXCEPT throw()
#endif /* __cplusplus */

#ifdef __cplusplus /* C++ */
#    define ZIS_NATIVE_MODULE__EXTERN_C extern "C"
#else /* C */
#    define ZIS_NATIVE_MODULE__EXTERN_C
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @defgroup zis-api-general API: general */
/** @{ */

/** @name Status code */
/** @{ */

#define ZIS_OK          0      ///< Succeeded.

#define ZIS_THR         (-1)   ///< An object (maybe an exception) was thrown.

#define ZIS_E_ARG       (-11)  ///< Illegal argument.
#define ZIS_E_IDX       (-12)  ///< Index out of range.
#define ZIS_E_TYPE      (-13)  ///< Type mismatched.
#define ZIS_E_BUF       (-14)  ///< Buffer is not big enough.

/** @} */

/**
 * Version number: { major, minor, patch }.
 */
ZIS_API extern const uint_least16_t zis_version[3];

/** @} */

/** @defgroup zis-api-context API: runtime instance */
/** @{ */

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

/** @name Panic cause */
/** @{ */
#define ZIS_PANIC_OOM   1  ///< Panic cause: out of memory (object memory)
#define ZIS_PANIC_SOV   2  ///< Panic cause: stack overflow (runtime callstack)
#define ZIS_PANIC_ILL   3  ///< Panic cause: illegal bytecode
/** @} */

/**
 * Panic handler function type. See `zis_at_panic()`.
 *
 * The first parameter is a zis instance; the second parameter is the cause
 * of panic (one of the `ZIS_PANIC_*` macros).
 */
typedef void (*zis_panic_handler_t)(zis_t, int) ZIS_NOEXCEPT;

/**
 * Install a panic handler.
 *
 * @param z zis instance
 * @param h the panic handler function, or `NULL` to use the default handler
 * @return Returns the previous installed handler or `NULL`.
 */
ZIS_API zis_panic_handler_t zis_at_panic(zis_t z, zis_panic_handler_t h) ZIS_NOEXCEPT;

/** @} */

/** @defgroup zis-api-natives API: native functions, types, and modules */
/** @{ */

/**
 * Implementation of a native function.
 */
typedef int (*zis_native_func_t)(zis_t) ZIS_NOEXCEPT;

/**
 * Metadata of a native function.
 */
struct zis_native_func_meta {
    uint8_t  na; ///< Number of arguments (excluding optional ones).
    uint8_t  no; ///< Number of optional arguments. Or `-1` to accept a `Tuple` holding the rest arguments (variadic).
    uint16_t nl; ///< Number of local variables (excluding REG-0 and arguments).
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
    const char *const                *fields;    ///< An array of field names (or NULL), the length of which must be `slots_num`. Optional.
    const struct zis_native_func_def *methods;   ///< A zero-terminated array of functions that define methods. Optional.
    const struct zis_native_func_def *statics;   ///< Static methods definitions like `methods`, but those without a name are ignored. Optional.
};

/**
 * Definition of a native module.
 *
 * When a module is created based on such a definition, the functions and types
 * are created and stored as module variables (global variables), excepting
 * those without names (`.name = NULL`).
 * If the first function definition does not have a name, it is the module initializer
 * and will be called automatically after the module created.
 */
struct zis_native_module_def {
    const char                       *name;      ///< Module name.
    const struct zis_native_func_def *functions; ///< A zero-terminated array of functions. Optional.
    const struct zis_native_type_def *types;     ///< A zero-terminated array of types. Optional.
};

/**
 * A macro to define a global variable that exports a native module from C code.
 *
 * @see ZIS_NATIVE_MODULE__VAR
 */
#define ZIS_NATIVE_MODULE(MODULE_NAME)  \
    ZIS_NATIVE_MODULE__EXTERN_C ZIS_NATIVE_MODULE__EXPORT \
    const struct zis_native_module_def ZIS_NATIVE_MODULE__VAR(MODULE_NAME)

/**
 * A macro to generate the name of the module-def variable.
 *
 * To export a module from C, a symbol named `__zis__mod_<MODULE_NAME>` shall be defined,
 * which is of the type `struct zis_native_module_def`. The symbol must be visible
 * from outside the compiled object.
 */
#define ZIS_NATIVE_MODULE__VAR(MODULE_NAME)  __zis__mod_ ## MODULE_NAME

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
ZIS_API int zis_native_block(zis_t z, size_t reg_max, int(*fn)(zis_t, void *), void *arg) ZIS_NOEXCEPT;

/** @} */

/** @defgroup zis-api-values API: build and read values */
/** @{ */

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
ZIS_API int zis_load_nil(zis_t z, unsigned int reg, unsigned int n) ZIS_NOEXCEPT;

/**
 * Check whether a variable is `nil`.
 *
 * @param z zis instance
 * @param reg register index
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (`reg` is not `nil`).
 */
ZIS_API int zis_read_nil(zis_t z, unsigned int reg) ZIS_NOEXCEPT;

/**
 * Load a `Bool` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val the boolean value
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_load_bool(zis_t z, unsigned int reg, bool val) ZIS_NOEXCEPT;

/**
 * Read value of a `Bool` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val pointer to a variable where the value will be stored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`).
 */
ZIS_API int zis_read_bool(zis_t z, unsigned int reg, bool *val) ZIS_NOEXCEPT;

/**
 * Create an `Int` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val the value of the integer
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_int(zis_t z, unsigned int reg, int64_t val) ZIS_NOEXCEPT;

/**
 * Read value of an `Int` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val pointer to a variable where the value will be stored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`),
 * `ZIS_E_BUF` (`int64_t` is not big enough to hold the value).
 */
ZIS_API int zis_read_int(zis_t z, unsigned int reg, int64_t *val) ZIS_NOEXCEPT;

/**
 * Create an `Int` object from string.
 *
 * @param z zis instance
 * @param reg register index
 * @param str pointer to the integer string (prefix "-" is allowed)
 * @param str_sz size in bytes of the string `str`, or `-1` to take `str` as a NUL-terminated string
 * @param base integer string base; the absolute value of which must be in the range `[2,36]`
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_ARG` (illegal `str`).
 */
ZIS_API int zis_make_int_s(zis_t z, unsigned int reg, const char *str, size_t str_sz, int base) ZIS_NOEXCEPT;

/**
 * Represent an `Int` object as string.
 *
 * @param z zis instance
 * @param reg register index
 * @param buf pointer to a buffer to store UTF-8 string, or `NULL` to get expected buffer size
 * @param buf_sz pointer to a `size_t` value that tells the buffer size and receives written size
 * @param base integer string base, the absolute value of which must be in the range `[2,36]`; negative for uppercase letters
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`),
 * `ZIS_E_BUF` (`buf` is not big enough).
 */
ZIS_API int zis_read_int_s(zis_t z, unsigned int reg, char *buf, size_t *buf_sz, int base) ZIS_NOEXCEPT;

/**
 * Create an `Float` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val the value of the floating-point number
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_float(zis_t z, unsigned int reg, double val) ZIS_NOEXCEPT;

/**
 * Read value of a `Float` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param val pointer to a variable where the value will be stored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`).
 */
ZIS_API int zis_read_float(zis_t z, unsigned int reg, double *val) ZIS_NOEXCEPT;

/**
 * Create a `String` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param str pointer to the UTF-8 string
 * @param sz size in bytes of the string `str`, or `-1` to take `str` as a NUL-terminated string
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_ARG` (illegal UTF-8 string `str`).
 */
ZIS_API int zis_make_string(zis_t z, unsigned int reg, const char *str, size_t sz) ZIS_NOEXCEPT;

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
ZIS_API int zis_read_string(zis_t z, unsigned int reg, char *buf, size_t *sz) ZIS_NOEXCEPT;

/**
 * Create or retrieve a `Symbol` object from a string.
 *
 * @param z zis instance
 * @param reg register index
 * @param str pointer to a UTF-8 string
 * @param sz size in bytes of the string `str`, or `-1` to take `str` as a NUL-terminated string
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_symbol(zis_t z, unsigned int reg, const char *str, size_t sz) ZIS_NOEXCEPT;

/**
 * Get string representation of a `Symbol` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param buf pointer to a buffer to store UTF-8 string, or `NULL` to get expected buffer size
 * @param sz pointer to a `size_t` value that tells the buffer size and receives written size
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`),
 * `ZIS_E_BUF` (`buf` is not big enough).
 */
ZIS_API int zis_read_symbol(zis_t z, unsigned int reg, char *buf, size_t *sz) ZIS_NOEXCEPT;

/**
 * Create a `Bytes` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param data pointer to the bytes data
 * @param sz size of the data
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_bytes(zis_t z, unsigned int reg, const void *data, size_t sz) ZIS_NOEXCEPT;

/**
 * Get the data in a `Bytes` object.
 *
 * @param z zis instance
 * @param reg register index
 * @param buf pointer to a buffer to receive the data, or `NULL` to get expected buffer size
 * @param sz pointer to a `size_t` value that tells the buffer size and receives written size
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_TYPE` (wrong type of `reg`),
 * `ZIS_E_BUF` (`buf` is not big enough).
 */
ZIS_API int zis_read_bytes(zis_t z, unsigned int reg, void *buf, size_t *sz) ZIS_NOEXCEPT;

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
 *   - `y`: `Symbol` value; see `zis_make_symbol()`.
 * + A collection of other values, inside which other specifiers can be used.
 *   However, **nested collections are not allowed**.
 *   - `(` [`<spec>`...] `)`: `Tuple` value.
 *   - `[` [`*`] [`<spec>`...] `]`: `Array` value.
 *   - `{` [`*`] [`<key_spec><val_spec>`...] `}`: `Map` value.
 * + Others.
 *   - `-`: skip one value; no argument.
 *   - `*`: reserve storage for collection types; the argument is `size_t n`.
 *
 * @warning No error will be reported if write pointer reaches the end of
 * current frame during interpretation.
 *
 * @warning REG-0 may be used by this function. The value in it can be modified.
 */
ZIS_API int zis_make_values(zis_t z, unsigned int reg_begin, const char *fmt, ...) ZIS_NOEXCEPT;

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
 *   - `y`: `Symbol` value; see `zis_read_symbol()`.
 * + A collection of other values, inside which other specifiers can be used.
 *   However, **nested collections are not allowed**.
 *   - `(` [`*`] [`<spec>`...] `)`: read `Tuple`.
 *   - `[` [`*`] [`<spec>`...] `]`: read `Array`.
 *   - `{` [`*`] `}`: read `Map` elements.
 * + Others.
 *   - `-`: skip one value; no argument.
 *   - `*`: get length of a collection type value; the argument is `size_t *n`.
 *   - `?`: ignore type errors if the actual type is `Nil`; no argument.
 *
 * @warning REG-0 may be used by this function. The value in it can be modified.
 */
ZIS_API int zis_read_values(zis_t z, unsigned int reg_begin, const char *fmt, ...) ZIS_NOEXCEPT;

#ifdef __GNUC__
__attribute__((format(__printf__, 5, 6)))
#endif /* __GNUC__ */
/**
 * Create an `Exception` object.
 *
 * @param z zis instance
 * @param reg register index to store the result
 * @param type exception type as symbol; optional
 * @param reg_data register index where the exception data is; or `-1` for `nil`
 * @param msg_fmt message format string; optional
 * @param ... message data to be formatted according to parameter `msg_fmt`
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg` or `reg_data`).
 */
ZIS_API int zis_make_exception(
    zis_t z, unsigned int reg,
    const char *type, unsigned int reg_data, const char *msg_fmt, ...
    ) ZIS_NOEXCEPT;

#define ZIS_RDE_TEST     0x00 ///< `zis_read_exception()`: do nothing.
#define ZIS_RDE_TYPE     0x01 ///< `zis_read_exception()`: get the `type` field.
#define ZIS_RDE_DATA     0x02 ///< `zis_read_exception()`: get the `data` field.
#define ZIS_RDE_WHAT     0x03 ///< `zis_read_exception()`: get the `what` field.
#define ZIS_RDE_DUMP     0x04 ///< `zis_read_exception()`: print this exception.

/**
 * Read contents of an `Exception` object.
 *
 * @param z zis instance
 * @param reg register index where the exception is
 * @param flag `ZIS_RDE_*`
 * @param reg_out register to store the result
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (invalid `flag`),
 * `ZIS_E_TYPE` (wrong type of `reg`).
 */
ZIS_API int zis_read_exception(zis_t z, unsigned int reg, int flag, unsigned int reg_out) ZIS_NOEXCEPT;

/** @name zis_make_stream() flags */
/** @{ */

#define ZIS_IOS_FILE    0x01 ///< `zis_make_stream()` type: file stream.
#define ZIS_IOS_TEXT    0x02 ///< `zis_make_stream()` type: read-only string stream.

#define ZIS_IOS_RDONLY  0x10 ///< `zis_make_stream()` `ZIS_IOS_FILE` mode: read-only.
#define ZIS_IOS_WRONLY  0x20 ///< `zis_make_stream()` `ZIS_IOS_FILE` mode: write-only.
#define ZIS_IOS_WINEOL  0x40 ///< `zis_make_stream()` `ZIS_IOS_FILE` mode: use Windows style of end-of-line (CRLF).

#define ZIS_IOS_STATIC  0x80 ///< `zis_make_stream()` `ZIS_IOS_TEXT` mode: string is static (infinite lifetime).

/** @} */

/**
 * Create a stream object.
 *
 * @param z zis instance
 * @param reg register index
 * @param flags `ZIS_STREAM_*` values
 * @param ... data used to open a stream (see @@details)
 * @return `ZIS_OK`, `ZIS_THR`; `ZIS_E_IDX` (invalid `reg`), `ZIS_E_ARG` (illegal `flags` or `...`).
 *
 * @details
 * To open a file:
 * ```c
 * const char *file_path = ...; // path to the file
 * const char *encoding  = ...; // text encoding (empty for UTF-8), or NULL to open as a binary file
 * const int other_flags = ...;
 * int status = zis_make_stream(z, reg, ZIS_IOS_FILE | other_flags, file_path, encoding);
 * ```
 * To open a string stream:
 * ```c
 * const char *string = "..."; // the string
 * const size_t string_size = strlen(string); // string size, or -1
 * int status = zis_make_stream(z, reg, ZIS_IOS_TEXT | ZIS_IOS_STATIC, string, string_size);
 * ```
 * To open a string stream from a `String` object:
 * ```c
 * const unsigned int reg_str_obj = ...; // the register where the string object is
 * int status = zis_make_stream(z, reg, ZIS_IOS_TEXT, NULL, reg_str_obj);
 * ```
 */
ZIS_API int zis_make_stream(zis_t z, unsigned int reg, int flags, ...);

/** @} */

/** @defgroup zis-api-code API: code (functions and modules) */
/** @{ */

/**
 * Create a function.
 *
 * @param z zis instance
 * @param reg register index
 * @param def native function definition; field `name` is ignored
 * @param reg_module index of the register where the module is; or `-1` to ignore.
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg` or `reg_module`), `ZIS_E_ARG` (illegal `def`).
 */
ZIS_API int zis_make_function(
    zis_t z, unsigned int reg,
    const struct zis_native_func_def *def, unsigned int reg_module
) ZIS_NOEXCEPT;

/**
 * Create a type.
 *
 * @param z zis instance
 * @param reg register index
 * @param def native function definition; field `name` is ignored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_type(
    zis_t z, unsigned int reg,
    const struct zis_native_type_def *def
);

/**
 * Create a module.
 *
 * @param z zis instance
 * @param reg register index
 * @param def native module definition; field `name` is ignored
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_module(zis_t z, unsigned int reg, const struct zis_native_module_def *def) ZIS_NOEXCEPT;

/**
 * Invoke a callable object.
 *
 * @param z zis instance
 * @param regs A vector of register indices. `regs[0]` = return value register,
 * `regs[1]` = object to invoke, `regs[2]`...`regs[argc+1]` = arguments.
 * Specially, `regs[2]` = first one of a vector of elements when `regs[3]` is `-1`;
 * `regs[2]` = the packed arguments (`Array` or `Tuple`) when `argc` is `-1`.
 * See @@details for special uses
 * @param argc number of arguments; or `-1` to indicate that `regs[2]` is the
 * packed arguments, which is an Array or a Tuple
 * @return `ZIS_OK` or `ZIS_THR`; `ZIS_E_IDX` (invalid `regs`),
 * `ZIS_E_ARG` (wrong type of packed arguments).
 *
 * @details In summary, there are three forms to pass arguments.
 * Here are examples to call function in `REG-0` with arguments in `REG-1` to `REG-3`:
 * ```c
 * // #1  enumerated arguments
 * zis_invoke(z, (unsigned[]){0, 0, 1, 2, 3}, 3); // { 1, 2, 3 } are arguments.
 * // #2  a vector of arguments (argc > 1 and regs[3] == -1)
 * zis_invoke(z, (unsigned[]){0, 0, 1, (unsigned)-1}, 3); // { 1, -1 } means arguments starting from 1.
 * // #3  packed arguments (argc == -1)
 * zis_make_values(z, 1, "(%%%)", 1, 2, 3); // Pack arguments into a tuple.
 * zis_invoke(z, (unsigned[]){0, 0, 1}, (size_t)-1); // { 1 } means packed arguments at 1.
 * ```
 *
 * @note Normally, the minimum length of array `regs` is `(argc + 2)`: 1 ret + 1 obj + N args.
 * When `regs[3]` is `-1`, the minimum length is `4`: 1 ret + 1 obj + 1 first_arg + (-1).
 * When `argc` is `-1`, the minimum length is `3`: 1 ret + 1 obj + 1 packed_args.
 *
 * @warning `REG-0` must not be used for argument passing.
 */
ZIS_API int zis_invoke(zis_t z, const unsigned int regs[], size_t argc) ZIS_NOEXCEPT;

/** @name zis_import() flags */
/** @{ */

#define ZIS_IMP_NAME     0x01 ///< `zis_import()` type: import by name.
#define ZIS_IMP_PATH     0x02 ///< `zis_import()` type: import by file path.
#define ZIS_IMP_CODE     0x03 ///< `zis_import()` type: compile source code.
#define ZIS_IMP_ADDP     0x0f ///< `zis_import()` type: add to search path.

#define ZIS_IMP_MAIN     0xf0 ///< `zis_import()` extra: call the `main` function (REG-1 = `(int)argc`, REG-2 = `(char**)argv`).

/** @} */

/**
 * Import a module.
 *
 * @param z zis instance
 * @param reg register index
 * @param what module name, or file path; see @@details
 * @param flags `ZIS_IMP_*` values; see @@details
 * @return `ZIS_OK` or `ZIS_THR`; `ZIS_E_ARG` (illegal `flags` or `what`).
 *
 * @details Examples:
 * ```c
 * // ##  To import a module by name
 * zis_import(z, reg, "module_name", ZIS_IMP_NAME);
 * // ##  To import a module by file path
 * zis_import(z, reg, "path/to/the/module/file.ext", ZIS_IMP_PATH);
 * // ##  To compile a string to a module
 * zis_import(z, reg, "the source code ...", ZIS_IMP_CODE);
 * // ##  To compile the `Stream` object in `REG-0` to a module
 * zis_import(z, reg, NULL, ZIS_IMP_CODE);
 * // ##  To add a module search path
 * zis_import(z, 0, "path/to/the/module/dir", ZIS_IMP_ADDP);
 * // ##  To load a module by path as the entry
 * zis_make_int(z, 1, argc);
 * zis_make_int(z, 2, (intptr_t)argv);
 * zis_import(z, "path", ZIS_IMP_PATH | ZIS_IMP_MAIN);
 * ```
 */
ZIS_API int zis_import(zis_t z, unsigned int reg, const char *what, int flags) ZIS_NOEXCEPT;

/** @} */

/** @defgroup zis-api-variables API: access and manipulate variables */
/** @{ */

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
ZIS_API int zis_move_local(zis_t z, unsigned int dst, unsigned int src) ZIS_NOEXCEPT;

/**
 * Get global variable from current module.
 *
 * @param z zis instance
 * @param reg register index
 * @param name name of the global variable; or `NULL` to get name from `REG-0`
 * @param name_len string length of parameter `name`; or `-1` to calculate length with `strlen()`
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (name does not exist or REG-0 is not a `Symbol`).
 *
 * @warning Do not try to access global variable outside a function,
 * in which case there is no *current module*.
 */
ZIS_API int zis_load_global(zis_t z, unsigned int reg, const char *name, size_t name_len) ZIS_NOEXCEPT;

/**
 * Set global variable to current module.
 *
 * @param z zis instance
 * @param reg register index
 * @param name name of the global variable; or `NULL` to get name from `REG-0`
 * @param name_len string length of parameter `name`; or `-1` to calculate length with `strlen()`
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (REG-0 is not a `Symbol`).
 *
 * @warning Do not try to access global variable outside a function,
 * in which case there is no *current module*.
 */
ZIS_API int zis_store_global(zis_t z, unsigned int reg, const char *name, size_t name_len) ZIS_NOEXCEPT;

/**
 * Get field of an object.
 *
 * `REG[reg_fld] <- ( REG[reg_obj] ) . ( REG[reg_name] )`
 *
 * @param z zis instance
 * @param reg_obj register where the object is
 * @param name field name string; or `NULL` to get name from `REG-0`.
 * @param name_len string length of parameter `name`; or `-1` to calculate length with `strlen()`
 * @param reg_val register to load the field to
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (name does not exist or REG-0 is not a `Symbol`).
 */
ZIS_API int zis_load_field(
    zis_t z, unsigned int reg_obj,
    const char *name, size_t name_len, unsigned int reg_val
) ZIS_NOEXCEPT;

/**
 * Set field of an object.
 *
 * `( REG[reg_obj] ) . ( REG[reg_name] ) <- REG[reg_fld]`
 *
 * @param z zis instance
 * @param reg_obj register where the object is
 * @param name field name string; or `NULL` to get name from `REG-0`.
 * @param name_len string length of parameter `name`; or `-1` to calculate length with `strlen()`
 * @param reg_val register where the new field value is
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (name does not exist or REG-0 is not a `Symbol`).
 */
ZIS_API int zis_store_field(
    zis_t z, unsigned int reg_obj,
    const char *name, size_t name_len, unsigned int reg_val
) ZIS_NOEXCEPT;

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
ZIS_API int zis_load_element(zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val) ZIS_NOEXCEPT;

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
ZIS_API int zis_store_element(zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val) ZIS_NOEXCEPT;

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
ZIS_API int zis_insert_element(zis_t z, unsigned int reg_obj, unsigned int reg_key, unsigned int reg_val) ZIS_NOEXCEPT;

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
ZIS_API int zis_remove_element(zis_t z, unsigned int reg_obj, unsigned int reg_key) ZIS_NOEXCEPT;

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ZIS_API
#undef ZIS_NOEXCEPT

#endif /* ZIS_H */
