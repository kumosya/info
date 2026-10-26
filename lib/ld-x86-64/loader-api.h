#pragma once
#include <stddef.h>
#include <stdint.h>
/* Private libc/interpreter handshake, not a plugin ABI. */
typedef struct OsDynamicLoader {
    uint32_t version, size;
    void* (*open)(const char*, int, char*, size_t);
    void* (*symbol)(void*, const char*, const void*, char*, size_t);
    int (*close)(void*, char*, size_t);
} OsDynamicLoader;
