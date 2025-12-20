#ifndef IO_H
#define IO_H

#include <cstdint>

using namespace std;

/* COM1 port base */
#define COM1_PORT 0x3F8

/* 8259 PIC ports */
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

/* ICW control words */
#define ICW1_INIT 0x11
#define ICW4_8086 0x01

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_FREQ 1193182UL

static inline void outb(uint16_t port, uint8_t val) { asm __volatile__("outb %0, %1" : : "a"(val), "Nd"(port)); }

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint8_t val) { asm __volatile__("outb %0, %1" : : "a"(val), "Nd"(port)); }

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

namespace serial {
void init();
void putc(char c);
void write(const char *s);
} // namespace serial

namespace pic {
void remap(int offset1, int offset2);
void mask(uint8_t master_mask, uint8_t slave_mask);
void init();
void mask_irq(int irq);
void unmask_irq(int irq);
} // namespace pic

namespace timer {
extern volatile uint64_t pit_ticks;
void init(uint32_t freq);
uint64_t ticks();
} // namespace timer

#endif // IO_H
