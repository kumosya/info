#ifndef _POSIX_DEVCTL_H
#define _POSIX_DEVCTL_H

#include <stddef.h>

/* PTY controls use a fixed-width payload and a separately returned device status. */
#define OSINFO_DEVCTL_TTY_GET_SIZE 0x545901
#define OSINFO_DEVCTL_TTY_SET_SIZE 0x545902
struct osinfo_devctl_tty_size {
    unsigned int rows, columns, pixel_width, pixel_height;
};
/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int posix_devctl(int, int, void *__restrict, size_t, int *__restrict);
#ifdef __cplusplus
}
#endif

#endif
