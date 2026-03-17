/**
 * @file task/clone.cc
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

namespace task {
    
pid_t pid_counter;
static pid_t NewPid() { return pid_counter++; }

Task *Task::Clone(int (*fn)(void *), void *stack, int flags, void *arg, int nice)
{
    Task *child = new Task(is_kernel);
    if (!child) {
        return nullptr;
    }

    if (current_proc) {
        child->parent = this;
    } else {
        child->parent = nullptr;
    }

    child->pid   = NewPid();
    child->flags = flags;
    child->argv  = 0;
    child->sched = &curr_sched;
    child->stat  = task::Blocked;

    if (this->tty) {
        child->tty = this->tty;
    } else {
        child->tty = 1;
    }

    task::Registers regs;
    std::memset(&regs, 0, sizeof(regs));

    // 设置参数
    regs.rdx = reinterpret_cast<std::uint64_t>(arg);
    PTE *user_pml4 = pml4;
    // 设置段选择器、入口点和页表
    if (is_kernel) {
        regs.ds = KERNEL_DS;
        regs.es = KERNEL_DS;
        regs.cs = KERNEL_CS;
        regs.ss = KERNEL_DS;

        regs.rbx = reinterpret_cast<std::uint64_t>(fn);
        regs.rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
    } else {
        regs.ds = USER_CS;  // 这里的 ds 和 es 必须是 USER_CS
        regs.es = USER_CS;
        regs.cs = USER_CS;
        regs.ss = USER_DS;

        regs.rcx = reinterpret_cast<std::uint64_t>(fn);
        regs.rip = reinterpret_cast<std::uint64_t>(ret_syscall);

        // 新页表
        user_pml4 = (PTE *)mm::page::Alloc(512 * sizeof(PTE));
        memset(user_pml4, 0, 512 * sizeof(PTE));

        memcpy(user_pml4, pml4, 512 * sizeof(PTE));
    }

    regs.rflags = (1 << 9) | (1 << 1);
    regs.r11 = regs.rflags;

    // 根据 nice 值设置 CFS 权重
    std::int32_t n = nice;

    child->se = new cfs::Entity();
    if (child->se == nullptr)
    {
        delete child;
        return nullptr;
    }
    child->se->pcb = child;
    child->se->weight           = curr_sched.Nice2Weight(n);
    child->se->vruntime         = 0;
    child->se->sum_exec_runtime = 0;
    child->se->min_vruntime     = 0;

    // 创建线程控制块
    child->thread = new Thread();
    if (child->thread == nullptr) {
        delete child->se;
        delete child;
        return nullptr;
    }
    std::memset(child->thread, 0, sizeof(Thread));

    // 设置内核栈指针
    child->MkStack();
    if (is_kernel) {
        child->thread->rsp0 = child->stack_base + KERNEL_STACK_SIZE;
        child->thread->fs = child->thread->gs = KERNEL_DS;
    } else {
        child->thread->rsp0 = reinterpret_cast<std::uint64_t>(mm::page::Alloc(KERNEL_STACK_SIZE)) + KERNEL_STACK_SIZE;
        child->thread->rsp = USER_STACK_BASE;
        regs.r12 = child->thread->rsp;
        child->thread->fs = USER_DS;
        child->thread->gs = 0;
        if (!child->thread->rsp0) {
            delete child->thread;
            delete child->se;
            delete child;
            return nullptr;
        }
    }

    // 如果current_proc为nullptr（创建第一个进程idle时），直接初始化
    if (current_proc == nullptr) {
        // 初始化第一个进程idle
        child->parent = nullptr;
        child->thread->rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
        child->thread->rsp = child->thread->rsp0;
        child->pml4 = mm::page::kernel_pml4;
    } else {
        // 设置页表
        child->pml4 = pml4;
        if (!is_kernel)
            child->pml4 = user_pml4;
        //tty::printk("%d pml4:0x%p\n", pid, child->pml4);
    }

    // 将寄存器状态复制到新进程的栈顶
    task::Registers *rsp = reinterpret_cast<task::Registers *>(
        child->thread->rsp0 - sizeof(task::Registers));

    std::memcpy(rsp, &regs, sizeof(task::Registers));
    child->thread->rsp = reinterpret_cast<std::uint64_t>(rsp);
    child->thread->rip = regs.rip;
    //tty::printk("%d thread:0x%p rsp:0x%p\n", child->pid, child->thread, child->thread->rsp);
    
    // 将新创建的进程添加到调度队列
    child->stat    = task::Ready;
    child->Enqueue();
    
    task_table.Insert(child);
    
    return child;
}

}
