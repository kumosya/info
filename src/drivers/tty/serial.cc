#include <cstdint>

#include "kernel/io.h"

using namespace std;

namespace serial {
void Init() {
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

static inline int IsTransmitEmpty() { return inb(COM1_PORT + 5) & 0x20; }

void Putc(char c) {
    while (!IsTransmitEmpty());
    outb(COM1_PORT, static_cast<std::uint8_t>(c));
}

void Write(const char *s) {
    while (*s) {
        if (*s == '\n') Putc('\r');
        Putc(*s++);
    }
}
}  // namespace serial
