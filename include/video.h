#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <stdint.h>
#include <stdarg.h>
namespace video {
    static uint8_t *video_addr;
    void init();
    void putchar (char c);
    int printf (const char *format, ...);
    static void puts (const char *s);
    static int vsprintf (char *buf, const char *fmt, va_list args);
}
#endif /* _VIDEO_H_ */