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

#define ZIS_OK          0      /**< Succeeded. */

#define ZIS_THR         (-1)   /**< An object (maybe an exception) was thrown. */

#define ZIS_E_ARG       (-11)  /**< Illegal argument. */
#define ZIS_E_IDX       (-12)  /**< Index out of range. */
#define ZIS_E_TYPE      (-13)  /**< Type mismatched. */
#define ZIS_E_BUF       (-14)  /**< Buffer is not big enough. */

/** @} */

/**
 * Build information structure. @see `zis_build_info`.
 */
struct zis_build_info {
    const char *system;     /**< Operating system name. */
    const char *machine;    /**< Hardware (architecture) name. */
    const char *compiler;   /**< Compiler name and version. */
    const char *extra;      /**< Extra information. Optional. */
    uint32_t    timestamp;  /**< UNIX timestamp (UTC), divided by 60. */
    uint8_t     version[3]; /**< Version number (major, minor, patch). */
};

/**
 * Build information.
 */
ZIS_API extern const struct zis_build_info zis_build_info;

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
#define ZIS_PANIC_OOM   1  /**< Panic cause: out of memory (object memory) */
#define ZIS_PANIC_SOV   2  /**< Panic cause: stack overflow (runtime callstack) */
#define ZIS_PANIC_ILL   3  /**< Panic cause: illegal bytecode */
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

struct zis_native_func_def;
struct zis_native_type_def;
struct zis_native_module_def;

/**
 * Definition of a simple native value.
 */
struct zis_native_value_def {
    /**
     * Type of the value.
     *
     * The type is represented as a character. Read the comment on the corresponding
     * member variable for the type character.
     */
    char type;

    union {
        const void *n; /**< Type `Nil` (`.type`=`n`). */
        bool        x; /**< Type `Bool` (`.type`=`b`). */
        int64_t     i; /**< Type `Int` (`.type`=`i`). */
        double      f; /**< Type `Float` (`.type`=`f`). */
        const char *s; /**< Type `String` (`.type`=`s`). */
        const char *y; /**< Type `Symbol` (`.type`=`y`). */
        const struct zis_native_value_def *T; /**< Type `Tuple` (`.type`=`(`). */
        const struct zis_native_value_def *A; /**< Type `Array` (`.type`=`[`). */
        const struct zis_native_value_def *M; /**< Type `Map` (`.type`=`{`). */
        const struct zis_native_func_def  *F; /**< Type `Function` (`.type`=`^`). */
    }
#if !defined(__cplusplus) && (!defined(__STDC__) || __STDC_VERSION__ < 201112L)
    value
    /* Anonymous union is not supported before C11. */
#endif
    /* Designator initializer is not supported before C99 and C++20. */
    ;
};

struct zis_native_value_def__named {
    const char *name;
    struct zis_native_value_def value;
};

/**
 * Implementation of a native function.
 */
typedef int (*zis_native_func_t)(zis_t) ZIS_NOEXCEPT;

/**
 * Metadata of a native function.
 */
struct zis_native_func_meta {
    /**
     * Number of arguments.
     *
     * Number of required arguments in a function, excluding optional ones.
     */
    uint8_t na;

    /**
     * Number of optional arguments.
     *
     * Number of optional arguments in a function, passed after required ones.
     * Or `-1` to accept a `Tuple` holding the rest arguments (variadic).
     */
    uint8_t no;

    /**
     * Number of local variables.
     *
     * Number of local variables in a function, excluding REG-0 (the first register)
     * but including arguments. That is, the maximum of the indices of used registers.
     */
    uint16_t nl;
};

/**
 * Definition of a native function.
 */
struct zis_native_func_def {
    /**
     * Function metadata.
     */
    struct zis_native_func_meta meta;

    /**
     * The C function that implements this function.
     *
     * @warning The value must not be `0` or `1`. Otherwise this structure would
     * be interpreted as a `struct zis_native_func_def_ex` object.
     */
    zis_native_func_t code;
};

