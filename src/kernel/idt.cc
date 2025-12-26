#include <cstdint>
#include <cstring>

#include "cpu.h"
#include "mm.h"
#include "tty.h"

extern "C" void de_fault_handler(faultStack_nocode *stack) {
    tty::printf("#DE Divide Fault!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void debug_trap_handler(faultStack_nocode *stack) {
    tty::printf("#DB Debug.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void nmi_handler(faultStack_nocode *stack) {
    tty::printf("NMI Interrupt.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void bp_trap_handler(faultStack_nocode *stack) {
    tty::printf("Breakpoint.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void of_trap_handler(faultStack_nocode *stack) {
    tty::printf("Overflow.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void br_fault_handler(faultStack_nocode *stack) {
    tty::printf("#BR BOUND Range Exceeded!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ud_fault_handler(faultStack_nocode *stack) {
    tty::printf("#UD Invalid Opcode!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void nm_fault_handler(faultStack_nocode *stack) {
    tty::printf("#NM Device Not Available!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void double_fault_handler(faultStack_nocode *stack) {
    tty::printf("#DF Double Fault!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void tss_fault_handler(faultStack_code *stack) {
    tty::printf("#TS Invalid TSS Fault! Error code = 0x%lx\n", stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void np_fault_handler(faultStack_code *stack) {
    tty::printf("#NP Segment Not Present! Error code = 0x%lx\n", stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ss_fault_handler(faultStack_code *stack) {
    tty::printf("Segment Fault.\nError code = 0x%lx,", stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void gp_fault_handler(faultStack_code *stack) {
    tty::printf("#GP General Protection Fault! Error code = 0x%lx\n", stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void page_fault_handler(faultStack_code *stack) {
    std::uint64_t cr3, cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    tty::printf("Page Fault. Error code = 0x%lx\n", stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx CR2=0x%lx\n", stack->rflags, cr2);
    tty::printf(" CR3=0x%lx\n", cr3);
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void mf_fault_handler(faultStack_nocode *stack) {
    tty::printf("#MF x87 FPU Floating-Point Error!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ac_fault_handler(faultStack_nocode *stack) {
    tty::printf("#AC Alignment Check.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void mc_abort_handler(faultStack_nocode *stack) {
    tty::printf("#MC Machine Check.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void xm_fault_handler(faultStack_nocode *stack) {
    tty::printf("#XM SIMD Floating-Point Exception!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ve_fault_handler(faultStack_nocode *stack) {
    tty::printf("#VE Virtualization Exception!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void cp_fault_handler(faultStack_code *stack) {
    tty::printf("#CP Control Protection Exception!");
    tty::printf(" Error code=0x%lx\n", stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);

    while (true) {
        asm volatile("hlt");
    }
}

namespace idt {

static Entry idt_table[256];
Ptr idt_ptr;

static void SetEntry(int vec, void *handler, std::uint16_t sel, std::uint8_t type_attr) {
    std::uint64_t addr         = reinterpret_cast<std::uint64_t>(handler);
    idt_table[vec].offset_low  = addr & 0xFFFF;
    idt_table[vec].selector    = sel;
    idt_table[vec].ist         = 0;
    idt_table[vec].type_attr   = type_attr;
    idt_table[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt_table[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt_table[vec].zero        = 0;
}

void Init() {
    memset(&idt_table, 0, sizeof(Entry));

    // Set up minimal IDT with page fault handler
    for (int i = 0; i < 256; i++) {
        SetEntry(i, nullptr, 0x08, 0x8E);  // Null handler
    }
    SetEntry(0, (void *)de_stub, 0x08, 0x8E);     // Divide by zero
    SetEntry(1, (void *)debug_stub, 0x08, 0x8E);  // Debug
    SetEntry(2, (void *)nmi_stub, 0x08, 0x8E);    // NMI
    SetEntry(3, (void *)bp_stub, 0x08, 0x8E);     // Breakpoint
    SetEntry(4, (void *)of_stub, 0x08, 0x8E);     // Overflow
    SetEntry(5, (void *)br_stub, 0x08, 0x8E);     // BOUND Range Exceeded
    SetEntry(6, (void *)ud_stub, 0x08, 0x8E);     // Invalid Opcode
    SetEntry(7, (void *)nm_stub, 0x08, 0x8E);     // Device Not Available
    SetEntry(8, (void *)df_stub, 0x08, 0x8E);     // Double Fault

    SetEntry(10, (void *)ts_stub, 0x08, 0x8E);  // Invalid TSS
    SetEntry(11, (void *)np_stub, 0x08, 0x8E);  // Segment Not Present
    SetEntry(12, (void *)ss_stub, 0x08, 0x8E);  // Stack-Segment Fault

    SetEntry(13, (void *)gp_stub, 0x08, 0x8E);  // General Protection Fault
    SetEntry(14, (void *)pf_stub, 0x08, 0x8E);  // Page Fault

    SetEntry(16, (void *)mf_stub, 0x08, 0x8E);  // x87 Floating-Point Exception
    SetEntry(17, (void *)ac_stub, 0x08, 0x8E);  // Alignment Check
    SetEntry(18, (void *)mc_stub, 0x08, 0x8E);  // Machine Check
    SetEntry(19, (void *)xm_stub, 0x08, 0x8E);  // SIMD Floating-Point Exception

    SetEntry(19, (void *)ve_stub, 0x08, 0x8E);
    SetEntry(19, (void *)cp_stub, 0x08, 0x8E);

    // Timer IRQ0 (PIC remapped to 0x20)
    SetEntry(0x20, (void *)pit_stub, 0x08, 0x8E);
    // Keyboard IRQ1 (PIC remapped to 0x21)
    SetEntry(0x21, (void *)kbd_stub, 0x08, 0x8E);
    
    idt_ptr.limit = sizeof(Entry) * 256 - 1;
    idt_ptr.base  = (std::uint64_t)&idt_table;
    
    asm volatile("lidt %0" : : "m"(idt_ptr));
}

}  // namespace idt
