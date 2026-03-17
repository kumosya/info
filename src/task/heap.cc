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

void *Task::Brk(std::uint64_t addr) {
    if (addr == 0) {
        return reinterpret_cast<void *>(heap_end);
    } else {
        if (addr < heap_start) {
            return nullptr; // Invalid address
        } else {
            std::uint64_t old_end = heap_end;
            std::uint64_t new_end = addr;
            if (new_end > old_end) {
                std::uint64_t start = (old_end + PAGE_SIZE - 1) & PAGE_MASK;
                std::uint64_t end = (new_end + PAGE_SIZE - 1) & PAGE_MASK;
                for (std::uint64_t va = start; va < end; va += PAGE_SIZE) {
                    void *kpage = mm::page::Alloc(PAGE_SIZE);
                    if (!kpage) {
                        return nullptr; // Allocation failed
                    }
                    mm::page::Map(pml4, va,
                                    mm::Vir2Phy((std::uint64_t)kpage),
                                    PTE_PRESENT | PTE_WRITABLE | PTE_USER);
                }
            }
            heap_end = new_end;
            return reinterpret_cast<void *>(heap_end);
        }
    }
}

void *Task::Sbrk(intptr_t increment) {
    if (heap_start == 0) {
        heap_start = USER_HEAP_BASE;
        heap_end = USER_HEAP_BASE;
    }
    uint64_t old_end = heap_end;
    uint64_t new_end = old_end + increment;
    if (increment > 0) {
        uint64_t start = (old_end + PAGE_SIZE - 1) & PAGE_MASK;
        uint64_t end = (new_end + PAGE_SIZE - 1) & PAGE_MASK;
        for (uint64_t va = start; va < end; va += PAGE_SIZE) {
            void *kpage = mm::page::Alloc(PAGE_SIZE);
            if (!kpage) {
                return nullptr; // Allocation failed
            }
            mm::page::Map(pml4, va,
                            mm::Vir2Phy((uint64_t)kpage),
                            PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        }
    } else {
        /* Shrink: for now, only adjust break without freeing pages */
    }
    heap_end = new_end;
    return reinterpret_cast<void *>(old_end);
}
}   // namespace task
