

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/tty.h"

namespace tty {

void Puts(const char *s, std::uint8_t color) {
    while (*s)
#if OUTPUT_TO_SERIAL == true
        serial::Putc(*s++);
#else
        video::Putchar(*s++, color);
#endif  // OUTPUT_TO_SERIAL
}

/*  Format a string and print it on the screen, just like the libc
function printf. */
int printk(const char *fmt, ...) {
    va_list argp;
    char str[512];
    int a;
    va_start(argp, fmt);
    a = vsnprintf(str, sizeof(str), fmt, argp);
    Puts(str, ATTRIBUTE);
    va_end(argp);
    return a;
}

void Panic(const char *fmt, ...) {
    va_list argp;
    char str[512];
    int a;
    va_start(argp, fmt);
    a = vsnprintf(str, sizeof(str), fmt, argp);
    Puts("KERNEL PANIC: ", 0x04);
    Puts(str, 0x04);
    va_end(argp);

    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}
}  // namespace tty
