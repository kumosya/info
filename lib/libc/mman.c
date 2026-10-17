#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include "os_adapter.h"

#define MMAP_PAGE_SIZE 4096ULL
#define MMAP_BASE 0x0000000040000000ULL
#define MMAP_CAPACITY 64

typedef enum {
    MAPPING_NONE,
    MAPPING_RESERVED,
    MAPPING_ANONYMOUS,
    MAPPING_FILE_PRIVATE,
    MAPPING_SHARED_MEMORY
} MappingKind;

typedef struct {
    uint64_t address;
    uint64_t size;
    uint64_t offset;
    int descriptor;
    int protection;
    int writable;
    MappingKind kind;
} MemoryMapping;

static MemoryMapping mappings[MMAP_CAPACITY];
static uint64_t next_mapping = MMAP_BASE;
static volatile uint8_t mapping_lock;

static void lock_mappings(void) {
    while (__atomic_test_and_set(&mapping_lock, __ATOMIC_ACQUIRE)) __libc_os_yield();
}

static void unlock_mappings(void) {
    __atomic_clear(&mapping_lock, __ATOMIC_RELEASE);
}

static uint64_t page_align(size_t length) {
    if (length == 0 || length > UINT64_MAX - (MMAP_PAGE_SIZE - 1)) return 0;
    return ((uint64_t)length + MMAP_PAGE_SIZE - 1) & ~(MMAP_PAGE_SIZE - 1);
}

static uint64_t native_protection(int protection) {
    return ((protection & PROT_WRITE) ? 1U : 0U) |
           ((protection & PROT_EXEC) ? 2U : 0U);
}

static int reserve_mapping(uint64_t size, uint64_t* address) {
    int index;
    int slot = -1;
    lock_mappings();
    for (index = 0; index < MMAP_CAPACITY; ++index) {
        if (mappings[index].kind == MAPPING_NONE && slot < 0) slot = index;
    }
    if (slot < 0 || next_mapping > UINT64_MAX - size - MMAP_PAGE_SIZE) {
        unlock_mappings();
        return -1;
    }
    *address = next_mapping;
    next_mapping += size + MMAP_PAGE_SIZE;
    mappings[slot].address = *address;
    mappings[slot].size = size;
    mappings[slot].kind = MAPPING_RESERVED;
    unlock_mappings();
    return slot;
}

static void finish_mapping(int slot, MappingKind kind, int descriptor,
                           off_t offset, int protection, int writable) {
    lock_mappings();
    mappings[slot].kind = kind;
    mappings[slot].descriptor = descriptor;
    mappings[slot].offset = (uint64_t)offset;
    mappings[slot].protection = protection;
    mappings[slot].writable = writable;
    unlock_mappings();
}

static void abandon_mapping(int slot) {
    lock_mappings();
    mappings[slot] = (MemoryMapping){0};
    unlock_mappings();
}

