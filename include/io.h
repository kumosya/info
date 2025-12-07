#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#endif // IO_H
