#include <errno.h>

#include "os/c_api.h"

int __libc_posix_not_implemented(uint64_t operation) {
    os_posix_unsupported(operation);
    errno = ENOSYS;
    return -1;
}
