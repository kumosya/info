/**
 * @file sched.cc
 * @brief 调度器框架
 * 
 */

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/kassert.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

Pcb *run_queue_head = nullptr;
SpinLock run_queue_lock;

void SchedInit() {
    run_queue_head = nullptr;
    
    cfs::sched.cfs_rq.rb_root = nullptr;
    cfs::sched.cfs_rq.leftmost = nullptr;
    cfs::sched.cfs_rq.min_vruntime = 0;
    cfs::sched.cfs_rq.nr_running = 0;
    cfs::sched.cfs_rq.curr_vruntime = 0;
    cfs::sched.current = nullptr;
    cfs::sched.clock = 0;
}

void Schedule() {
    Pcb *prev = current_proc;
    Pcb *next = nullptr;

    // Update runqueue first: dequeue / enqueue the previous task so
    // the tree reflects its Ready state before picking the next task.
    if (prev->stat == Dead) {
        cfs::Dequeue(prev);
    } else {
        if (prev->stat != Blocked) {
            prev->stat = Ready;
            cfs::Dequeue(prev);
            cfs::Enqueue(prev);
        } else {
            cfs::Dequeue(prev);
        }
    }

    // Now pick the next task from the up-to-date runqueue and print state
    next = cfs::PickNextTask();
    //tty::printk("[%d -> %d]", prev->pid, next ? next->pid : -1);
    //task::cfs::RbPrintTree();

    cfs::sched.lock.lock();

    if (next == nullptr) {
        tty::Panic("No runnable task found!\n");
    }

    if (prev->stat == Dead) {
        cfs::sched.lock.unlock();

        // 用户进程退出后，恢复内核页表
        if (prev->mm.pml4 != 0 && (next->flags & THREAD_KERNEL)) {
            SwitchTable(next);
        }

        current_proc = next;
        SwitchContext(prev, next);
    } else {
        //if (prev->stat == Blocked) {
        //    tty::printk("Schedule: prev=%d is blocked, skip enqueue, next=%d\n", prev->pid, next ? next->pid : -1);
        //}

        if (prev == next) {
            cfs::sched.lock.unlock();
            return;
        }

        current_proc = next;
        cfs::sched.lock.unlock();

        if (!(prev->flags & THREAD_KERNEL) || !(next->flags & THREAD_KERNEL)) {
            if (!(next->flags & THREAD_KERNEL) && next->mm.pml4) {
                //mm::page::UpdateKernelPml4(next->mm.pml4);
                SwitchTable(next);
                //tty::printk("Switch to user page table: 0x%x\n", mm::Vir2Phy((std::uint64_t)next->mm.pml4));
            } else if (!(prev->flags & THREAD_KERNEL) && (next->flags & THREAD_KERNEL)) {
                //tty::printk("Switch to kernel page table\n");
                SwitchTable(next);
            }
        }
        SwitchContext(prev, next);
    }
}

extern "C" void __switch_to(Pcb *prev, Pcb *next) {
    gdt::tss->rsp0 = next->thread->rsp0;

    __asm__ __volatile__("movw	%%fs,	%0 \n\t" : "=a"(prev->thread->fs));
    __asm__ __volatile__("movw	%%gs,	%0 \n\t" : "=a"(prev->thread->gs));

    __asm__ __volatile__("movw	%0,	%%fs \n\t" ::"a"(next->thread->fs));
    __asm__ __volatile__("movw	%0,	%%gs \n\t" ::"a"(next->thread->gs));

    __asm__ __volatile__("sti");
    wrmsr(0x175, next->thread->rsp0);
}

}  // namespace task
