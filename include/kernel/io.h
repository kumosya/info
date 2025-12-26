#ifndef IO_H
#define IO_H

#include <cstdint>
#include "cpu.h"

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

static inline void outb(std::uint16_t port, std::uint8_t val) {
    asm __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t ret;
    asm __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(std::uint16_t port, std::uint16_t val) {
    asm __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline std::uint16_t inw(std::uint16_t port) {
    std::uint16_t ret;
    asm __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void wrmsr(std::uint32_t msr, std::uint64_t val) {
    asm __volatile__("wrmsr" : : "c"(msr), "a"(val >> 32), "d"(val));
}

static inline std::uint64_t rdmsr(std::uint32_t msr) {
    std::uint64_t ret;
    asm __volatile__("rdmsr" : "=a"(ret), "=d"(ret) : "c"(msr));
    return ret;
}

inline static void lgdt(gdt::Ptr *gdtr) {
    asm volatile ("lgdt %0" : : "m"(*gdtr));
    // Update segment registers
}

inline static void ltr(std::uint16_t selector) {
    asm volatile ("ltr %0" : : "r"(selector));
}

namespace serial {
void Init();
void Putc(char c);
void Write(const char *s);
}  // namespace serial

namespace pic {
void Remap(int offset1, int offset2);
void Mask(std::uint8_t master_mask, std::uint8_t slave_mask);
void Init();
void MaskIrq(int irq);
void UnmaskIrq(int irq);
}  // namespace pic

namespace timer {
extern volatile std::uint64_t pit_ticks;
void Init(std::uint32_t freq);
std::uint64_t GetTicks();
}  // namespace timer

#endif  // IO_H
