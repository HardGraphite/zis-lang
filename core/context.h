/// ZIS runtime context.

#pragma once

#include "attributes.h"
#include "locals.h"

struct zis_callstack;
struct zis_context;
struct zis_context_globals;
struct zis_module_loader;
struct zis_object;
struct zis_objmem_context;
struct zis_symbol_registry;

typedef void(*zis_context_panic_handler_t)(struct zis_context *, int);

/// Runtime context.
struct zis_context {
    struct zis_objmem_context  *objmem_context;
    struct zis_callstack       *callstack;
    struct zis_symbol_registry *symbol_registry;
    struct zis_context_globals *globals;
    struct zis_module_loader   *module_loader;
    struct zis_locals_root      locals_root;
    zis_context_panic_handler_t panic_handler;
};

/// Create a runtime context.
zis_nodiscard struct zis_context *zis_context_create(void);

/// Delete a runtime context.
void zis_context_destroy(struct zis_context *z);

/// Store `v` to REG-0.
void zis_context_set_reg0(struct zis_context *z, struct zis_object *v);

/// Load the value in REG-0.
struct zis_object *zis_context_get_reg0(struct zis_context *z);

/// Panic reason. See `zis_context_panic()`.
enum zis_context_panic_reason {
    ZIS_CONTEXT_PANIC_ABORT,///< Abort without calling a handler.
    ZIS_CONTEXT_PANIC_OOM,  ///< Out of memory.
    ZIS_CONTEXT_PANIC_SOV,  ///< Stack overflow.
    ZIS_CONTEXT_PANIC_ILL,  ///< Illegal bytecode.
    ZIS_CONTEXT_PANIC_IMPL, ///< Not implemented.
};

/// Call the panic handler and then abort.
zis_noreturn void zis_context_panic(struct zis_context *z, enum zis_context_panic_reason r);
