#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/* COM1 port base */
#define COM1_PORT 0x3F8

namespace serial {
    void init();
    void putc(char c);
    void write(const char *s);
}

#endif // SERIAL_H
