#include <stdio.h>
#include <stdarg.h>
#include <kernel/syscall.h>

int putchar(int c) {
    MESSAGE msg;
    msg.num[0] = c;
    msgSend(SYS_CHAR, SYS_CHAR_PUTCHAR, &msg);
    return c;
}

int puts(const char *str) {
    int ret = 0;
    while (*str) {
        putchar(*str++);
        ret ++;
    }
    ret += putchar('\n');
    return ret;
}

int printf(const char *fmt, ...) {
    char buf[256] = {0};
    va_list args;
    va_start(args, fmt);
    int ret = vsprintf(buf, fmt, args);
    va_end(args);
    
    char *p = buf;
    while (*p) {
        putchar(*p++);
    }

    return ret;
}
