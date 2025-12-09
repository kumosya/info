#include "cpu.h"
#include "mm.h"
#include "tty.h"

#include <cstdint>
#include <cstring>
using namespace std;

// Stubs implemented in interrupt.S
extern "C" void pit_stub();
extern "C" void kbd_stub();
extern "C" void gp_stub();
extern "C" void pf_stub();
extern "C" void default_handler();

// default_handler implemented in interrupt.S
extern "C" void default_handler(void);

static idt_entry idt_table[256];

// General protection handler: receive faulting RIP in RDI
extern "C" void gp_fault_handler(uint64_t rip, uint64_t rsp) {
    tty::printf("#GP Fault! RIP=0x%lx RSP=0x%lx\n", rip, rsp);
    while (true) {
        asm volatile("hlt");
    }
}

// Page fault handler: RDI = CR2 (faulting linear address), RSI = faulting RIP
extern "C" void page_fault_handler(uint64_t fault_addr, uint64_t rip) {
    uint64_t cr3, rsp;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    tty::printf("Page Fault! CR2=0x%lx RIP=0x%lx RSP=0x%lx CR3=0x%lx\n", fault_addr, rip, rsp, cr3);
    while (true) {
        asm volatile("hlt");
    }
}

namespace idt {
void init() {
    memset(&idt_table, 0, sizeof(idt_table));

    // Set up minimal IDT with page fault handler
    for (int i = 0; i < 256; i++) {
        set_entry(i, nullptr, 0x08, 0x8E); // Null handler
    }
    set_entry(0, (void *)default_handler, 0x08, 0x8E); // Divide by zero
    set_entry(1, (void *)default_handler, 0x08, 0x8E); // Debug
    set_entry(2, (void *)default_handler, 0x08, 0x8E); // NMI
    set_entry(3, (void *)default_handler, 0x08, 0x8E); // Breakpoint
    set_entry(4, (void *)default_handler, 0x08, 0x8E); // Overflow
    set_entry(5, (void *)default_handler, 0x08, 0x8E); // BOUND Range Exceeded
    set_entry(6, (void *)default_handler, 0x08, 0x8E); // Invalid Opcode
    set_entry(7, (void *)default_handler, 0x08, 0x8E); // Device Not Available
    set_entry(8, (void *)default_handler, 0x08, 0x8E); // Double Fault

    set_entry(10, (void *)default_handler, 0x08, 0x8E); // Invalid TSS
    set_entry(11, (void *)default_handler, 0x08, 0x8E); // Segment Not Present
    set_entry(12, (void *)default_handler, 0x08, 0x8E); // Stack-Segment Fault

    set_entry(13, (void *)gp_stub, 0x08, 0x8E); // General Protection Fault
    set_entry(14, (void *)pf_stub, 0x08, 0x8E); // Page Fault

    set_entry(16, (void *)default_handler, 0x08, 0x8E); // x87 Floating-Point Exception
    set_entry(17, (void *)default_handler, 0x08, 0x8E); // Alignment Check
    set_entry(18, (void *)default_handler, 0x08, 0x8E); // Machine Check
    set_entry(19, (void *)default_handler, 0x08, 0x8E); // SIMD Floating-Point Exception

    // Timer IRQ0 (PIC remapped to 0x20)
    set_entry(0x20, (void *)pit_stub, 0x08, 0x8E);
    // Keyboard IRQ1 (PIC remapped to 0x21)
    set_entry(0x21, (void *)kbd_stub, 0x08, 0x8E);

    idt_ptr_t idt_ptr;
    idt_ptr.limit = sizeof(idt_entry) * 256 - 1;
    idt_ptr.base  = (uint64_t)&idt_table;

    asm volatile("lidt %0" : : "m"(idt_ptr));
}

static void set_entry(int vec, void *handler, uint16_t sel, uint8_t type_attr) {
    uint64_t addr              = (uint64_t)handler;
    idt_table[vec].offset_low  = addr & 0xFFFF;
    idt_table[vec].selector    = sel;
    idt_table[vec].ist         = 0;
    idt_table[vec].type_attr   = type_attr;
    idt_table[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt_table[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt_table[vec].zero        = 0;
}

} // namespace idt
