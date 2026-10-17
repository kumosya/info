#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "os_adapter.h"

typedef struct AllocationHeader {
    size_t size;
    void* allocation;
} AllocationHeader;

void* malloc(size_t size) {
    AllocationHeader* header;
    if (!size) size=1;
    if (size > SIZE_MAX - sizeof(*header)) {
        errno = ENOMEM;
        return NULL;
    }
    header = __libc_os_allocate_memory(size + sizeof(*header));
    if (header == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    header->size = size;
    header->allocation = header;
    return header + 1;
}

void free(void* pointer) {
    if (pointer != NULL) __libc_os_free_memory(((AllocationHeader*)pointer - 1)->allocation);
}

void* calloc(size_t count, size_t size) {
    size_t total;
    void* pointer;
    if (size != 0 && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    total = count * size;
    pointer = malloc(total);
    if (pointer != NULL) memset(pointer, 0, total);
    return pointer;
}

void* realloc(void* pointer, size_t size) {
    AllocationHeader* header;
    size_t oldSize;
    void* replacement;
    if (pointer == NULL) return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    header = (AllocationHeader*)pointer - 1;
    oldSize = header->size;
    replacement = malloc(size);
    if (replacement == NULL) return NULL;
    memcpy(replacement, pointer, oldSize < size ? oldSize : size);
    free(pointer);
    return replacement;
}

int posix_memalign(void** result, size_t alignment, size_t size) {
    if (!result || alignment < sizeof(void*) || (alignment & (alignment-1))) return EINVAL;
    if (size > SIZE_MAX - alignment - sizeof(AllocationHeader)) return ENOMEM;
    void* allocation = __libc_os_allocate_memory(size + alignment + sizeof(AllocationHeader));
    if (!allocation) return ENOMEM;
    uintptr_t address = ((uintptr_t)allocation + sizeof(AllocationHeader) + alignment-1) & ~(alignment-1);
    AllocationHeader* header = (AllocationHeader*)address - 1;
    header->size = size;
    header->allocation = allocation;
    *result = (void*)address;
    return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
    void* result;
    if (!alignment || size % alignment) { errno = EINVAL; return NULL; }
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    int status = posix_memalign(&result, alignment, size);
    if (status) { errno = status; return NULL; }
    return result;
}
