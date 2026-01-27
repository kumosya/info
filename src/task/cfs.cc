/**
 * @file cfs.cc
 * @brief Completely Fair Scheduler
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task::cfs {

Sched sched;

static inline std::uint64_t CalcVruntimeDelta(std::uint64_t delta,
                                              std::uint32_t weight) {
    return (delta * 1024) / weight;
}

void Sched::UpdateVruntime(task::Pcb *pcb, std::uint64_t delta) {
    std::uint32_t weight         = pcb->se.weight ? pcb->se.weight : 1024;
    std::uint64_t vruntime_delta = CalcVruntimeDelta(delta, weight);

    pcb->se.vruntime += vruntime_delta;
    pcb->se.sum_exec_runtime += delta;
    if (pcb->se.vruntime < min_vruntime) {
        min_vruntime = pcb->se.vruntime;
    }
}

void Sched::NormalizeVruntime(task::Pcb *pcb) {
    if (pcb->se.vruntime < min_vruntime) {
        pcb->se.vruntime = min_vruntime;
    }
}

void Sched::Enqueue(task::Pcb *pcb) {
    if (pcb == nullptr) return;

    lock.lock();

    if (pcb->stat == task::Running || pcb->stat == task::Ready) {
        if (rb_root != nullptr) {
            NormalizeVruntime(pcb);
        }

        if (pcb->se.weight == 0) {
            pcb->se.weight = 1024;
        }

        RbInsert(pcb);

        nr_running++;
        total_weight += pcb->se.weight;

        if (current == nullptr) {
            current = pcb;
        }
    }

    lock.unlock();
}

void Sched::Dequeue(task::Pcb *pcb) {
    if (pcb == nullptr) return;

    // tty::printk("[Dequeue] pid=%d, stat=%d, nr_before=%d, wt_before=%d\n",
    //             pcb->pid, pcb->stat, nr_running, total_weight);

    lock.lock();

    RbErase(pcb);

    // tty::printk("[Dequeue] after RbErase, rb_root=%d\n",
    //             rb_root ? rb_root->pid : -1);

    if (pcb->stat == task::Running || pcb->stat == task::Ready ||
        pcb->stat == task::Blocked) {
        nr_running--;
        total_weight -= pcb->se.weight;
        //    tty::printk("[Dequeue] nr-- to %d\n", nr_running);
    } /*else {
        tty::printk("[Dequeue] stat=%d, no nr change\n", pcb->stat);
    }*/

    if (current == pcb) {
        current = FirstTask();
    }

    lock.unlock();
}

task::Pcb *Sched::PickNextTask(void) {
    task::Pcb *next = nullptr;

    lock.lock();
    // tty::printk("[PickNextTask] curr=%d, nr=%d, wt=%d\n",
    //             current ? current->pid : -1,
    //             nr_running, total_weight);

    if (nr_running > 0) {
        next = FirstTask();
        // tty::printk("[PickNextTask] first=%d, stat=%d\n",
        //             next ? next->pid : -1, next ? next->stat : -1);
        //  跳过 Dead 状态的进程
        while (next && next->stat == Dead) {
            // tty::printk("[PickNextTask] skip dead proc %d\n", next->pid);
            Dequeue(next);
            nr_running--;
            if (nr_running > 0) {
                next = FirstTask();
            } else {
                next = nullptr;
                break;
            }
        }
    }
    current = next;

    lock.unlock();

    return next;
}

void Sched::UpdateClock(std::uint64_t delta) {
    lock.lock();

    clock += delta;
    if (current == nullptr) return;
    UpdateVruntime(current, delta);

    lock.unlock();
}

void Sched::UpdateVruntimeCurrent(std::uint64_t delta) {
    if (current == nullptr) return;

    lock.lock();
    UpdateVruntime(current, delta);
    lock.unlock();
}

bool Sched::NeedsSchedule() {
    if (total_weight == 0 || nr_running == 0) return false;
    std::uint64_t need_time =
        nr_running * TIMER_PERIOD * current->se.weight / total_weight;

    if (current->time_used >= need_time) {
        current->time_used = 0;
        return true;
    } else {
        return false;
    }
}
}  // namespace task::cfs
