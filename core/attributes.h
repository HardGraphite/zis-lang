/// Attributes.

#pragma once

#if defined __GNUC__
#    define zis_likely(EXPR)   __builtin_expect((_Bool)(EXPR), 1)
#    define zis_unlikely(EXPR) __builtin_expect((_Bool)(EXPR), 0)
#    define zis_force_inline   __attribute__((always_inline)) inline
#    define zis_noinline       __attribute__((noinline))
#    define zis_nodiscard      __attribute__((warn_unused_result))
#    define zis_fallthrough    __attribute__((fallthrough))
#    define zis_unused_fn      __attribute__((unused))
#    define zis_hot_fn         __attribute__((hot))
#    define zis_cold_fn        __attribute__((cold))
#elif defined _MSC_VER
#    define zis_likely(EXPR)   (EXPR)
#    define zis_unlikely(EXPR) (EXPR)
#    define zis_force_inline   __forceinline
#    define zis_noinline       __declspec(noinline)
#    define zis_nodiscard      _Check_return_
#    define zis_fallthrough
#    define zis_unused_fn
#    define zis_hot_fn
#    define zis_cold_fn
#else
#    define zis_likely(EXPR)   (EXPR)
#    define zis_unlikely(EXPR) (EXPR)
#    define zis_force_inline
#    define zis_noinline
#    define zis_nodiscard
#    define zis_fallthrough
#    define zis_unused_fn
#    define zis_hot_fn
#    define zis_cold_fn
#endif

#define zis_static_inline       static inline
#define zis_static_force_inline zis_force_inline static
#define zis_noreturn            _Noreturn
#define zis_unused_var(X)       ((void)(X))

#if defined __GNUC__
#    define zis_printf_fn_attrs(fmtstr_idx, data_idx)                          \
        __attribute__((format(__printf__, fmtstr_idx, data_idx)))
#    define zis_printf_fn_arg_fmtstr
#    define zis_malloc_fn_attrs(sz_idx, sz_name)                               \
        __attribute__((malloc)) __attribute__((alloc_size(sz_idx)))
#    define zis_realloc_fn_attrs(sz_idx, sz_name)                              \
        __attribute__((warn_unused_result)) __attribute__((alloc_size(sz_idx)))
#elif defined _MSC_VER
#    define zis_printf_fn_attrs(fmtstr_idx, data_idx)
#    define zis_printf_fn_arg_fmtstr _Printf_format_string_
#    define zis_malloc_fn_attrs(sz_idx, sz_name)                               \
        _Check_return_ _Ret_maybenull_ _Post_writable_byte_size_(sz_name)
#    define zis_realloc_fn_attrs(sz_idx, sz_name)                              \
        _Success_(return != 0) _Check_return_ _Ret_maybenull_                  \
        _Post_writable_byte_size_(sz_name)
#else
#    define zis_printf_fn_attrs(fmtstr_idx, data_idx)
#    define zis_printf_fn_arg_fmtstr
#    define zis_malloc_attrs(sz_idx, sz_name)
#    define zis_realloc_attrs(sz_idx, sz_name)
#endif