/**
 * Definition of a native function with extra information.
 *
 * @note A pointer to an object of this structure can be cast to `struct zis_native_func_def *`,
 * but make sure that the value of member `code_type` is `0` or `1`.
 */
struct zis_native_func_def_ex {
    /**
     * Function metadata.
     */
    struct zis_native_func_meta meta;

    /**
     * Function implementation type.
     *
     * Specifies which implementation is provided in member `code`.
     * Value `0` means using `code.bytecode`; value `1` means using `code.native`.
     * Other values are not allowed.
     *
     * @warning The value must be either `0` or `1`. Otherwise this structure
     * would be interpreted as a `struct zis_native_func_def` object.
     */
    uintptr_t code_type;

    union {
        /**
         * Function implementation in bytecode, valid when `code_type` is `0`.
         *
         * An array of bytecode instructions, ternimated with `-1`.
         */
        const uint32_t *bytecode;

        /**
         * Function implementation in native function, valid when `code_type` is `1`.
         */
        zis_native_func_t native;
    } code; /**< Function implementation. See member `code_type`. */

    /**
     * Symbols (optional).
     *
     * A NULL-terminated array of strings that defines the symbols to be used
     * in this function.
     */
    const char *const *symbols;

    /**
     * Constants (optional).
     *
     * A NULL-terminated array of value definitions that defines the constants
     * to be used in this function.
     *
     * @see ZIS_NATIVE_VALUE_DEF_LIST()
     */
    const struct zis_native_value_def *constants;
};

struct zis_native_func_def__named_ref {
    const char *name;
    const struct zis_native_func_def *def;
};

/**
 * Definition of a native type (struct).
 */
struct zis_native_type_def {
    /**
     * Number of slots in object SLOTS part.
     */
    size_t slots_num;

    /**
     * Size of object BYTES part.
     */
    size_t bytes_size;

    /**
     * List of field names (optional).
     *
     * An array of strings (or NULLs) indicating field names. The length of
     * the array must be the same with member variable `slots_num`.
     */
    const char *const *fields;

    /**
     * List of method definitions (optional).
     *
     * A zero-terminated array of named function definitions that defines the methods.
     *
     * @see ZIS_NATIVE_FUNC_DEF_LIST()
     */
    const struct zis_native_func_def__named_ref *methods;

    /**
     * List of static variable definitions (optional).
     *
     * A zero-terminated array of named variable definitions that defines
     * the type's static variables.
     *
     * @see ZIS_NATIVE_VAR_DEF_LIST()
     */
    const struct zis_native_value_def__named *statics;
};

struct zis_native_type_def__named_ref {
    const char *name;
    const struct zis_native_type_def *def;
};

/**
 * Definition of a native module.
 *
 * When a module is created based on such a definition, the functions and types
 * are created and stored as module variables (global variables), excepting
 * those without names (`.name = NULL`).
 */
struct zis_native_module_def {
    /**
     * List of function definitions (optional).
     *
     * A zero-terminated array of named function definitions that defines
     * the global functions in the module.
     * If the first function definition does not have a name, it is the module
     * initializer and will be called automatically after the module created.
     *
     * @see ZIS_NATIVE_FUNC_DEF_LIST()
     */
    const struct zis_native_func_def__named_ref *functions;

    /**
     * List of type definitions (optional).
     *
     * A zero-terminated array of named type definitions that defines
     * the global types in the module.
     *
     * @see ZIS_NATIVE_TYPE_DEF_LIST()
     */
    const struct zis_native_type_def__named_ref *types;

    /**
     * List of variable definitions (optional).
     *
     * A zero-terminated array of named variable definitions that defines
     * the global variables in the module.
     *
     * @see ZIS_NATIVE_VAR_DEF_LIST()
     */
    const struct zis_native_value_def__named *variables;
};

