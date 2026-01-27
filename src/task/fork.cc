/**
 * @file fork.cc
 * @brief Fork functions for process creation
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task::thread {

static pid_t NewPid() { return pid_counter++; }

pid_t UserFork(void) {
    // todo
    return 0;
}

pid_t Fork(Registers *regs, std::uint64_t flags, std::uint64_t stack_size,
           int nice) {
    // 分配新的进程控制块，需要包含栈空间
    // 栈在Pcb之后，分配 Pcb + STACK_SIZE 大小的内存
    Pcb *child =
        reinterpret_cast<Pcb *>(mm::page::Alloc(sizeof(Pcb) + stack_size));
    if (child == nullptr) {
        return -1;
    }

    std::memset(child, 0, sizeof(Pcb) + stack_size);

    // 如果current_proc为nullptr（创建第一个进程时），直接初始化
    if (current_proc != nullptr) {
        *child        = *current_proc;
        child->parent = current_proc;
    } else {
        // 初始化第一个进程
        child->parent = nullptr;
    }

    child->pid   = NewPid();
    child->stat  = task::Blocked;
    child->flags = flags;

    // 根据 nice 值设置 CFS 权重
    std::int32_t n = nice;

    child->se.weight           = cfs::Nice2Weight(n);
    child->se.vruntime         = 0;
    child->se.sum_exec_runtime = 0;
    child->se.min_vruntime     = 0;

    // 创建线程控制块
    Tcb *thread = reinterpret_cast<Tcb *>(mm::page::Alloc(sizeof(Tcb)));
    if (thread == nullptr) {
        mm::page::Free(child);
        return -1;
    }

    std::memset(thread, 0, sizeof(Tcb));
    child->thread = thread;

    // 设置内核栈指针
    thread->rsp0 =
        reinterpret_cast<std::uint64_t>(child) + sizeof(Pcb) + STACK_SIZE;

    // 如果有regs参数，复制寄存器状态到新进程的栈中
    if (regs != nullptr) {
        // 将寄存器状态复制到新进程的栈顶
        task::Registers *rsp = reinterpret_cast<task::Registers *>(
            thread->rsp0 - sizeof(task::Registers));
        std::memcpy(rsp, regs, sizeof(task::Registers));

        thread->rsp = reinterpret_cast<std::uint64_t>(rsp);
        thread->rip = regs->rip;
        if (flags & THREAD_NO_ARGS) {
            child->argv = current_proc->argv;
        } else {
            child->argv = regs->rcx;
        }
    } else {
        // 如果没有regs参数，使用默认的寄存器状态
        thread->rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
        thread->rsp = thread->rsp0;
    }

    // 设置页表
    child->mm.pml4 = mm::page::kernel_pml4;
    child->stat    = task::Ready;

    // 将新创建的进程添加到 CFS 调度队列
    cfs::sched.Enqueue(child);

    return child->pid;
}
}  // namespace task::thread