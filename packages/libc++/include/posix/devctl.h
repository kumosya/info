#ifndef _POSIX_DEVCTL_H
#define _POSIX_DEVCTL_H

#include <stddef.h>

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
