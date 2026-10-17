#ifndef _POSIX_SYS_SYSMACROS_H
#define _POSIX_SYS_SYSMACROS_H

#include <sys/types.h>

static inline unsigned int major(dev_t device) {
    return (unsigned int)(device >> 32);
}
static inline unsigned int minor(dev_t device) {
    return (unsigned int)(device & 0xffffffffU);
}
static inline dev_t makedev(unsigned int majorNumber,
                            unsigned int minorNumber) {
    return ((dev_t)majorNumber << 32) | minorNumber;
}

#endif
