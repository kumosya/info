#include "cpu.h"

#include <cstdint>
#include <string>

using namespace std;

namespace gdt {
uint64_t gdt_table[5];
gdt_ptr_t gdtr;

void set_entry(int index, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {
    uint64_t descriptor;

    // Create the high 32 bits of the descriptor
    descriptor = (limit & 0x000F0000ULL) >> 16; // Limit bits 16-19
    descriptor |= (uint64_t)(gran & 0x0F) << 20;          // Granularity
    descriptor |= (uint64_t)(access & 0xFF) << 40;        // Access byte
    descriptor |= (base & 0xFF000000ULL) >> 24; // Base bits 24-31

    // Create the low 32 bits of the descriptor
    descriptor |= (base & 0x00FFFFFFULL) << 16; // Base bits 0-23
    descriptor |= (limit & 0x0000FFFFULL);      // Limit bits 0-15

    gdt_table[index] = descriptor;
}
static void lgdt(gdt_ptr_t *gdtr) {
    asm volatile("lgdt %0" : : "m"(*gdtr));
    // Update segment registers
}
void init() {
    // Null descriptor
    gdt_table[0] = 0;
    // Code segment descriptor
    gdt_table[1] = 0x00AF9A000000FFFF;
    // Data segment descriptor
    gdt_table[2] = 0x00AF92000000FFFF;
    // User code segment descriptor
    gdt_table[3] = 0x00AFFA000000FFFF;
    // User data segment descriptor
    gdt_table[4] = 0x00AFF2000000FFFF;

    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base  = (uint64_t)&gdt_table;

    lgdt(&gdtr);
}
} // namespace gdt