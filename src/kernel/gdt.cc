#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace gdt {

std::uint64_t gdt_table[12];
TssEntry *tss;
Ptr gdtr;

void SetEntry(int index, std::uint64_t base, std::uint64_t limit,
              std::uint8_t access, std::uint8_t gran) {
    // Build descriptor from two 32-bit halves to avoid tricky 64-bit shifts
    std::uint32_t low = (std::uint32_t)((limit & 0xFFFF) | ((base & 0xFFFF) << 16));
    std::uint32_t high = (std::uint32_t)(((base >> 16) & 0xFF) | ((std::uint32_t)access << 8) |
                                         (((limit >> 16) & 0xF) << 16) | ((std::uint32_t)(gran & 0xF) << 20) |
                                         (((base >> 24) & 0xFF) << 24));

    gdt_table[index] = ((std::uint64_t)high << 32) | low;
    //tty::printk("GDT Entry %d set: base=0x%lx, limit=0x%lx, access=0x%x, gran=0x%x\n",
    //            index, base, limit, access, gran);
}

void SetTss(int index, std::uint64_t tss_base, std::uint8_t access) {
    std::uint64_t tss_limit = sizeof(TssEntry) - 1;

    std::uint32_t low = (std::uint32_t)((tss_limit & 0xFFFF) | ((tss_base & 0xFFFF) << 16));
    std::uint32_t high = (std::uint32_t)(((tss_base >> 16) & 0xFF) | ((std::uint32_t)access << 8) |
                                         (((tss_limit >> 16) & 0xF) << 16) | (0 << 20) |
                                         (((tss_base >> 24) & 0xFF) << 24));

    gdt_table[index]     = ((std::uint64_t)high << 32) | low;
    gdt_table[index + 1] = (tss_base >> 32) & 0xFFFFFFFFULL;
}


void Init() {
    memset(gdt_table, 0, sizeof(gdt_table));

    tss = reinterpret_cast<TssEntry*>(mm::page::Alloc(sizeof(TssEntry)));
    memset(tss, 0, sizeof(TssEntry));

    // Null descriptor
    SetEntry(0, 0, 0, 0, 0);

    // Code segment descriptor (64-bit, 0x08)
    // Access: 0x9A = present, ring 0, code, executable, readable
    SetEntry(1, 0, 0, GDT_PRESENT | GDT_DPL_RING0 | GDT_TYPE_CODE | GDT_TYPE_RW, 0x2);
    // Data segment descriptor (64-bit, 0x10)
    // Access: 0x92 = present, ring 0, data, writable
    SetEntry(2, 0, 0, GDT_PRESENT | GDT_DPL_RING0 | GDT_TYPE_DATA | GDT_TYPE_RW, 0x0);

    // User code segment descriptor (32-bit, 0x18) - leave empty
    SetEntry(3, 0, 0, 0, 0);
    // User data segment descriptor (32-bit, 0x20) - leave empty
    SetEntry(4, 0, 0, 0, 0);

    // User code segment descriptor (64-bit, 0x28)
    // Access: 0xFA = present, ring 3, code, executable, readable
    SetEntry(5, 0, 0, GDT_PRESENT | GDT_DPL_RING3 | GDT_TYPE_CODE | GDT_TYPE_RW, 0x2);
    // User data segment descriptor (64-bit, 0x30)
    // Access: 0xF2 = present, ring 3, data, writable
    SetEntry(6, 0, 0, GDT_PRESENT | GDT_DPL_RING3 | GDT_TYPE_DATA | GDT_TYPE_RW, 0x0);

    // Code segment descriptor (32-bit, 0x38)
    SetEntry(7, 0, 0xfffff, GDT_PRESENT | GDT_DPL_RING0 | GDT_TYPE_CODE | GDT_TYPE_RW, 0xc);
    // Data segment descriptor (32-bit, 0x40)
    SetEntry(8, 0, 0xfffff, GDT_PRESENT | GDT_DPL_RING0 | GDT_TYPE_DATA | GDT_TYPE_RW, 0);

    void *stack = mm::page::Alloc(STACK_SIZE);
    tss->rsp0 = reinterpret_cast<std::uint64_t>(stack) + STACK_SIZE;

    std::uint64_t tss_addr = reinterpret_cast<std::uint64_t>(tss);
    SetTss(10, tss_addr, 0x89);
    //tty::printk("gdt addr: 0x%lx, tss addr: 0x%lx\n", reinterpret_cast<std::uint64_t>(&gdt_table), tss_addr);
    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base  = reinterpret_cast<std::uint64_t>(&gdt_table);

    lgdt(&gdtr);

    // Load TSS
    ltr(10 * 8);
}
}  // namespace gdt