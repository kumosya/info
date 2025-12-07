#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQ     1193182UL

namespace timer {
    static volatile uint64_t pit_ticks = 0;
    void init(uint32_t freq);
    uint64_t ticks();
}

#endif // TIMER_H
