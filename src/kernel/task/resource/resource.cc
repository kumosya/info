/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/resource/resource.cc
 * Resource limits, thread statistics, and permission-checked priorities.
 */

#include "kernel/task.h"
#include "service/common/records.h"

namespace kernel::task {

// Query or set CORE/NICE limits for this target under lock_. caller supplies
// the authorization identity; soft/hard input new values when set and return
// the old values on success. Enforce soft <= hard and restrict hard-limit
// increases to root. Return positive errno; unsupported limits are not no-ops.
int Task::resourceLimit(Task& caller, int resource, bool set, uint64_t& soft,
                        uint64_t& hard) {
    InterruptSpinLockGuard guard(lock_);
    if (&caller != this && caller.effectiveUid() != 0 &&
        (caller.uid() != uid_ || caller.uid() != effectiveUid_ ||
         caller.uid() != savedUid_ || caller.gid() != gid_ ||
         caller.gid() != effectiveGid_ || caller.gid() != savedGid_))
        return 1;
    if (resource != 4 && resource != 8 && resource != 13)
        return resource >= 0 && resource < 16 ? 95 : 22;
    uint64_t& current      = resource == 4 ? coreLimit_ : resource==8?memlockLimit_:niceLimit_;
    uint64_t& maximum      = resource == 4 ? coreHardLimit_ : resource==8?memlockHardLimit_:niceHardLimit_;
    const uint64_t oldSoft = current, oldHard = maximum;
    if (set) {
        if (soft > hard) return 22;
        if (hard > maximum && caller.effectiveUid() != 0) return 1;
        __atomic_store_n(&current,soft,__ATOMIC_RELEASE);
        maximum = hard;
    }
    soft = oldSoft;
    hard = oldHard;
    return 0;
}
// Main-thread nice only; sibling threads can have different priorities.
int32_t Task::mainNice() {
    InterruptSpinLockGuard guard(lock_);
    return mainThread_ ? mainThread_->getNice() : 0;
}
// Main-thread ID, or zero after its object is no longer available.
uint64_t Task::mainThreadId() {
    InterruptSpinLockGuard guard(lock_);
    return mainThread_ ? mainThread_->getId() : 0;
}
// Count live threads rather than all linked Thread objects: terminated objects
// can remain linked until join or full process reclamation.
uint32_t Task::threadCount() {
    InterruptSpinLockGuard guard(lock_);
    uint32_t count = 0;
    for (Thread* thread = threads_; thread; thread = thread->getTaskNext())
        if (thread->getState() != ThreadState::Terminated) ++count;
    return count;
}
// Choose the smallest live TID at or above minimumTid, then fill one wire
// record under lock_. Multi-call enumeration is not a frozen snapshot.
bool Task::threadInfo(uint64_t minimumTid, os::wire::ThreadInfo& info) {
    InterruptSpinLockGuard guard(lock_);
    Thread* selected = nullptr;
    for (Thread* thread = threads_; thread; thread = thread->getTaskNext()) {
        if (thread->getId() >= minimumTid &&
            thread->getState() != ThreadState::Terminated &&
            (!selected || thread->getId() < selected->getId()))
            selected = thread;
    }
    if (!selected) return false;
    info = {selected->getId(), id_, selected->getRuntime(), selected->getNice(),
            static_cast<uint32_t>(selected->getState())};
    return true;
}
// Select threads by TID, process group, or real UID. operation is query,
// absolute set, or current-thread relative nice; who = 0 uses the caller's
// corresponding identity. result carries nice, return value carries positive
// errno. Group updates are incremental and do not roll back earlier success.
int applyTaskPriority(Task& caller, Thread& current, unsigned operation,
                      unsigned which, uint64_t who, int value, int& result) {
    if (which > 2 || operation > 2 || (operation == 2 && (which || who)))
        return 22;
    if (!who)
        who = which == 0   ? current.getId()
              : which == 1 ? caller.taskGroupId()
                           : caller.uid();
    const bool privileged = caller.effectiveUid() == 0;
    bool found            = false;
    int error             = 0;
    result                = 20;
    uint64_t cursor       = 0;
    while (true) {
        auto target = task::next(cursor);
        if (!target) break;
        cursor = target->id() + 1;
        if (which == 1 && target->taskGroupId() != who) continue;
        InterruptSpinLockGuard guard(target->lock_);
        if (which == 2 && target->uid_ != who) continue;
        for (Thread* thread = target->threads_; thread;
             thread         = thread->getTaskNext()) {
            if ((which == 0 && thread->getId() != who) ||
                thread->getState() == ThreadState::Terminated)
                continue;
            found = true;
            // For a set of threads, report the smallest nice (highest
            // priority), not an average or the first member encountered.
            if (!operation) {
                const int nice = thread->getNice();
                if (nice < result) result = nice;
                continue;
            }
            if (!privileged && caller.effectiveUid() != target->uid_ &&
                caller.effectiveUid() != target->effectiveUid_) {
                error = 1;
                continue;
            }
            int changed = 0;
            // Use the target's NICE limit and the authenticated caller's
            // privilege. changeNice serializes the actual weight change with
            // its CPU accounting.
            const int status =
                task::changeNice(*thread, value, operation == 2, privileged,
                                 target->niceLimit_, changed);
            if (status)
                error = status;
            else
                result = changed;
        }
        if (which == 0 && found) break;
    }
    return found ? error : 3;
}

// Current-task core limit update. A non-root task may lower its hard limit but
// cannot raise it again beyond the current hard ceiling.
bool Task::setCoreLimit(uint64_t soft, uint64_t hard) {
    InterruptSpinLockGuard guard(lock_);
    if (soft > hard || (hard > coreHardLimit_ && effectiveUid_ != 0))
        return false;
    coreLimit_     = soft;
    coreHardLimit_ = hard;
    return true;
}

}  // namespace kernel::task
