#ifndef _POSIX_SYS_UIO_H
#define _POSIX_SYS_UIO_H


#include <stddef.h>
#include <sys/types.h>

#ifndef __DEFINED_struct_iovec
struct iovec {
    void* iov_base;
    size_t iov_len;
};
#define __DEFINED_struct_iovec
#endif

#ifdef __cplusplus
extern "C" {
#endif
ssize_t readv(int fileDescriptor, const struct iovec* vectors,
              int vectorCount);
ssize_t writev(int fileDescriptor, const struct iovec* vectors,
               int vectorCount);
#ifdef __cplusplus
}
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_struct_iovec

#endif
