/* Consolidated interrupt/exception stubs for x86_64
   Each stub preserves registers, sets up arguments as expected by
   C handlers, calls the C handler, restores registers and returns
   with iretq (or halts for boot-time handlers that never return). */

.section .text
.global pit_stub
.global kbd_stub
.global gp_stub
.global pf_stub
.global default_handler

/* PIT IRQ0 stub: call C handler and iretq */
pit_stub:
    cli
    /* preserve caller regs */
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    call pit_handler_c
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    sti
    iretq

/* Keyboard IRQ1 stub */
kbd_stub:
    cli
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    call kbd_handler_c
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    sti
    iretq


/* General Protection Fault stub (vector 13) */
gp_stub:
    cli
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    /* RDI = faulting RIP (stored at top of stack), RSI = current RSP */
    mov %rsp, %rsi
    mov (%rsi), %rdi
    call gp_fault_handler
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    iretq

/* Page Fault stub (vector 14) */
pf_stub:
    cli
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    /* RDI = CR2, RSI = faulting RIP */
    mov %cr2, %rdi
    mov %rsp, %rsi
    mov (%rsi), %rsi
    call page_fault_handler
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    iretq

/* Default handler: halt */
default_handler:
    cli
1:  hlt
    jmp 1b


.section .boot.text

.global ge_fault_stub
.global page_fault_stub

/* Boot-time GE and PF stubs used by early boot IDT; these call C handlers
   and then halt (they don't return). */
ge_fault_stub:
    cli
    call ge_fault_handler_c
1:  hlt
    jmp 1b

page_fault_stub:
    cli
    mov %cr2, %rdi
    call page_fault_handler_c
2:  hlt
    jmp 2b
