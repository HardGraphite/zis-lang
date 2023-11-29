/// ZIS runtime context.

#pragma once

#include "attributes.h"

struct zis_callstack;
struct zis_objmem_context;

/// Runtime context.
struct zis_context {
    struct zis_objmem_context *objmem_context;
    struct zis_callstack      *callstack;
};

/// Create a runtime context.
zis_nodiscard struct zis_context *zis_context_create(void);

/// Delete a runtime context.
void zis_context_destroy(struct zis_context *z);
