#include <timer.h>
#include <tty.h>
#include <pic.h>
#include <io.h>
#include <idt.h>
#include <stdint.h>

extern "C" void pit_handler_c();

// IRQ0 stub: preserve registers, call C handler, send EOI in handler, then iretq
void pit_stub(void) {
    asm __volatile__ (
        "cli\n"
        "push %rax\n"
        "push %rbx\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "push %rbp\n"
        "push %r8\n"
        "push %r9\n"
        "push %r10\n"
        "push %r11\n"
        "call pit_handler_c\n"
        "pop %r11\n"
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %rbp\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rbx\n"
        "pop %rax\n"
        "sti\n"
        "iretq\n"
    );
}

extern "C" void pit_handler_c() {
    // increment tick
    timer::pit_ticks++;
    if (timer::pit_ticks % 100 == 0) {
        tty::printf("." );
    }
    // send EOI
    outb(PIC1_CMD, 0x20);
}

namespace timer {
    void init(uint32_t freq) {
        if (freq == 0) return;
        uint32_t div = PIT_FREQ / freq;
        uint8_t lo = div & 0xFF;
        uint8_t hi = (div >> 8) & 0xFF;
        // set channel 0, lobyte/hibyte, mode 2 (rate generator), binary
        outb(PIT_COMMAND, 0x34);
        outb(PIT_CHANNEL0, lo);
        outb(PIT_CHANNEL0, hi);
        // unmask IRQ0
        pic::unmask_irq(0);
    }

    uint64_t ticks() { return pit_ticks; }
}