/**
 * A macro to define a global variable that exports a native module from C source code.
 *
 * @see ZIS_NATIVE_MODULE__VAR
 */
#define ZIS_NATIVE_MODULE(MODULE_NAME) \
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
 * @param str pointer to the integer string (prefix '-' is allowed; underscores '_' are ignored)
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
 * Create a value from `struct zis_native_value_def`.
 *
 * @param z zis instance
 * @param reg register to store the value to
 * @param def pointer to the definition
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_value(zis_t z, unsigned int reg, const struct zis_native_value_def *def);

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

#define ZIS_RDE_TEST     0x00 /**< `zis_read_exception()`: do nothing. */
#define ZIS_RDE_TYPE     0x01 /**< `zis_read_exception()`: get the `type` field. */
#define ZIS_RDE_DATA     0x02 /**< `zis_read_exception()`: get the `data` field. */
#define ZIS_RDE_WHAT     0x03 /**< `zis_read_exception()`: get the `what` field. */
#define ZIS_RDE_DUMP     0x04 /**< `zis_read_exception()`: print this exception. */

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

#define ZIS_IOS_FILE    0x01 /**< `zis_make_stream()` type: file stream. */
#define ZIS_IOS_STDX    0x02 /**< `zis_make_stream()` type: standard I/O stream (0=stdin, 1=stdout, 2=stderr). */
#define ZIS_IOS_TEXT    0x03 /**< `zis_make_stream()` type: read-only string stream. */

#define ZIS_IOS_RDONLY  0x10 /**< `zis_make_stream()` `ZIS_IOS_FILE` mode: read-only. */
#define ZIS_IOS_WRONLY  0x20 /**< `zis_make_stream()` `ZIS_IOS_FILE` mode: write-only. */
#define ZIS_IOS_WINEOL  0x40 /**< `zis_make_stream()` `ZIS_IOS_FILE` mode: use Windows style of end-of-line (CRLF). */

#define ZIS_IOS_STATIC  0x80 /**< `zis_make_stream()` `ZIS_IOS_TEXT` mode: string is static (infinite lifetime). */

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
 * To get the standard input stream:
 * ```c
 * const int stdio_id = 0; // 0 for stdin
 * int status = zis_make_stream(z, reg, ZIS_IOS_STDX, stdio_id);
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
ZIS_API int zis_make_stream(zis_t z, unsigned int reg, int flags, ...) ZIS_NOEXCEPT;

/** @} */

/** @defgroup zis-api-code API: code (functions and modules) */
/** @{ */

/**
 * Create a function.
 *
 * @param z zis instance
 * @param reg register index
 * @param def native function definition, pointer to either a `struct zis_native_func_def`
 * object or a `struct zis_native_func_def_ex` object
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
 * @param def native function definition
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_type(
    zis_t z, unsigned int reg,
    const struct zis_native_type_def *def
) ZIS_NOEXCEPT;

/**
 * Create a module.
 *
 * @param z zis instance
 * @param reg register index
 * @param def native module definition
 * @return `ZIS_OK`; `ZIS_E_IDX` (invalid `reg`).
 */
ZIS_API int zis_make_module(zis_t z, unsigned int reg, const struct zis_native_module_def *def) ZIS_NOEXCEPT;

/**
 * Invoke a callable object or method.
 *
 * @param z zis instance
 * @param regs A vector of register indices. `regs[0]` = return value register,
 * `regs[1]` = object to invoke, `regs[2]`...`regs[argc+1]` = arguments.
 * Specially, when `regs[3]` is `-1`, `regs[2]` = the first register of the argument vector;
 * when `argc` is `-1`, `regs[2]` = the packed arguments (`Array` or `Tuple`);
 * when `regs[-1]` = `-1`, call the first argument's method whose name is given in REG-0 (a symbol).
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
 * @details Here are examples to call method `foo`:
 * ```c
 * // #1  enumerated arguments
 * zis_make_symbol(z, 0, "foo", 3);
 * zis_invoke(z, (unsigned[]){0, (unsigned)-1, 1, 2, 3}, 3);
 * // #2  a vector of arguments
 * zis_make_symbol(z, 0, "foo", 3);
 * zis_invoke(z, (unsigned[]){0, (unsigned)-1, 1, (unsigned)-1}, 3);
 * // !! packed arguments not supported
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

