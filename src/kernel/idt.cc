#include "cpu.h"
#include "mm.h"
#include "tty.h"

#include <cstdint>
#include <cstring>
using namespace std;

static idt_entry idt_table[256];

struct fault_stack_code {
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t error_code, rip, cs, rflags, rsp, ss;
};

struct fault_stack_nocode {
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
};

extern "C" void de_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#DE Divide Fault!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void debug_trap_handler(fault_stack_nocode *stack) {
    tty::printf("#DB Debug.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void nmi_handler(fault_stack_nocode *stack) {
    tty::printf("NMI Interrupt.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void bp_trap_handler(fault_stack_nocode *stack) {
    tty::printf("Breakpoint.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void of_trap_handler(fault_stack_nocode *stack) {
    tty::printf("Overflow.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void br_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#BR BOUND Range Exceeded!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ud_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#UD Invalid Opcode!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void nm_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#NM Device Not Available!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void double_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#DF Double Fault!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void tss_fault_handler(fault_stack_code *stack) {
    tty::printf("#TS Invalid TSS Fault! Error code = 0x%lx\n",  stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void np_fault_handler(fault_stack_code *stack) {
    tty::printf("#NP Segment Not Present! Error code = 0x%lx\n",  stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ss_fault_handler(fault_stack_code *stack) {
    tty::printf("Segment Fault.\nError code = 0x%lx,",  stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void gp_fault_handler(fault_stack_code *stack) {
    tty::printf("#GP General Protection Fault! Error code = 0x%lx\n",  stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void page_fault_handler(fault_stack_code *stack, uint64_t rip) {
    uint64_t cr3, cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    tty::printf("Page Fault. Error code = 0x%lx\n",  stack->error_code);
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx CR2=0x%lx\n", stack->rflags, cr2);
    tty::printf(" CR3=0x%lx\n", cr3);
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void mf_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#MF x87 FPU Floating-Point Error!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ac_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#AC Alignment Check.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void mc_abort_handler(fault_stack_nocode *stack) {
    tty::printf("#MC Machine Check.\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void xm_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#XM SIMD Floating-Point Exception!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void ve_fault_handler(fault_stack_nocode *stack) {
    tty::printf("#VE Virtualization Exception!\n");
    tty::printf(" RIP=0x%lx, RSP=0x%lx\n", stack->rip, stack->rsp);
    tty::printf(" CS=0x%lx, SS=0x%lx\n", stack->cs, stack->ss);
    tty::printf(" RFLAGS=0x%lx\n", stack->rflags);
    
    while (true) {
        asm volatile("hlt");
    }
}

extern "C" void cp_fault_handler(fault_stack_code *stack) {
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
void init() {
    memset(&idt_table, 0, sizeof(idt_table));

    // Set up minimal IDT with page fault handler
    for (int i = 0; i < 256; i++) {
        set_entry(i, nullptr, 0x08, 0x8E); // Null handler
    }
    set_entry(0, (void *)de_stub, 0x08, 0x8E); // Divide by zero
    set_entry(1, (void *)debug_stub, 0x08, 0x8E); // Debug
    set_entry(2, (void *)nmi_stub, 0x08, 0x8E); // NMI
    set_entry(3, (void *)bp_stub, 0x08, 0x8E); // Breakpoint
    set_entry(4, (void *)of_stub, 0x08, 0x8E); // Overflow
    set_entry(5, (void *)br_stub, 0x08, 0x8E); // BOUND Range Exceeded
    set_entry(6, (void *)ud_stub, 0x08, 0x8E); // Invalid Opcode
    set_entry(7, (void *)nm_stub, 0x08, 0x8E); // Device Not Available
    set_entry(8, (void *)df_stub, 0x08, 0x8E); // Double Fault

    set_entry(10, (void *)ts_stub, 0x08, 0x8E); // Invalid TSS
    set_entry(11, (void *)np_stub, 0x08, 0x8E); // Segment Not Present
    set_entry(12, (void *)ss_stub, 0x08, 0x8E); // Stack-Segment Fault

    set_entry(13, (void *)gp_stub, 0x08, 0x8E); // General Protection Fault
    set_entry(14, (void *)pf_stub, 0x08, 0x8E); // Page Fault

    set_entry(16, (void *)mf_stub, 0x08, 0x8E); // x87 Floating-Point Exception
    set_entry(17, (void *)ac_stub, 0x08, 0x8E); // Alignment Check
    set_entry(18, (void *)mc_stub, 0x08, 0x8E); // Machine Check
    set_entry(19, (void *)xm_stub, 0x08, 0x8E); // SIMD Floating-Point Exception

    set_entry(19, (void *)ve_stub, 0x08, 0x8E);
    set_entry(19, (void *)cp_stub, 0x08, 0x8E);

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
