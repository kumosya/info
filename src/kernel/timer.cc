
#include "cpu.h"
#include "io.h"
#include "tty.h"

#include <cstdint>

using namespace std;

extern "C" void pit_handler_c();

// Stub implemented in interrupt.S
extern "C" void pit_stub(void);

extern "C" void pit_handler_c() {
    // increment tick
    timer::pit_ticks++;
    // Keep interrupt handler short: only print a dot every 100 ticks
    if (timer::pit_ticks % 100 == 0) {
        tty::printf(".");
    }
    // send EOI
    outb(PIC1_CMD, 0x20);
}

namespace timer {
volatile uint64_t pit_ticks = 0;
void init(uint32_t freq) {
    if (freq == 0)
        return;
    uint32_t div = PIT_FREQ / freq;
    uint8_t lo   = div & 0xFF;
    uint8_t hi   = (div >> 8) & 0xFF;
    // set channel 0, lobyte/hibyte, mode 2 (rate generator), binary
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, lo);
    outb(PIT_CHANNEL0, hi);
    // unmask IRQ0
    pic::unmask_irq(0);
}

uint64_t ticks() { return pit_ticks; }
} // namespace timer
