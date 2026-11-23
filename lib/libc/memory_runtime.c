#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lock.h"

typedef struct AllocationHeader {
    size_t size;
    void* allocation;
} AllocationHeader;

typedef struct SmallArena {
    struct SmallArena* next;
    void* availableBlocks;
    size_t blockSize;
    size_t capacity;
    size_t available;
    size_t classIndex;
} SmallArena;

enum { smallClassCount = 8, smallArenaSize = 65536 };
static const size_t smallBlockSizes[smallClassCount] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096
};
static SmallArena* smallArenas[smallClassCount];
static volatile uint32_t allocatorLock;

/* fork 按与运行库相同的次序冻结分配状态，孩子从未占锁状态继续。 */
void __libc_allocator_fork_prepare(void) {
    libcWaitLock(&allocatorLock);
}

void __libc_allocator_fork_parent(void) {
    libcWaitUnlock(&allocatorLock);
}

void __libc_allocator_fork_child(void) {
    allocatorLock = 0;
}

/* 一次申请一个页块，再把它划为同尺寸的小块，块头和用户地址都按 16 字节对齐。 */
static SmallArena* createSmallArena(size_t classIndex) {
    SmallArena* arena = __libc_os_allocate_memory(smallArenaSize);
    if (arena == NULL) {
        return NULL;
    }

    const size_t first = (sizeof(*arena) + 15) & ~(size_t)15;
    arena->classIndex = classIndex;
    arena->blockSize = smallBlockSizes[classIndex];
    arena->capacity = (smallArenaSize - first) / arena->blockSize;
    arena->available = arena->capacity;
    arena->availableBlocks = NULL;
    for (size_t index = arena->capacity; index != 0; --index) {
        void* block = (unsigned char*)arena + first + (index - 1) * arena->blockSize;
        *(void**)block = arena->availableBlocks;
        arena->availableBlocks = block;
    }

    arena->next = smallArenas[classIndex];
    smallArenas[classIndex] = arena;
    return arena;
}

/* 低位标记区分小块所在页块与单独申请的大块；页级地址本身按页对齐。 */
static SmallArena* smallArena(const AllocationHeader* header) {
    const uintptr_t allocation = (uintptr_t)header->allocation;
    return allocation & 1U ? (SmallArena*)(allocation & ~(uintptr_t)1) : NULL;
}

/* 优先复用已有页块的空闲位置，减少服务调用和运行库分配记录。 */
static void* allocateSmall(size_t size, size_t classIndex) {
    libcWaitLock(&allocatorLock);
    SmallArena* arena = smallArenas[classIndex];
    while (arena != NULL && arena->available == 0) {
        arena = arena->next;
    }
    if (arena == NULL) {
        arena = createSmallArena(classIndex);
    }
    if (arena == NULL) {
        libcWaitUnlock(&allocatorLock);
        errno = ENOMEM;
        return NULL;
    }

    AllocationHeader* header = arena->availableBlocks;
    arena->availableBlocks = *(void**)header;
    --arena->available;
    header->size = size;
    header->allocation = (void*)((uintptr_t)arena | 1U);
    libcWaitUnlock(&allocatorLock);
    return header + 1;
}

/* 每种尺寸保留一个空页块供复用，多余的空页块归还运行库。 */
static void freeSmall(AllocationHeader* header, SmallArena* arena) {
    libcWaitLock(&allocatorLock);
    *(void**)header = arena->availableBlocks;
    arena->availableBlocks = header;
    ++arena->available;

    int release = 0;
    if (arena->available == arena->capacity) {
        for (SmallArena* other = smallArenas[arena->classIndex]; other != NULL; other = other->next) {
            if (other != arena && other->available == other->capacity) {
                release = 1;
                break;
            }
        }
    }
    if (release) {
        SmallArena** position = &smallArenas[arena->classIndex];
        while (*position != arena) {
            position = &(*position)->next;
        }
        *position = arena->next;
    }
    libcWaitUnlock(&allocatorLock);
    if (release) {
        __libc_os_free_memory(arena);
    }
}

/* 小申请共用页块，大申请继续使用独立的运行库分配。 */
void* malloc(size_t size) {
    AllocationHeader* header;
    if (!size) {
        size = 1;
    }
    if (size > SIZE_MAX - sizeof(*header)) {
        errno = ENOMEM;
        return NULL;
    }
    const size_t required = size + sizeof(*header);
    size_t classIndex = 0;
    while (classIndex < smallClassCount && smallBlockSizes[classIndex] < required) {
        ++classIndex;
    }
    if (classIndex < smallClassCount) {
        return allocateSmall(size, classIndex);
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

/* 小块回到所在页块，大块归还原始页级申请。 */
void free(void* pointer) {
    if (pointer != NULL) {
        AllocationHeader* header = (AllocationHeader*)pointer - 1;
        SmallArena* arena = smallArena(header);
        if (arena != NULL) {
            freeSmall(header, arena);
        } else {
            __libc_os_free_memory(header->allocation);
        }
    }
}

/* 检查总长度后分配，并清零调用者申请的部分。 */
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

/* 同一小块还有容量时原地调整，否则迁移内容再释放旧块。 */
void* realloc(void* pointer, size_t size) {
    AllocationHeader* header;
    size_t oldSize;
    void* replacement;
    if (pointer == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(pointer);
        return NULL;
    }

    header = (AllocationHeader*)pointer - 1;
    oldSize = header->size;
    SmallArena* arena = smallArena(header);
    if (arena != NULL && size <= arena->blockSize - sizeof(*header)) {
        header->size = size;
        return pointer;
    }
    replacement = malloc(size);
    if (replacement == NULL) return NULL;
    memcpy(replacement, pointer, oldSize < size ? oldSize : size);
    free(pointer);
    return replacement;
}

/* 单独保存原始页级地址，释放时仍能找到对齐前的申请。 */
int posix_memalign(void** result, size_t alignment, size_t size) {
    if (!result || alignment < sizeof(void*) || (alignment & (alignment-1))) {
        return EINVAL;
    }
    if (size > SIZE_MAX - alignment - sizeof(AllocationHeader)) {
        return ENOMEM;
    }

    void* allocation = __libc_os_allocate_memory(size + alignment + sizeof(AllocationHeader));
    if (!allocation) {
        return ENOMEM;
    }

    uintptr_t address = ((uintptr_t)allocation + sizeof(AllocationHeader) + alignment-1) & ~(alignment-1);
    AllocationHeader* header = (AllocationHeader*)address - 1;
    header->size = size;
    header->allocation = allocation;
    *result = (void*)address;
    return 0;
}

/* C 对齐申请要求长度是对齐量的整数倍。 */
void* aligned_alloc(size_t alignment, size_t size) {
    void* result;
    if (!alignment || size % alignment) {
        errno = EINVAL;
        return NULL;
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    int status = posix_memalign(&result, alignment, size);
    if (status) {
        errno = status;
        return NULL;
    }
    return result;
}
