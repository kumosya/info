/**
 * @file task/timer.cc
 * @brief Timer implementation
 * @author Kumosya, 2025-2026
 **/
#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/kassert.h"

extern "C" void pit_handler_c() {
    timer::pit_ticks++;

    if (task::current_proc) {
        task::current_proc->GetEntity()->time_used += TIMER_PERIOD;
        task::curr_sched.UpdateClock(TIMER_PERIOD);
    }
    outb(PIC1_CMD, 0x20);
    if (task::current_proc && task::curr_sched.NeedsSchedule()) {
        task::Schedule();
    }
}