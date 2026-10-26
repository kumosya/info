#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include "internal.h"

static uint64_t page_align(size_t length) {
    return !length || length > UINT64_MAX - 4095 ? 0 : ((uint64_t)length + 4095) & ~4095ULL;
}

static int mapping_error(int64_t result) {
    switch (result) {
        case -1: errno = EINVAL; break;
        case -2: errno = EBADF; break;
        case -3: errno = EACCES; break;
        case -4: errno = ENOENT; break;
        case -5: errno = ENOMEM; break;
        case -7: errno = ENOTSUP; break;
        case -11: errno = EIO; break;
        default: errno = EIO; break;
    }
    return -1;
}

void* mmap(void* address, size_t length, int protection, int flags,
           int descriptor, off_t offset) {
    const uint64_t size = page_align(length);
    const int type = flags & (MAP_PRIVATE | MAP_SHARED);
    if (!size || offset < 0 || ((uint64_t)offset & 4095) ||
        (uint64_t)offset > INT64_MAX - size || (protection & ~7) ||
        !type || type == (MAP_PRIVATE | MAP_SHARED) ||
        (flags & ~(MAP_PRIVATE | MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS))) {
        errno = EINVAL; return MAP_FAILED;
    }
    if (flags & MAP_FIXED) { errno = ENOTSUP; return MAP_FAILED; }
    if ((protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
        errno = EACCES; return MAP_FAILED;
    }
    int64_t result;
    const uint64_t hint = (uint64_t)(uintptr_t)address;
    if (flags & MAP_ANONYMOUS) {
        if (descriptor != -1 || offset) { errno = EINVAL; return MAP_FAILED; }
        if (type != MAP_PRIVATE) { errno = ENOTSUP; return MAP_FAILED; }
        result = __libc_os_mm_anonymous(size, protection, hint);
    } else {
        OpenDescription* description = libcDescriptor(descriptor);
        if (!description) { errno = EBADF; return MAP_FAILED; }
        if ((description->flags & O_ACCMODE) == O_WRONLY) { errno = EACCES; return MAP_FAILED; }
        if (description->kind == DESCRIPTOR_SHARED_MEMORY) {
            if (type != MAP_SHARED || ((protection & PROT_WRITE) && (description->flags & O_ACCMODE) != O_RDWR)) {
                errno = EACCES; return MAP_FAILED;
            }
            result = __libc_os_mm_shared(description->handle, size, protection, offset, hint);
        } else if (description->kind == DESCRIPTOR_FILE) {
            if (type != MAP_PRIVATE) { errno = ENOTSUP; return MAP_FAILED; }
            result = __libc_os_mm_file(description->handle, size, protection, offset, hint);
        } else { errno = ENODEV; return MAP_FAILED; }
    }
    if (result < 0) { mapping_error(result); return MAP_FAILED; }
    return (void*)(uintptr_t)result;
}

int munmap(void* address, size_t length) {
    const uint64_t value = (uint64_t)(uintptr_t)address, size = page_align(length);
    if (!value || (value & 4095) || !size) { errno = EINVAL; return -1; }
    const int result = __libc_os_mm_unmap(value, size);
    return result ? mapping_error(result) : 0;
}

int mprotect(void* address, size_t length, int protection) {
    const uint64_t value = (uint64_t)(uintptr_t)address, size = page_align(length);
    if (!value || (value & 4095) || !size || (protection & ~7)) { errno = EINVAL; return -1; }
    if ((protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) { errno = EACCES; return -1; }
    const int result = __libc_os_mm_protect(value, size, protection);
    return result ? mapping_error(result) : 0;
}

int msync(void* address, size_t length, int flags) {
    const uint64_t value = (uint64_t)(uintptr_t)address, size = page_align(length);
    if (!value || (value & 4095) || !size || (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) ||
        ((flags & MS_ASYNC) && (flags & MS_SYNC))) { errno = EINVAL; return -1; }
    const int result = __libc_os_mm_sync(value, size);
    return result ? mapping_error(result) : 0;
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
