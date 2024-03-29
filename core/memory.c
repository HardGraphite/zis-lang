#include "memory.h"

#include <assert.h>
#include <stdlib.h>

#include "debug.h"
#include "platform.h"
#include "zis_config.h"

#if ZIS_SYSTEM_POSIX
#    include <sys/mman.h>
#    include <unistd.h>
#    ifndef MAP_ANONYMOUS
#        define MAP_ANONYMOUS MAP_ANON
#    endif // MAP_ANONYMOUS
#elif ZIS_SYSTEM_WINDOWS
#    include <Windows.h>
#endif

#ifdef ZIS_MALLOC_INCLUDE
#    include ZIS_MALLOC_INCLUDE
#endif

zis_malloc_fn_attrs(1, size) void *zis_mem_alloc(size_t size) {
    return malloc(size);
}

zis_realloc_fn_attrs(2, size) void *zis_mem_realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

void zis_mem_free(void *ptr) {
    free(ptr);
}

zis_malloc_fn_attrs(1, size) void *zis_vmem_alloc(size_t size) {
    void *ptr;
#if ZIS_SYSTEM_POSIX
    const int   prot = PROT_READ | PROT_WRITE;
    const int   flags = MAP_PRIVATE | MAP_ANONYMOUS;
    ptr = mmap(NULL, size, prot, flags, -1, 0);
    if (zis_unlikely(ptr == MAP_FAILED))
        ptr = NULL;
#elif ZIS_SYSTEM_WINDOWS
    const DWORD type = MEM_RESERVE | MEM_COMMIT;
    const DWORD prot = PAGE_READWRITE;
    ptr = VirtualAlloc(NULL, size, type, prot);
#else
    ptr = zis_mem_alloc(size);
#endif
    zis_debug_log(INFO, "Memory", "vmem_alloc(%zu) -> %p", size, ptr);
    return ptr;
}

void zis_vmem_free(void *ptr, size_t size) {
    bool ok;
    zis_debug_log(INFO, "Memory", "vmem_free(%p, %zu)", ptr, size);
#if ZIS_SYSTEM_POSIX
    ok = munmap(ptr, size) == 0;
#elif ZIS_SYSTEM_WINDOWS
    zis_unused_var(size);
    ok = VirtualFree(ptr, 0, MEM_RELEASE);
#else
    zis_unused_var(size);
    zis_mem_free(ptr);
    ok = true;
#endif
    assert(ok), zis_unused_var(ok);
}

size_t zis_vmem_pagesize(void) {
#if ZIS_SYSTEM_POSIX
    const long sz = sysconf(_SC_PAGESIZE);
    assert(sz > 0);
    return (size_t)sz;
#elif ZIS_SYSTEM_WINDOWS
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return 4096;
#endif
}
