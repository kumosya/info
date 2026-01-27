#ifndef INFO_KERNEL_IO_H_
#define INFO_KERNEL_IO_H_

#include <cstdint>

#include "kernel/cpu.h"

#define TIMER_FREQUENCY 100
#define TIMER_PERIOD (1000 / TIMER_FREQUENCY)

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
    __asm__ __volatile__("wrmsr	\n\t" ::"d"(val >> 32), "a"(val & 0xffffffff),
                         "c"(msr)
                         : "memory");
}

static inline std::uint64_t rdmsr(std::uint32_t msr) {
    std::uint32_t tmp0 = 0;
    std::uint32_t tmp1 = 0;
    __asm__ __volatile__("rdmsr	\n\t"
                         : "=d"(tmp0), "=a"(tmp1)
                         : "c"(msr)
                         : "memory");
    return static_cast<std::uint64_t>(tmp0) << 32 | tmp1;
}

static inline void cpuid(std::uint32_t func, std::uint32_t &eax,
                         std::uint32_t &ebx, std::uint32_t &ecx,
                         std::uint32_t &edx) {
    asm __volatile__("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(func), "c"(0));
}

static inline void cpuid_ext(std::uint32_t func, std::uint32_t subfunc,
                             std::uint32_t &eax, std::uint32_t &ebx,
                             std::uint32_t &ecx, std::uint32_t &edx) {
    asm __volatile__("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(func), "c"(subfunc));
}

inline static void lgdt(gdt::Ptr *gdtr) {
    asm volatile("lgdt %0" : : "m"(*gdtr));
    // Update segment registers
}

inline static void ltr(std::uint16_t selector) {
    asm volatile("ltr %0" : : "r"(selector));
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

#endif  // INFO_KERNEL_IO_H_
