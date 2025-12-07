#ifndef _TTY_H_
#define _TTY_H_

#include <stdint.h>
#include <stdarg.h>

/* Here! */
#define OUTPUT_TO_SERIAL

namespace tty {
    namespace video {
        static uint8_t *video_addr;
        void init();
        void putchar (char c, uint8_t color);
    }
    static void puts (const char *s, uint8_t color);
    int printf (const char *format, ...);
    void panic(const char *format, ...);
}
#endif /* _VIDEO_H_ */