#include "kernel/serial.h"

namespace {

constexpr uint16_t serialPort = 0x3f8;

inline void writePort(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t readPort(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

}  // namespace

namespace kernel::serial {

void initSerial() {
    writePort(serialPort + 1, 0x00);
    writePort(serialPort + 3, 0x80);
    writePort(serialPort + 0, 0x01);
    writePort(serialPort + 1, 0x00);
    writePort(serialPort + 3, 0x03);
    writePort(serialPort + 2, 0xc7);
    writePort(serialPort + 4, 0x0b);
}

void serialWrite(char value) {
    for (uint32_t timeout = 0; timeout < 100000; ++timeout) {
        if ((readPort(serialPort + 5) & 0x20) != 0) {
            break;
        }
    }
    writePort(serialPort, static_cast<uint8_t>(value));
}

void serialWrite(const char* value) {
    if (value == nullptr) {
        return;
    }
    while (*value != '\0') {
        if (*value == '\n') {
            serial::serialWrite('\r');
        }
        serial::serialWrite(*value++);
    }
}

void serialWriteHex(uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    serial::serialWrite("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial::serialWrite(digits[(value >> shift) & 0xf]);
    }
}

void serialWriteDecimal(uint64_t value) {
    char digits[20];
    uint32_t count = 0;
    if (value == 0) {
        serial::serialWrite('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) {
        serial::serialWrite(digits[--count]);
    }
}

}  // namespace kernel::serial
