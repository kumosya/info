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
        if (description->kind == DESCRIPTOR_TYPED_MEMORY) {
            if(type!=MAP_SHARED || offset) {errno=EINVAL;return MAP_FAILED;}
            if((protection&PROT_WRITE) && (description->flags&O_ACCMODE)!=O_RDWR) {errno=EACCES;return MAP_FAILED;}
            const int64_t backing=__libc_os_typed_allocate(description->handle,size);
            if(backing<=0) {mapping_error(backing);return MAP_FAILED;}
            result=__libc_os_mm_shared(backing,size,protection,0,hint);
            __libc_os_close_handle(backing);
        } else if (description->kind == DESCRIPTOR_SHARED_MEMORY) {
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

static int residency_range(const void* address,size_t length,unsigned operation,int advice) {
    const uint64_t begin=(uint64_t)(uintptr_t)address;
    if(!length)return 0;
    if(begin>UINT64_MAX-length || begin+length>UINT64_MAX-4095)return EINVAL;
    const uint64_t first=begin&~4095ULL,end=(begin+length+4095)&~4095ULL;
    return __libc_os_mm_policy(operation,first,end-first,(uint64_t)advice);
}
int mlock(const void* address,size_t size) {
    const int error=residency_range(address,size,0,0);
    if(error){errno=error;return -1;}return 0;
}
int munlock(const void* address,size_t size) {
    const int error=residency_range(address,size,1,0);
    if(error){errno=error;return -1;}return 0;
}
int mlockall(int flags) {
    if(!flags || (flags&~(MCL_CURRENT|MCL_FUTURE))){errno=EINVAL;return -1;}
    const int error=__libc_os_mm_policy(2,flags,0,0);
    if(error){errno=error;return -1;}return 0;
}
int munlockall(void) {
    const int error=__libc_os_mm_policy(3,0,0,0);
    if(error){errno=error;return -1;}return 0;
}
int posix_madvise(void* address,size_t length,int advice) {
    const int saved=errno;
    int error;
    if(advice<POSIX_MADV_NORMAL || advice>POSIX_MADV_DONTNEED || ((uintptr_t)address&4095))error=EINVAL;
    else error=residency_range(address,length,4,advice);
    errno=saved;return error;
}

int posix_mem_offset(const void* address,size_t length,off_t* offset,
                        size_t* contiguous,int* descriptor) {
    const int saved=errno;
    if(!offset || !contiguous || !descriptor || !__libc_os_accessible(offset,sizeof(*offset),1) ||
       !__libc_os_accessible(contiguous,sizeof(*contiguous),1) || !__libc_os_accessible(descriptor,sizeof(*descriptor),1))return EFAULT;
    const uint64_t begin=(uint64_t)(uintptr_t)address;
    if(!length || begin>UINT64_MAX-length)return EINVAL;
    uint64_t objectOffset=0,bytes=0;unsigned rights=0;
    const int64_t handle=__libc_os_mm_offset(begin,length,&objectOffset,&bytes,&rights);
    if(handle<=0) {mapping_error(handle);const int error=errno;errno=saved;return error;}
    if(objectOffset>INT64_MAX) {__libc_os_close_handle(handle);errno=saved;return EOVERFLOW;}
    const int result=libcAllocateDescriptor(handle,"",(rights&2)?O_RDWR:O_RDONLY,DESCRIPTOR_SHARED_MEMORY);
    if(result<0) {const int error=errno;__libc_os_close_handle(handle);errno=saved;return error;}
    libcDescriptors[result].closeOnExec=1;
    *offset=(off_t)objectOffset;*contiguous=(size_t)bytes;*descriptor=result;
    errno=saved;return 0;
}

int posix_typed_mem_open(const char* name,int flags,int policy) {
    if(!name || (flags&~(O_ACCMODE|O_CLOEXEC)) ||
       ((flags&O_ACCMODE)!=O_RDONLY && (flags&O_ACCMODE)!=O_RDWR) ||
       (policy&~(POSIX_TYPED_MEM_ALLOCATE|POSIX_TYPED_MEM_ALLOCATE_CONTIG|POSIX_TYPED_MEM_MAP_ALLOCATABLE)) ||
       ((policy&POSIX_TYPED_MEM_ALLOCATE) && (policy&POSIX_TYPED_MEM_ALLOCATE_CONTIG))) {
        errno=EINVAL;return -1;
    }
    const int64_t handle=__libc_os_typed_open(name,flags,policy);
    if(handle<=0)return mapping_error(handle);
    const int descriptor=libcAllocateDescriptor(handle,name,flags,DESCRIPTOR_TYPED_MEMORY);
    if(descriptor<0) {__libc_os_close_handle(handle);return -1;}
    libcDescriptors[descriptor].closeOnExec=(flags&O_CLOEXEC)!=0;
    return descriptor;
}
int posix_typed_mem_get_info(int descriptor,struct posix_typed_mem_info* info) {
    if(!info || !__libc_os_accessible(info,sizeof(*info),1)) {errno=EFAULT;return -1;}
    OpenDescription* description=libcDescriptor(descriptor);
    if(!description) {errno=EBADF;return -1;}
    if(description->kind!=DESCRIPTOR_TYPED_MEMORY) {errno=EINVAL;return -1;}
    uint64_t available=0;const int status=__libc_os_typed_info(description->handle,&available);
    if(status)return mapping_error(status);
    info->posix_tmi_length=available;return 0;
}
