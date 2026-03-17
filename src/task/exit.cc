/**
 * @file task/exit.cc
 * @brief Exit & Kill functions for process termination
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/syscall.h"

extern "C" std::int64_t do_exit(std::int64_t code) {
    return task::current_proc->Exit(code);
}

namespace task {

std::int64_t Task::Exit(std::int64_t code) {
    tty::printk("Thread %d exit with code: 0x%lx\n", pid, code);
    
    exit_code = code;
    stat = Dead;
    task_table.Remove(pid);
    
    if (IsParentWaitingForMyself()) {
        parent->is_waiting = false;
        parent->waiting_for = nullptr;
        parent->exit_code = code;
        parent->Unblock();
    }

    this->~Task();
    return 0;
}

Task::~Task() {
    if (files) {
        files->~FileDescriptorTable();
    }

    if (thread) {
        if (thread->rsp0 && !is_kernel) {
            mm::page::Free(reinterpret_cast<void*>(thread->rsp0));
        }
        delete thread;
    }
    if (se) {
        delete se;
    }
    if (pml4 && pml4 != reinterpret_cast<PTE*>(mm::page::kernel_pml4)) {
        mm::page::Free(pml4);
    }
    
    if (argv != 0) {
        char **argv_ptr = reinterpret_cast<char **>(argv);
        for (std::uint64_t i = 0; argv_ptr[i] != nullptr; i++) {
            mm::page::Free(argv_ptr[i]);
        }
        mm::page::Free(argv_ptr);
    }
    Schedule();
}

int Task::Kill(Task *proc, int code) {
    return 0;
}

}  // namespace task
