/**
 * @file cfs.cc
 * @brief 完全公平调度器(Completely Fair Scheduler)实现
 */

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task::cfs {

CfsSched sched;

static inline std::int32_t GetNiceFromWeight(std::uint32_t weight) {
    for (std::int32_t i = 0; i < 40; i++) {
        if (PRIO_TO_WEIGHT[i] == weight) {
            return i - 20;
        }
    }
    return 0;
}

static inline std::uint32_t GetWeightFromNice(std::int32_t nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    return PRIO_TO_WEIGHT[nice + 20];
}

static inline std::uint32_t GetInvWeightFromNice(std::int32_t nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    return PRIO_TO_INV_WEIGHT[nice + 20];
}

static task::Pcb *FirstTask(void) {
    return sched.cfs_rq.leftmost;
}

static inline std::uint64_t CalcVruntimeDelta(std::uint64_t delta, std::uint32_t weight) {
    return (delta * 1024) / weight;
}

static void UpdateVruntime(task::Pcb *pcb, std::uint64_t delta) {
    std::uint32_t weight = pcb->weight ? pcb->weight : 1024;
    std::uint64_t vruntime_delta = CalcVruntimeDelta(delta, weight);
    
    pcb->vruntime += vruntime_delta;
    pcb->sum_exec_runtime += delta;
    
    if (pcb->vruntime < sched.cfs_rq.min_vruntime) {
        sched.cfs_rq.min_vruntime = pcb->vruntime;
    }
}

static void NormalizeVruntime(task::Pcb *pcb) {
    if (pcb->vruntime < sched.cfs_rq.min_vruntime) {
        pcb->vruntime = sched.cfs_rq.min_vruntime;
    }
}

void Enqueue(task::Pcb *pcb) {
    if (pcb == nullptr) return;
    
    sched.lock.lock();
    
    if (pcb->stat == task::Running || pcb->stat == task::Ready) {
        if (sched.cfs_rq.rb_root != nullptr) {
            NormalizeVruntime(pcb);
        }
        
        if (pcb->weight == 0) {
            pcb->weight = 1024;
        }
        
        RbInsert(pcb);
        
        sched.cfs_rq.nr_running++;
        
        if (sched.current == nullptr) {
            sched.current = pcb;
        }
    }
    
    sched.lock.unlock();
}

void Dequeue(task::Pcb *pcb) {
    if (pcb == nullptr) return;
    
    //tty::printk("[Dequeue] pid=%d, stat=%d, nr_before=%d\n",
    //            pcb->pid, pcb->stat, sched.cfs_rq.nr_running);
    
    sched.lock.lock();
    
    RbErase(pcb);
    
    //tty::printk("[Dequeue] after RbErase, rb_root=%d\n",
    //            sched.cfs_rq.rb_root ? sched.cfs_rq.rb_root->pid : -1);
    
    if (pcb->stat == task::Running || pcb->stat == task::Ready || pcb->stat == task::Blocked) {
        sched.cfs_rq.nr_running--;
    //    tty::printk("[Dequeue] nr-- to %d\n", sched.cfs_rq.nr_running);
    } /*else {
        tty::printk("[Dequeue] stat=%d, no nr change\n", pcb->stat);
    }*/
    
    if (sched.current == pcb) {
        sched.current = FirstTask();
    }
    
    sched.lock.unlock();
}

task::Pcb *PickNextTask(void) {
    task::Pcb *next = nullptr;
    
    sched.lock.lock();
    //tty::printk("[PickNextTask] curr=%d, nr=%d\n",
    //            sched.current ? sched.current->pid : -1,
    //            sched.cfs_rq.nr_running);
    
    if (sched.cfs_rq.nr_running > 0) {
        next = FirstTask();
        //tty::printk("[PickNextTask] first=%d, stat=%d\n",
        //            next ? next->pid : -1, next ? next->stat : -1);
        // 跳过 Dead 状态的进程
        while (next && next->stat == Dead) {
            //tty::printk("[PickNextTask] skip dead proc %d\n", next->pid);
            Dequeue(next);
            sched.cfs_rq.nr_running--;
            if (sched.cfs_rq.nr_running > 0) {
                next = FirstTask();
            } else {
                next = nullptr;
                break;
            }
        }
    }
    
    sched.lock.unlock();
    
    return next;
}

void UpdateClock(std::uint64_t delta) {
    sched.lock.lock();
    
    sched.clock += delta;
    
    if (sched.current && 
        (sched.current->stat == task::Running || 
         sched.current->stat == task::Ready)) {
        UpdateVruntime(sched.current, delta);
    }
    
    sched.lock.unlock();
}

void UpdateVruntimeCurrent(std::uint64_t delta) {
    if (sched.current == nullptr) return;
    
    sched.lock.lock();
    UpdateVruntime(sched.current, delta);
    sched.lock.unlock();
}

std::uint32_t NrRunning(void) {
    return sched.cfs_rq.nr_running;
}

task::Pcb *GetLeftmost(void) {
    return sched.cfs_rq.leftmost;
}

bool NeedsPreempt(task::Pcb *pcb) {
    if (sched.current == nullptr || pcb == nullptr) {
        return false;
    }
    
    std::int64_t vruntime_diff = (std::int64_t)sched.current->vruntime - 
                                 (std::int64_t)pcb->vruntime;
    
    return vruntime_diff > (std::int64_t)SYSCTL_SCHED_WAKEUP_GRANULARITY;
}

std::uint64_t TimeSlice(void) {
    std::uint64_t slice = SYSCTL_SCHED_LATENCY;
    
    if (sched.cfs_rq.nr_running > 0) {
        slice = SYSCTL_SCHED_LATENCY / sched.cfs_rq.nr_running;
        
        if (slice < SYSCTL_SCHED_MIN_GRANULARITY) {
            slice = SYSCTL_SCHED_MIN_GRANULARITY;
        }
    }
    
    return slice;
}

}  // namespace cfs
