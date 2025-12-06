#include <video.h>
#include <mm.h>
#include <entry.h>
#include <idt.h>

#include <stdint.h>
#include <string.h>

static idt_entry idt_table[256];

// General protection handler: receive faulting RIP in RDI
extern "C" void gp_fault_handler(uint64_t rip) {
    video::printf("#GP Fault! RIP=0x%lx\n", rip);
    while (true) { asm volatile("hlt"); }
}

// Page fault handler: RDI = CR2 (faulting linear address), RSI = faulting RIP
extern "C" void page_fault_handler(uint64_t fault_addr, uint64_t rip) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r" (cr3));
    video::printf("Page Fault! CR2=0x%lx RIP=0x%lx CR3=0x%lx\n", fault_addr, rip, cr3);
    while (true) { asm volatile("hlt"); }
}

void gp_stub(void) {
    asm __volatile__ (
        "    cli\n"
        "    /* load faulting RIP (pushed by CPU) into RDI */\n"
        "    mov (%rsp), %rdi\n"
        "    /* call the C handler; it will not return */\n"
        "    call gp_fault_handler\n"
    );
}

void pf_stub(void) {
    asm __volatile__ (
        "    cli\n"
        "    /* read CR2 into RDI (first arg) */\n"
        "    mov %cr2, %rdi\n"
        "    /* load faulting RIP (pushed by CPU) into RSI (second arg) */\n"
        "    mov (%rsp), %rsi\n"
        "    /* call the C handler; it will not return */\n"
        "    call page_fault_handler\n"
    );
}

void default_handler(void) {
    asm __volatile__ (
        "    cli\n"
        "1:\t hlt\n"
        "    jmp 1b\n"
    );
}

namespace idt {
    void init() {
		//memset(&idt_table, 0, sizeof(idt_table));

        // Set up minimal IDT with page fault handler
        for (int i = 0; i < 256; i++) {
            set_entry(i, nullptr, 0x08, 0x8E); // Null handler
        }
        set_entry(0, (void *)default_handler, 0x08, 0x8E);  // Divide by zero
        set_entry(1, (void *)default_handler, 0x08, 0x8E);  // Debug
        set_entry(2, (void *)default_handler, 0x08, 0x8E);  // NMI
        set_entry(3, (void *)default_handler, 0x08, 0x8E);  // Breakpoint
        set_entry(4, (void *)default_handler, 0x08, 0x8E);  // Overflow
        set_entry(5, (void *)default_handler, 0x08, 0x8E);  // BOUND Range Exceeded
        set_entry(6, (void *)default_handler, 0x08, 0x8E);  // Invalid Opcode
        set_entry(7, (void *)default_handler, 0x08, 0x8E);  // Device Not Available
        set_entry(8, (void *)default_handler, 0x08, 0x8E);  // Double Fault

        set_entry(10, (void *)default_handler, 0x08, 0x8E); // Invalid TSS
        set_entry(11, (void *)default_handler, 0x08, 0x8E); // Segment Not Present
        set_entry(12, (void *)default_handler, 0x08, 0x8E); // Stack-Segment Fault

        set_entry(13, (void*)gp_stub, 0x08, 0x8E); // General Protection Fault
        set_entry(14, (void*)pf_stub, 0x08, 0x8E); // Page Fault

        set_entry(16, (void *)default_handler, 0x08, 0x8E); // x87 Floating-Point Exception
        set_entry(17, (void *)default_handler, 0x08, 0x8E); // Alignment Check
        set_entry(18, (void *)default_handler, 0x08, 0x8E); // Machine Check
        set_entry(19, (void *)default_handler, 0x08, 0x8E); // SIMD Floating-Point Exception

        idt_ptr_t idt_ptr;
        idt_ptr.limit = sizeof(idt_entry) * 256 - 1;
        idt_ptr.base = (uint64_t)&idt_table;

        asm volatile("lidt %0" : : "m"(idt_ptr));

        video::printf("IDT initialized.\n");
    }

    static void set_entry(int vec, void* handler, uint16_t sel, uint8_t type_attr) {
        uint64_t addr = (uint64_t)handler;
        idt_table[vec].offset_low = addr & 0xFFFF;
        idt_table[vec].selector = sel;
        idt_table[vec].ist = 0;
        idt_table[vec].type_attr = type_attr;
        idt_table[vec].offset_mid = (addr >> 16) & 0xFFFF;
        idt_table[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
        idt_table[vec].zero = 0;
    }

}