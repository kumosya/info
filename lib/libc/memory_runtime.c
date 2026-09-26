#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "os/c_api.h"

typedef struct AllocationHeader {
    size_t size;
} AllocationHeader;

void* malloc(size_t size) {
    AllocationHeader* header;
    if (size == 0 || size > SIZE_MAX - sizeof(*header)) {
        errno = ENOMEM;
        return NULL;
    }
    header = os_allocate_memory(size + sizeof(*header));
    if (header == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    header->size = size;
    return header + 1;
}

void free(void* pointer) {
    if (pointer != NULL) os_free_memory((AllocationHeader*)pointer - 1);
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
