#include "cpu.h"
#include <cstdint>
#include <string>

using namespace std;

namespace gdt {
uint64_t gdt_table[5];
gdt_ptr_t gdtr;

void set_entry(int index, uint8_t access, uint8_t gran) {
    uint64_t descriptor;

    // Create the high 32 bits of the descriptor
    descriptor = (0 & 0x000F0000ULL) >> 16; // Limit bits 16-19
    descriptor |= (uint64_t)(gran & 0x0F) << 52;          // Granularity
    descriptor |= (uint64_t)(access & 0xFF) << 40;        // Access byte
    descriptor |= (0 & 0xFF000000ULL) >> 24; // Base bits 24-31

    // Create the low 32 bits of the descriptor
    descriptor |= (0 & 0x00FFFFFFULL) << 16; // Base bits 0-23
    descriptor |= (0 & 0x0000FFFFULL);      // Limit bits 0-15

    gdt_table[index] = descriptor;
}
static void lgdt(gdt_ptr_t *gdtr) {
    asm volatile("lgdt %0" : : "m"(*gdtr));
    // Update segment registers
}
void init() {
    // Null descriptor
    set_entry(0, 0, 0);
    // Code segment descriptor
    set_entry(1, 0x9a, 0xa);
    // Data segment descriptor
    set_entry(2, 0x92, 0xa);
    // User code segment descriptor
    set_entry(3, 0xfa, 0xa);
    // User data segment descriptor
    set_entry(4, 0xf2, 0xa);

    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base  = (uint64_t)&gdt_table;

    lgdt(&gdtr);
}
} // namespace gdt