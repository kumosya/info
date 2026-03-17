/**
 * @file task/cfs.cc
 * @brief Completely Fair Scheduler
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/kassert.h"

namespace task {

Sched curr_sched;

static inline std::uint64_t CalcVruntimeDelta(std::uint64_t delta,
                                              std::uint32_t weight) {
    return (delta * 1024) / weight;
}

void Sched::UpdateVruntime(cfs::Entity *entity, std::uint64_t delta) {
    std::uint32_t weight         = entity->weight ? entity->weight : 1024;
    std::uint64_t vruntime_delta = CalcVruntimeDelta(delta, weight);

    entity->vruntime += vruntime_delta;
    entity->sum_exec_runtime += delta;
    if (entity->vruntime < min_vruntime) {
        min_vruntime = entity->vruntime;
    }
}

void Sched::NormalizeVruntime(cfs::Entity *entity) {
    if (entity->vruntime < min_vruntime) {
        entity->vruntime = min_vruntime;
    }
}

void Sched::Enqueue(Task *pcb) {
    KASSERT(pcb != nullptr);
    KASSERT(pcb->GetEntity() != nullptr);
    KASSERT(pcb->GetEntity()->pcb == pcb);

    lock.lock();

    if (pcb->GetState() == task::Running || pcb->GetState() == task::Ready) {
        if (rb_root != nullptr) {
            NormalizeVruntime(pcb->GetEntity());
        }

        if (pcb->GetEntity()->weight == 0) {
            pcb->GetEntity()->weight = 1024;
        }

        RbInsert(pcb->GetEntity());

        nr_running++;
        total_weight += pcb->GetEntity()->weight;

        if (current == nullptr) {
            current = pcb->GetEntity();
        }
    }

    lock.unlock();
}

void Sched::Dequeue(Task *pcb) {
    if (pcb == nullptr) return;

    //tty::printk("[Dequeue] pid=%d, stat=%d, nr_before=%d, wt_before=%d\n",
    //             pcb->GetPid(), pcb->GetState(), nr_running, total_weight);
    
    lock.lock();
    
    RbErase(pcb->GetEntity());

    //tty::printk("[Dequeue] after RbErase, rb_root=%d\n",
    //             rb_root ? rb_root->pcb->GetPid() : -1);

    if (pcb->GetState() == task::Running || pcb->GetState() == task::Ready ||
        pcb->GetState() == task::Blocked) {
        nr_running--;
        total_weight -= pcb->GetEntity()->weight;
        //    tty::printk("[Dequeue] nr-- to %d\n", nr_running);
    } /*else {
        tty::printk("[Dequeue] stat=%d, no nr change\n", entity->stat);
    }*/

    if (current == pcb->GetEntity()) {
        current = FirstTask();
    }

    lock.unlock();
}

Task *Sched::PickNextTask(void) {
    cfs::Entity *next = nullptr;

    lock.lock();
    // tty::printk("[PickNextTask] curr=%d, nr=%d, wt=%d\n",
    //             current ? current->pid : -1,
    //             nr_running, total_weight);

    if (nr_running > 0) {
        next = FirstTask();
        // tty::printk("[PickNextTask] first=%d, stat=%d\n",
        //             next ? next->pid : -1, next ? next->stat : -1);
        //  跳过 Dead 状态的进程
        while (next && next->pcb->GetState() == Dead) {
            // tty::printk("[PickNextTask] skip dead proc %d\n", next->pid);
            Dequeue(next->pcb);
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

    return next->pcb;
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
        nr_running * TIMER_PERIOD * current->weight / total_weight;

    if (current->time_used >= need_time) {
        current->time_used = 0;
        return true;
    } else {
        return false;
    }
}
}  // namespace task::cfs