#define ZIS_IMP_NAME     0x01 /**< `zis_import()` type: import by name. */
#define ZIS_IMP_PATH     0x02 /**< `zis_import()` type: import by file path. */
#define ZIS_IMP_CODE     0x03 /**< `zis_import()` type: compile source code. */
#define ZIS_IMP_ADDP     0x0f /**< `zis_import()` type: add to search path. */

#define ZIS_IMP_MAIN     0xf0 /**< `zis_import()` extra: call the `main` function (REG-1 = `(int)argc`, REG-2 = `(char**)argv`). */

/** @} */

/**
 * Import a module.
 *
 * @param z zis instance
 * @param reg register index
 * @param what module name, or file path; see @@details
 * @param flags `ZIS_IMP_*` values; see @@details
 * @return `ZIS_OK` or `ZIS_THR`; `ZIS_E_ARG` (illegal `flags` or `what`);
 * the returned integer from `main()` function if `flags` contains `ZIS_IMP_MAIN`.
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (REG-0 is not a `Symbol`).
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (REG-0 is not a `Symbol`).
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index), `ZIS_E_ARG` (REG-0 is not a `Symbol`).
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index); `ZIS_E_ARG` (key does not exist).
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index); `ZIS_E_ARG` (key does not exist).
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index).
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
 * @return `ZIS_OK`; `ZIS_THR`, `ZIS_E_IDX` (invalid reg index).
 */
ZIS_API int zis_remove_element(zis_t z, unsigned int reg_obj, unsigned int reg_key) ZIS_NOEXCEPT;

/** @} */

/** @defgroup zis-api-misc API: miscellaneous */
/** @{ */

#if defined(__cplusplus) && __cplusplus >= 201803L
#    define ZIS_API__IF_LIKELY(EXPR) if (EXPR) [[likely]]
#    define ZIS_API__IF_UNLIKELY(EXPR) if (EXPR) [[unlikely]]
#elif defined(__GNUC__) || defined(__clang__)
#    define ZIS_API__IF_LIKELY(EXPR) if (__builtin_expect((bool)(EXPR), 1))
#    define ZIS_API__IF_UNLIKELY(EXPR) if (__builtin_expect((bool)(EXPR), 0))
#else
#    define ZIS_API__IF_LIKELY(EXPR) if (EXPR)
#    define ZIS_API__IF_UNLIKELY(EXPR) if (EXPR)
#endif

#if __STDC__ && __STDC_VERSION__ >= 201112L
#    define ZIS_API__EXPR_WITH_TYPE_CHECKED(EXPR, TYPE) (_Generic((EXPR), TYPE : (EXPR)))
#else
#    define ZIS_API__EXPR_WITH_TYPE_CHECKED(EXPR, TYPE) (EXPR)
#endif

/**
 * Defines a IF branch, testing whether `expr` is `ZIS_OK`.
 *
 * @details Example:
 * ```c
 * int64_t value;
 * zis_if_ok (zis_read_int(z, 0, &value)) {
 *     printf("value = %" PRIi64, value);
 * }
 * ```
 */
#define zis_if_ok(__expr) \
    ZIS_API__IF_LIKELY( ZIS_API__EXPR_WITH_TYPE_CHECKED(__expr, int) == ZIS_OK )

/**
 * Defines a IF branch, testing whether `expr` is `ZIS_THR`.
 *
 * @see zis_if_ok()
 */
#define zis_if_thr(__expr) \
    ZIS_API__IF_UNLIKELY( ZIS_API__EXPR_WITH_TYPE_CHECKED(__expr, int) == ZIS_THR )

