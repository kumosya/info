#ifndef _POSIX_SYS_MMAN_H
#define _POSIX_SYS_MMAN_H

#include <stddef.h>
struct posix_typed_mem_info {
    size_t posix_tmi_length;
};

#include <sys/types.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

#define POSIX_TYPED_MEM_ALLOCATE 1
#define POSIX_TYPED_MEM_ALLOCATE_CONTIG 2
#define POSIX_TYPED_MEM_MAP_ALLOCATABLE 4

#define MCL_CURRENT 1
#define MCL_FUTURE 2
#define POSIX_MADV_NORMAL 0
#define POSIX_MADV_RANDOM 1
#define POSIX_MADV_SEQUENTIAL 2
#define POSIX_MADV_WILLNEED 3
#define POSIX_MADV_DONTNEED 4

#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *address, size_t length, int protection, int flags,
           int file_descriptor, off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);
int msync(void *address, size_t length, int flags);
int shm_open(const char *name, int flags, mode_t mode);
int shm_unlink(const char *name);

#ifdef __cplusplus
}
#endif

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int mlock(const void *, size_t);
int mlockall(int);
int munlock(const void *, size_t);
int munlockall(void);
int posix_madvise(void *, size_t, int);
int posix_mem_offset(const void *__restrict, size_t, off_t *__restrict,
                     size_t *__restrict, int *__restrict);
int posix_typed_mem_get_info(int, struct posix_typed_mem_info *);
int posix_typed_mem_open(const char *, int, int);
#ifdef __cplusplus
}
#endif

#endif
