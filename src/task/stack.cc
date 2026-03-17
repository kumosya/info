/**
 * @file task/stack.cc
 * @brief Stack management for tasks
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

void Task::MkStack() {
    if (is_kernel) {
        stack_base = reinterpret_cast<std::uint64_t>(mm::page::Alloc(KERNEL_STACK_SIZE));
        stack_size = KERNEL_STACK_SIZE;
    } else {
        stack_base = USER_STACK_BASE;
        stack_size = USER_STACK_SIZE;
    }
}

std::uint64_t Task::AllocateStack(uint64_t size) {
    if (stack_allocated + size > stack_size) {
        return 0; // Not enough space
    }
    void *newstack = mm::page::Alloc(size);
    stack_allocated += size;
    return reinterpret_cast<std::uint64_t>(newstack);
}

}  // namespace task


