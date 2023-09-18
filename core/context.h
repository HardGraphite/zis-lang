/// ZIS runtime context.

#pragma once

#include "attributes.h"

struct zis_context {
    int _;
};

/// Create a runtime context.
zis_nodiscard struct zis_context *zis_context_create(void);
/// Delete a runtime context.
void zis_context_destroy(struct zis_context *z);