/**
 * Defines a IF branch, testing whether `expr` is not `ZIS_OK`.
 *
 * @see zis_if_ok()
 */
#define zis_if_err(__expr) \
    ZIS_API__IF_UNLIKELY( ZIS_API__EXPR_WITH_TYPE_CHECKED(__expr, int) != ZIS_OK )

#if (defined(__STDC__) && __STDC_VERSION__ >= 199901L) || (defined(__cplusplus) && __cplusplus >= 201103L)
/* C99, C++11: supports variable number of arguments in function-like macros. */

/**
 * Generates a static const variable that defines a function.
 *
 * `ZIS_NATIVE_FUNC_DEF(func_def_var, context_var, func_meta)`.
 * Parameter `func_def_var` is the variable name of the definition; `context_var`
 * is the argument (of type `zis_t`) name in the function implementation;
 * `func_meta` is the function metadata initializer.
 *
 * @details Example:
 * ```c
 * ZIS_NATIVE_FUNC_DEF(F_add_int, z, {2, 0, 2}) {
 *     int64_t lhs, rhs;
 *     zis_read_int(z, 1, &lhs), zis_read_int(z, 2, &rhs);
 *     zis_make_int(z, 0, lhs + rhs);
 *     return ZIS_OK;
 * }
 * ```
 *
 * @see struct zis_native_func_def
 */
#define ZIS_NATIVE_FUNC_DEF(__func_def_var, __context_var, ...) \
    static int __func_def_var##__impl (zis_t);                  \
    static const struct zis_native_func_def __func_def_var = {  \
        .meta = __VA_ARGS__ ,                                   \
        .code = __func_def_var##__impl ,                        \
    };                                                          \
    static int __func_def_var##__impl ( zis_t __context_var )

/**
 * Generates a static const variable that defines an array of named function definitions.
 *
 * @details Example:
 * ```c
 * ZIS_NATIVE_FUNC_DEF(F_f1, z, {2, 0, 2}) { ... }
 * ZIS_NATIVE_FUNC_DEF(F_f2, z, {1, 0, 3}) { ... }
 * ZIS_NATIVE_FUNC_DEF_LIST(
 *     func_list,
 *     { "f1", &F_f1 },
 *     { "f2", &F_f2 },
 * );
 * ```
 *
 * @see struct zis_native_func_def__named_ref
 */
#define ZIS_NATIVE_FUNC_DEF_LIST(__list_var, ...) \
    static const struct zis_native_func_def__named_ref __list_var [] = { \
        __VA_ARGS__                               \
        { NULL, NULL }                            \
    }

/**
 * Generates a static const variable that defines an array of named type definitions.
 *
 * @see struct zis_native_type_def__named_ref
 */
#define ZIS_NATIVE_TYPE_DEF_LIST(__list_var, ...) \
    static const struct zis_native_type_def__named_ref __list_var [] = { \
        __VA_ARGS__                               \
        { NULL, NULL }                            \
    }

/**
 * Generates a static const variable that defines an array of named variable definitions.
 *
 * @see struct zis_native_value_def__named
 */
#define ZIS_NATIVE_VAR_DEF_LIST(__list_var, ...) \
    static const struct zis_native_value_def__named __list_var [] = { \
        __VA_ARGS__                              \
        { NULL, { .type = 0, .n = NULL } }       \
    }

/**
 * Generates a static const variable that defines an array of value definitions.
 *
 * @see struct zis_native_value_def
 */
#define ZIS_NATIVE_VALUE_DEF_LIST(__list_var, ...) \
    static const struct zis_native_value_def __list_var [] = { \
        __VA_ARGS__                                \
        { .type = 0, .n = NULL }                   \
    }

#endif /* C99, C++11 */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ZIS_API
#undef ZIS_NOEXCEPT

#endif /* ZIS_H */
