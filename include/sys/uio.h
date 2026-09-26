#pragma once

#include <stddef.h>
#include <sys/types.h>

struct iovec {
    void* iov_base;
    size_t iov_len;
};

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