void* mmap(void* address, size_t length, int protection, int flags,
           int file_descriptor, off_t offset) {
    const int map_type = flags & (MAP_PRIVATE | MAP_SHARED);
    uint64_t size = page_align(length);
    uint64_t requested;
    uint64_t result;
    OpenDescription* description = NULL;
    MappingKind kind;
    int slot;
    if (size == 0 || offset < 0 ||
        ((uint64_t)offset & (MMAP_PAGE_SIZE - 1)) != 0 ||
        (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
        map_type == 0 || map_type == (MAP_PRIVATE | MAP_SHARED) ||
        (flags & ~(MAP_PRIVATE | MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS)) != 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if ((flags & MAP_FIXED) != 0 || protection == PROT_NONE) {
        errno = ENOTSUP;
        return MAP_FAILED;
    }
    if ((protection & (PROT_WRITE | PROT_EXEC)) ==
        (PROT_WRITE | PROT_EXEC)) {
        errno = EACCES;
        return MAP_FAILED;
    }
    (void)address;
    if ((flags & MAP_ANONYMOUS) != 0) {
        if (map_type != MAP_PRIVATE || file_descriptor != -1 || offset != 0) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        kind = MAPPING_ANONYMOUS;
    } else {
        description = libcDescriptor(file_descriptor);
        if (description == NULL) {
            errno = EBADF;
            return MAP_FAILED;
        }
        if (description->kind == DESCRIPTOR_SHARED_MEMORY) {
            if (map_type != MAP_SHARED ||
                ((protection & PROT_WRITE) != 0 &&
                 (description->flags & O_ACCMODE) != O_RDWR)) {
                errno = EACCES;
                return MAP_FAILED;
            }
            kind = MAPPING_SHARED_MEMORY;
        } else if (description->kind == DESCRIPTOR_FILE) {
            if (map_type == MAP_SHARED) {
                errno = ENOTSUP;
                return MAP_FAILED;
            }
            if ((description->flags & O_ACCMODE) == O_WRONLY) {
                errno = EACCES;
                return MAP_FAILED;
            }
            kind = MAPPING_FILE_PRIVATE;
        } else {
            errno = ENODEV;
            return MAP_FAILED;
        }
    }
    slot = reserve_mapping(size, &requested);
    if (slot < 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (kind == MAPPING_SHARED_MEMORY) {
        result = __libc_os_map_shared_memory(description->handle, requested, size,
                                      (uint64_t)offset,
                                      native_protection(protection));
    } else {
        const uint64_t initial_protection =
            kind == MAPPING_FILE_PRIVATE ? 1U : native_protection(protection);
        result = __libc_os_map_memory(requested, size, initial_protection);
    }
    if ((int64_t)result < 0 || result != requested) {
        abandon_mapping(slot);
        errno = kind == MAPPING_SHARED_MEMORY ? EINVAL : ENOMEM;
        return MAP_FAILED;
    }
    if (kind == MAPPING_FILE_PRIVATE) {
        const ssize_t count = pread(file_descriptor, (void*)(uintptr_t)result,
                                    length, offset);
        if (count < 0 ||
            __libc_os_protect_memory(result, size,
                              native_protection(protection)) != 0) {
            (void)__libc_os_unmap_memory(result, size);
            abandon_mapping(slot);
            if (count >= 0) errno = EACCES;
            return MAP_FAILED;
        }
    }
    finish_mapping(slot, kind, file_descriptor, offset, protection,
                   description != NULL &&
                       (description->flags & O_ACCMODE) == O_RDWR);
    return (void*)(uintptr_t)result;
}

int munmap(void* address, size_t length) {
    uint64_t value = (uint64_t)(uintptr_t)address;
    uint64_t size = page_align(length);
    int index;
    if (value == 0 || (value & (MMAP_PAGE_SIZE - 1)) != 0 || size == 0) {
        errno = EINVAL;
        return -1;
    }
    lock_mappings();
    for (index = 0; index < MMAP_CAPACITY; ++index) {
        if (mappings[index].address == value && mappings[index].size == size &&
            mappings[index].kind > MAPPING_RESERVED) {
            if (__libc_os_unmap_memory(value, size) != 0) {
                unlock_mappings();
                errno = EINVAL;
                return -1;
            }
            mappings[index] = (MemoryMapping){0};
            unlock_mappings();
            return 0;
        }
    }
    unlock_mappings();
    errno = EINVAL;
    return -1;
}

int mprotect(void* address, size_t length, int protection) {
    const uint64_t value = (uint64_t)(uintptr_t)address;
    const uint64_t size = page_align(length);
    int index;
    if (value == 0 || (value & (MMAP_PAGE_SIZE - 1)) != 0 || size == 0 ||
        protection == PROT_NONE ||
        (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((protection & (PROT_WRITE | PROT_EXEC)) ==
        (PROT_WRITE | PROT_EXEC)) {
        errno = EACCES;
        return -1;
    }
    lock_mappings();
    for (index = 0; index < MMAP_CAPACITY; ++index) {
        const uint64_t mapping_end =
            mappings[index].address + mappings[index].size;
        if (mappings[index].kind > MAPPING_RESERVED &&
            value >= mappings[index].address && value + size > value &&
            value + size <= mapping_end) {
            if (mappings[index].kind == MAPPING_SHARED_MEMORY &&
                (protection & PROT_WRITE) != 0 &&
                !mappings[index].writable) {
                unlock_mappings();
                errno = EACCES;
                return -1;
            }
            if (__libc_os_protect_memory(value, size,
                                  native_protection(protection)) != 0) {
                unlock_mappings();
                errno = EINVAL;
                return -1;
            }
            if (value == mappings[index].address &&
                size == mappings[index].size) {
                mappings[index].protection = protection;
            }
            unlock_mappings();
            return 0;
        }
    }
    unlock_mappings();
    errno = ENOMEM;
    return -1;
}

int msync(void* address, size_t length, int flags) {
    uint64_t value = (uint64_t)(uintptr_t)address;
    uint64_t size = page_align(length);
    int index;
    if (value == 0 || size == 0 || (value & (MMAP_PAGE_SIZE - 1)) != 0 ||
        (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) != 0 ||
        ((flags & MS_ASYNC) && (flags & MS_SYNC))) {
        errno = EINVAL;
        return -1;
    }
    lock_mappings();
    for (index = 0; index < MMAP_CAPACITY; ++index) {
        if (mappings[index].kind > MAPPING_RESERVED &&
            value >= mappings[index].address && value + size > value &&
            value + size <= mappings[index].address + mappings[index].size) {
            unlock_mappings();
            return 0;
        }
    }
    unlock_mappings();
    errno = ENOMEM;
    return -1;
}

int shm_open(const char* name, int flags, mode_t mode) {
    uint64_t handle;
    int descriptor;
    const int access = flags & O_ACCMODE;
    (void)mode;
    if (name == NULL || name[0] != '/' || name[1] == '\0' ||
        (access != O_RDONLY && access != O_RDWR) ||
        (flags & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & O_TRUNC) != 0 && access != O_RDWR) {
        errno = EACCES;
        return -1;
    }
    handle = __libc_os_open_shared_memory(name, flags);
    if ((int64_t)handle < 0 || handle == 0) {
        errno = ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
                    ? EEXIST
                    : ENOENT;
        return -1;
    }
    if ((flags & O_TRUNC) != 0 && __libc_os_resize_shared_memory(handle, 0) != 0) {
        (void)__libc_os_close_handle(handle);
        errno = EINVAL;
        return -1;
    }
    descriptor = libcAllocateDescriptor(handle, name, flags,
                                         DESCRIPTOR_SHARED_MEMORY);
    if (descriptor < 0) {
        (void)__libc_os_close_handle(handle);
        return -1;
    }
    libcDescriptors[descriptor].closeOnExec = 1;
    return descriptor;
}

int shm_unlink(const char* name) {
    if (name == NULL || __libc_os_unlink_shared_memory(name) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
