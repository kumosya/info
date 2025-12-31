#include <cstdint>
#include <cstring>

#include "cpu.h"
#include "io.h"
#include "mm.h"
#include "task.h"

namespace gdt {

std::uint64_t gdt_table[11];
TssEntry tss;
Ptr gdtr;

void SetEntry(int index, std::uint64_t base, std::uint64_t limit,
              std::uint8_t access, std::uint8_t gran) {
    std::uint64_t descriptor;

    // Create the high 32 bits of the descriptor
    descriptor = (limit & 0x000F0000ULL) >> 16;  // Limit bits 16-19
    descriptor |= static_cast<std::uint64_t>(gran & 0x0F) << 52;  // Granularity
    descriptor |= static_cast<std::uint64_t>(access & 0xFF)
                  << 40;                         // Access byte
    descriptor |= (base & 0xFF000000ULL) >> 24;  // Base bits 24-31

    // Create the low 32 bits of the descriptor
    descriptor |= (base & 0x00FFFFFFULL) << 16;  // Base bits 0-23
    descriptor |= (limit & 0x0000FFFFULL);       // Limit bits 0-15

    gdt_table[index] = descriptor;
}

void SetTss(int index, std::uint64_t tss_base, std::uint8_t access) {
    std::uint64_t descriptor;

    // Create the high 32 bits of the descriptor
    descriptor |= static_cast<std::uint64_t>(access & 0xFF)
                  << 40;                             // Access byte
    descriptor |= (tss_base & 0xFF000000ULL) >> 24;  // Base bits 24-31

    // Create the low 32 bits of the descriptor
    descriptor |= (tss_base & 0x00FFFFFFULL) << 16;  // Base bits 0-23

    descriptor += 103;

    gdt_table[index]     = descriptor;
    gdt_table[index + 1] = tss_base >> 32;
}

void Init() {
    memset(gdt_table, 0, sizeof(gdt_table));
    memset(&tss, 0, sizeof(TssEntry));

    // Null descriptor
    SetEntry(0, 0, 0, 0, 0);

    // Code segment descriptor (64-bit, 0x08)
    SetEntry(1, 0, 0, 0x98, 0xa);
    // Data segment descriptor (64-bit, 0x10)
    SetEntry(2, 0, 0, 0x92, 0);

    // User code segment descriptor (32-bit, 0x18)
    SetEntry(3, 0, 0, 0, 0);
    // User data segment descriptor (32-bit, 0x20)
    SetEntry(4, 0, 0, 0, 0);

    // User code segment descriptor (64-bit, 0x28)
    SetEntry(5, 0, 0, 0xf8, 0xa);
    // User data segment descriptor (64-bit, 0x30)
    SetEntry(6, 0, 0, 0xf2, 0);

    // Code segment descriptor (32-bit, 0x38)
    SetEntry(7, 0, 0xfffff, 0x9a, 0xc);
    // Data segment descriptor (32-bit, 0x40)
    SetEntry(8, 0, 0xfffff, 0x92, 0);

    void *stack = mm::page::Alloc(STACK_SIZE);
    tss.rsp0 = tss.rsp1 = tss.rsp2 =
        reinterpret_cast<std::uint64_t>(stack) + STACK_SIZE;

    SetTss(9, reinterpret_cast<std::uint64_t>(&tss), 0x89);

    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base  = reinterpret_cast<std::uint64_t>(&gdt_table);

    lgdt(&gdtr);

    // Load TSS
    ltr(9 * 8);
}
}  // namespace gdt