/// Memory management.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "attributes.h"

/* ----- high-level memory allocation --------------------------------------- */

/// Allocate memory like `malloc()`.
zis_malloc_fn_attrs(1, size) void *zis_mem_alloc(size_t size);

/// Re-alloc memory like `realloc()`.
zis_realloc_fn_attrs(2, size) void *zis_mem_realloc(void *ptr, size_t size);

/// Dealloc memory like `free()`.
void zis_mem_free(void *ptr);

/* ----- virtual memory ----------------------------------------------------- */

/// Allocate virtual memory like `mmap()` or `VirtualAlloc()`.
zis_malloc_fn_attrs(1, size) void *zis_vmem_alloc(size_t size);

/// Dealloc virtual memory like `munmap()` or `VirtualFree()`.
bool zis_vmem_free(void *ptr, size_t size);

/// Get virtual memory page size.
size_t zis_vmem_pagesize(void);
