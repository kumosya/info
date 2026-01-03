
#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/task.h"
#include "kernel/tty.h"

using namespace std;

extern "C" void pit_handler_c();

// Stub implemented in interrupt.S
extern "C" void pit_stub(void);

extern "C" void pit_handler_c() {
    // increment tick
    timer::pit_ticks++;
    // send EOI
    outb(PIC1_CMD, 0x20);
    // tty::printf(".");
    task::schedule();
}

namespace timer {
volatile uint64_t pit_ticks = 0;
void Init(uint32_t freq) {
    if (freq == 0) return;
    uint32_t div = PIT_FREQ / freq;
    uint8_t lo   = div & 0xFF;
    uint8_t hi   = (div >> 8) & 0xFF;
    // set channel 0, lobyte/hibyte, mode 2 (rate generator), binary
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, lo);
    outb(PIT_CHANNEL0, hi);
    // unmask IRQ0
    pic::UnmaskIrq(0);
}

uint64_t GetTicks() { return pit_ticks; }
}  // namespace timer
