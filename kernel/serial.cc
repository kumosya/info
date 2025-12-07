#include <serial.h>
#include <io.h>

namespace serial {
    void init() {
        // Disable interrupts
        outb(COM1_PORT + 1, 0x00);
        // Enable DLAB (set baud rate divisor)
        outb(COM1_PORT + 3, 0x80);
        // Set divisor to 1 (115200 baud)
        outb(COM1_PORT + 0, 0x01);
        outb(COM1_PORT + 1, 0x00);
        // 8 bits, no parity, one stop bit
        outb(COM1_PORT + 3, 0x03);
        // Enable FIFO, clear them, with 14-byte threshold
        outb(COM1_PORT + 2, 0xC7);
        // IRQs enabled, RTS/DSR set
        outb(COM1_PORT + 4, 0x0B);
    }

    static inline int is_transmit_empty() {
        return inb(COM1_PORT + 5) & 0x20;
    }

    void putc(char c) {
        while (!is_transmit_empty()) ;
        outb(COM1_PORT, (uint8_t)c);
    }

    void write(const char *s) {
        while (*s) {
            if (*s == '\n') putc('\r');
            putc(*s++);
        }
    }
}
