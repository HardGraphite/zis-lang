#ifndef ZIS_H
#define ZIS_H 1

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

/**
 * Runtime context.
 */
struct zis_context;

/**
 * Create a runtime context.
 *
 * @return Returns the pointer to the newly created context.
 *
 * @warning To avoid a memory leak, the context must be finalized with `zis_destroy()`.
 */
ZIS_API struct zis_context *zis_create(void) ZIS_NOEXCEPT;

/**
 * Delete a runtime context.
 *
 * @param z Pointer to the context to destroy.
 */
ZIS_API void zis_destroy(struct zis_context *z) ZIS_NOEXCEPT;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ZIS_NOEXCEPT

#endif /* ZIS_H */
