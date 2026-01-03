#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace gdt {

std::uint64_t gdt_table[12];
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
    // 64位TSS的大小是sizeof(TssEntry) - 1 (其实就是0x67)
    std::uint64_t tss_limit = sizeof(TssEntry) - 1;
    
    std::uint64_t descriptor = 0;

    // x86-64 TSS描述符格式（低64位）：
    // bits 0-15: limit[15:0]
    // bits 16-31: base[15:0]  
    // bits 32-39: access byte (type attributes)
    // bits 40-43: limit[19:16]
    // bits 44-47: flags (G=granularity, AVL, etc)
    // bits 48-55: base[23:16]
    // bits 56-63: base[31:24]
    
    descriptor |= (tss_limit & 0xFFFFULL);                           // Limit bits 0-15
    descriptor |= (tss_base & 0xFFFFULL) << 16;                      // Base bits 0-15
    descriptor |= static_cast<std::uint64_t>(access & 0xFF) << 40;   // Access byte (bits 32-39)
    descriptor |= ((tss_limit >> 16) & 0xFULL) << 48;               // Limit bits 16-19 (bits 40-43)
    // flags (bits 44-47): G=0 (byte granularity), AVL=0, L=0, D/B=0
    descriptor |= (tss_base & 0xFF0000ULL) << 32;                    // Base bits 16-23 (bits 48-55)
    descriptor |= (tss_base & 0xFF000000ULL) << 32;                 // Base bits 24-31 (bits 56-63)

    gdt_table[index]     = descriptor;
    gdt_table[index + 1] = tss_base >> 32;  // Base bits 32-63
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
    tss.rsp0 = reinterpret_cast<std::uint64_t>(stack) + STACK_SIZE;

    std::uint64_t tss_addr = reinterpret_cast<std::uint64_t>(&tss);
    SetTss(10, tss_addr, 0x89);

    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base  = reinterpret_cast<std::uint64_t>(&gdt_table);

    lgdt(&gdtr);

    // Load TSS
    ltr(10 * 8);
}
}  // namespace gdt