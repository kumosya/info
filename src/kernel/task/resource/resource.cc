#include "service/common/records.h"
#include "kernel/task.h"

namespace kernel {
int Task::resourceLimit(Task& caller, int resource, bool set, uint64_t& soft, uint64_t& hard) {
    InterruptSpinLockGuard guard(lock_);
    if (&caller != this && caller.effectiveUid() != 0 &&
        (caller.uid() != uid_ || caller.uid() != effectiveUid_ || caller.uid() != savedUid_ ||
         caller.gid() != gid_ || caller.gid() != effectiveGid_ || caller.gid() != savedGid_)) return 1;
    if (resource != 4 && resource != 13) return resource >= 0 && resource < 16 ? 95 : 22;
    uint64_t& current = resource == 4 ? coreLimit_ : niceLimit_;
    uint64_t& maximum = resource == 4 ? coreHardLimit_ : niceHardLimit_;
    const uint64_t oldSoft = current, oldHard = maximum;
    if (set) {
        if (soft > hard) return 22;
        if (hard > maximum && caller.effectiveUid() != 0) return 1;
        current = soft; maximum = hard;
    }
    soft = oldSoft; hard = oldHard;
    return 0;
}
int32_t Task::mainNice() {
    InterruptSpinLockGuard guard(lock_);
    return mainThread_ ? mainThread_->getNice() : 0;
}
uint64_t Task::mainThreadId() {
    InterruptSpinLockGuard guard(lock_);
    return mainThread_ ? mainThread_->getId() : 0;
}
uint32_t Task::threadCount() {
    InterruptSpinLockGuard guard(lock_);
    uint32_t count = 0;
    for (Thread* thread = threads_; thread; thread = thread->getTaskNext())
        if (thread->getState() != ThreadState::Terminated) ++count;
    return count;
}
bool Task::threadInfo(uint64_t minimumTid, os::wire::ThreadInfo& info) {
    InterruptSpinLockGuard guard(lock_);
    Thread* selected = nullptr;
    for (Thread* thread = threads_; thread; thread = thread->getTaskNext()) {
        if (thread->getId() >= minimumTid && thread->getState() != ThreadState::Terminated &&
            (!selected || thread->getId() < selected->getId())) selected = thread;
    }
    if (!selected) return false;
    info = {selected->getId(), id_, selected->getRuntime(), selected->getNice(), static_cast<uint32_t>(selected->getState())};
    return true;
}
int TaskPriorities::apply(Task& caller, Thread& current, unsigned operation,
                          unsigned which, uint64_t who, int value, int& result) {
    if (which > 2 || operation > 2 || (operation == 2 && (which || who))) return 22;
    if (!who) who = which == 0 ? current.getId() : which == 1 ? caller.taskGroupId() : caller.uid();
    const bool privileged = caller.effectiveUid() == 0;
    bool found = false;
    int error = 0;
    result = 20;
    uint64_t cursor = 0;
    while (true) {
        auto target = task::next(cursor);
        if (!target) break;
        cursor = target->id() + 1;
        if (which == 1 && target->taskGroupId() != who) continue;
        InterruptSpinLockGuard guard(target->lock_);
        if (which == 2 && target->uid_ != who) continue;
        for (Thread* thread = target->threads_; thread; thread = thread->getTaskNext()) {
            if ((which == 0 && thread->getId() != who) || thread->getState() == ThreadState::Terminated) continue;
            found = true;
            if (!operation) {
                const int nice = thread->getNice();
                if (nice < result) result = nice;
                continue;
            }
            if (!privileged && caller.effectiveUid() != target->uid_ && caller.effectiveUid() != target->effectiveUid_) {
                error = 1; continue;
            }
            int changed = 0;
            const int status = schedule::changeNice(*thread, value, operation == 2, privileged, target->niceLimit_, changed);
            if (status) error = status;
            else result = changed;
        }
        if (which == 0 && found) break;
    }
    return found ? error : 3;
}
}
