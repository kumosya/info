

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
    char str[128];
    int a;
    va_start(argp, fmt);          /*开始使用可变参数*/
    a = vsprintf(str, fmt, argp); /*格式化输出*/
    Puts(str, ATTRIBUTE);         /*输出格式化后的字符串*/
    va_end(argp);                 /*停止使用可变参数*/
    return a;
}

void Panic(const char *fmt, ...) {
    va_list argp;
    char str[128];
    int a;
    va_start(argp, fmt);          /*开始使用可变参数*/
    a = vsprintf(str, fmt, argp); /*格式化输出*/
    Puts("KERNEL PANIC: ", 0x04);
    Puts(str, 0x04); /*输出格式化后的字符串*/
    va_end(argp);    /*停止使用可变参数*/

    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}
}  // namespace tty
