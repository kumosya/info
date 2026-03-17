/**
 * @file task/wait.cc
 * @brief Function of waiting
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

void Task::Wait(Task *child) {
    if (stat == Dead) {
        return;
    }
    is_waiting = true;
    waiting_for = child;

    Block();
    Schedule();
}

}
