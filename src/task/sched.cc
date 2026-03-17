/**
 * @file task/curr_sched.cc
 * @brief curr_Scheduler implementation
 * @author Kumosya, 2025-2026
 */

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/kassert.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

Task *run_queue_head = nullptr;
SpinLock run_queue_lock;

void SchedInit() { run_queue_head = nullptr; }

void Schedule() {
    Task *prev = current_proc;
    Task *next = nullptr;

    // Update runqueue first: dequeue / enqueue the previous task so
    // the tree reflects its Ready state before picking the next task.
    if (prev->GetState() == Dead) {
        curr_sched.Dequeue(prev);
    } else {
        if (prev->GetState() != Blocked) {
            prev->SetState(Ready);
            curr_sched.Dequeue(prev);
            curr_sched.Enqueue(prev);
        } else {
            curr_sched.Dequeue(prev);
        }
    }

    // Now pick the next task from the up-to-date runqueue and print state
    next = curr_sched.PickNextTask();

    curr_sched.lock.lock();

    if (next == nullptr) {
        tty::printk("WARNING: No runnable task, switching to idle.\n");
        next = idle;
    }
    tty::printk("%d ", next->GetPid());

    if (prev->GetState() == Dead) {
        curr_sched.lock.unlock();

        // 用户进程退出后，恢复内核页表
        if (prev->GetPml4() != 0 && !(next->IsKernel())) {
            SwitchTable(next);
        }

        mm::page::Free(prev->thread);
        mm::page::Free(prev->GetEntity());
        mm::page::Free(prev);

        current_proc = next;
        SwitchContext(prev, next);
    } else {
        // if (prev->stat == Blocked) {
        //     tty::printk("curr_Schedule: prev=%d is blocked, skip enqueue,
        //     next=%d\n", prev->pid, next ? next->pid : -1);
        // }

        if (prev == next) {
            curr_sched.lock.unlock();
            return;
        }
        
        current_proc = next;
        curr_sched.lock.unlock();

        //tty::printk(" GS_BASE=0x%lx GS_KERNEL_BASE=0x%lx\n", rdmsr(MSR_GS_BASE), rdmsr(MSR_KERNEL_GS_BASE));
        if (!(prev->IsKernel()) || !(next->IsKernel())) {
            if (!(next->IsKernel()) && next->GetPml4()) {
                // mm::page::UpdateKernelPml4(next->mm.pml4);
                SwitchTable(next);
                // tty::printk("Switch to user page table: 0x%x\n",
                // mm::Vir2Phy((std::uint64_t)next->mm.pml4));
            } else if (!(prev->IsKernel()) &&
                       (next->IsKernel())) {
                // tty::printk("Switch to kernel page table\n");
                SwitchTable(next);
            }
        }
        SwitchContext(prev, next);
    }
}

extern "C" void __switch_to(Task *prev, Task *next) {
    gdt::tss->rsp0 = next->thread->rsp0;

    __asm__ __volatile__("movw	%%fs,	%0\n" : "=a"(prev->thread->fs));
    __asm__ __volatile__("movw  %0,     %%fs\n" ::"a"(next->thread->fs));

    // 我也不知道为什么，反正这么写就能正常跑
    wrmsr(MSR_GS_BASE, reinterpret_cast<std::uint64_t>(gdt::tss));
    wrmsr(MSR_KERNEL_GS_BASE, 0);

    sti();
}

}  // namespace task
