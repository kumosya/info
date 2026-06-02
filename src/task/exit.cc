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
#include "kernel/vfs.h"

extern "C" std::int64_t do_exit(std::int64_t code) {
    return task::current_proc->Exit(code);
}

namespace task {

std::int64_t Task::Exit(std::int64_t code) {
    tty::printk("Thread %d exit with code: 0x%lx\n", pid, code);
    
    exit_code = code;
    stat = Dead;
    
    // 处理孤儿进程：将子进程的父进程设为 init
    Task *init = task_table.Find(1);
    if (init && init != this) {
        task_table.Reparent(this, init);
    }
    
    task_table.Remove(pid);
    
    if (parent && IsParentWaitingForMyself()) {
        tty::printk("Parent %d is waiting for child %d\n", parent->pid, pid);
        parent->is_waiting = false;
        parent->waiting_for = nullptr;
        parent->exit_code = code;
        parent->child_pid = pid;
        parent->Unblock();
    }

    this->~Task();
    // 释放 Task 对象本身的内存
    mm::page::Free(this);
    return 0;
}

Task::~Task() {
    // 关闭所有打开的文件描述符
    if (files) {
        for (std::uint32_t i = 0; i < MAX_FD; i++) {
            if (files->fds[i].used && files->fds[i].file) {
                vfs::Close(files->fds[i].file);
                files->fds[i].used = false;
                files->fds[i].file = nullptr;
            }
        }
        delete files;
    }
    if (current_dir) {
        delete current_dir;
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
