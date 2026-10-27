/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/wait.cc
 * Child-state waits, thread joins, and deferred thread-object retirement.
 */

#include "kernel/mm.h"
#include "kernel/task.h"

namespace kernel::task {

// Inspect children and register a wait without sleeping under the task lock.
// taskId selects PID/group/all; options select event classes, NOHANG, and
// waitid peek behavior. Outputs carry status, childId and optional event.
// Return 0 for a result, 1 for registered wait, 2 for no ready NOHANG event,
// and -1 when no eligible child exists. The service loops after wakeup.
int Task::waitForChild(int64_t taskId, Thread* waiter, int& status,
                       uint32_t options, uint64_t& childId,
                       os::wire::ChildEvent* event) {
    if (waiter == nullptr) return -1;
    TaskReference reaped;
    bool found        = false;
    const bool waitId = (options & 0x10000000U) != 0;
    const bool peek   = (options & 0x01000000U) != 0;
    {
        InterruptSpinLockGuard guard(lock_);
        Task** link = &firstChild_;
        while (*link != nullptr) {
            Task* child = *link;
            const bool matches =
                taskId == -1 ||
                (taskId > 0 && child->id_ == static_cast<uint64_t>(taskId)) ||
                (taskId == 0 && child->taskGroupId_ == taskGroupId_) ||
                (taskId < -1 &&
                 child->taskGroupId_ == static_cast<uint64_t>(-taskId));
            if (!matches) {
                link = &child->nextSibling_;
                continue;
            }
            found           = true;
            bool terminated = false;
            bool stateEvent = false;
            {
                InterruptSpinLockGuard childGuard(child->lock_);
                // Do not expose a final exit status while core writing can
                // still change the dumped flag. Stopped/continued events
                // remain separately selectable.
                const bool exited = child->state_ == TaskState::Terminated &&
                                    !coreDump().pending(*child);
                terminated = exited && (!waitId || (options & 4U));
                if (terminated) {
                    status     = child->exitSignal_
                                     ? static_cast<int>(child->exitSignal_) |
                                       ((child->coreState_ ==
                                             os::wire::CoreState::Complete ||
                                         child->coreState_ ==
                                             os::wire::CoreState::Truncated)
                                                ? 0x80
                                                : 0)
                                     : (child->exitCode_ & 0xff) << 8;
                    stateEvent = true;
                    if (event)
                        *event = {child->id_, child->uid_,
                                  child->exitSignal_
                                      ? ((child->coreState_ ==
                                              os::wire::CoreState::Complete ||
                                          child->coreState_ ==
                                              os::wire::CoreState::Truncated)
                                             ? 3U
                                             : 2U)
                                      : 1U,
                                  child->exitSignal_
                                      ? static_cast<int32_t>(child->exitSignal_)
                                      : child->exitCode_ & 255};
                } else if (!exited && (options & 2U) != 0 && child->stopped_ &&
                           !child->stopReported_) {
                    if (!peek) child->stopReported_ = true;
                    status = (static_cast<int>(child->stopSignal_) << 8) | 0x7f;
                    stateEvent = true;
                    if (event)
                        *event = {child->id_, child->uid_, 5,
                                  static_cast<int32_t>(child->stopSignal_)};
                } else if (!exited && (options & 8U) != 0 &&
                           child->continuedPending_) {
                    if (!peek) child->continuedPending_ = false;
                    status     = 0xffff;
                    stateEvent = true;
                    if (event) *event = {child->id_, child->uid_, 6, 18};
                }
            }
            // WNOWAIT reports the event without consuming stop/continue flags
            // or unlinking an exited child, allowing a later consuming wait to
            // claim it.
            if (stateEvent && peek) {
                childId = child->id_;
                return 0;
            }
            if (terminated) {
                *link               = child->nextSibling_;
                child->nextSibling_ = nullptr;
                reaped              = TaskReference(child);
                childId             = child->id_;
                // Hand cleanup to the worker before the waiter can be killed.
                // The local reference and queue reference replace list
                // ownership.
                taskReclaimer().submit(*child);
                child->unlinkGlobal();
                child->release();
                break;
            }
            if (stateEvent) {
                childId = child->id_;
                return 0;
            }
            link = &child->nextSibling_;
        }
        // Register before releasing the same lock used by state notification.
        // prepareCurrentWait covers a wake that arrives before the caller
        // blocks.
        if (reaped == nullptr && found && (options & 1U) == 0) {
            prepareCurrentWait();
            waiter->registerWait(lock_, childWaiters_);
            waiter->setWaitNext(childWaiters_);
            childWaiters_ = waiter;
        }
    }
    // The reference and worker now own lifetime independently of the parent
    // list. Wait for physical cleanup outside the parent lock, which cleanup
    // itself may need for orphan adoption and notification.
    if (reaped != nullptr) {
        reaped->destroy();
        return 0;
    }
    if (!found) return -1;
    if ((options & 1U) != 0) {
        childId = 0;
        status  = 0;
        return 2;
    }
    return 1;
}

// Detach all child-event waiters under the owner lock, then wake them after
// unlocking. Every waiter rechecks its own PID/event filter; a wake does not
// reserve a particular child result.
void Task::notifyChildStateChange() {
    Thread* waiters = nullptr;
    {
        InterruptSpinLockGuard guard(lock_);
        waiters       = childWaiters_;
        childWaiters_ = nullptr;
    }
    while (waiters != nullptr) {
        Thread* waiter = waiters;
        waiters        = waiters->getWaitNext();
        waiter->setWaitNext(nullptr);
        waiter->clearWaitRegistration();
        wakeThread(waiter);
    }
}

// Join only a sibling thread and reject self-join or an already consumed
// result. Return the same ready/wait/no-hang categories as child waiting. A
// successful join consumes the result, but object freeing also requires the
// old execution stack to have been retired by context-switch completion.
int Task::waitForThread(uint64_t threadId, Thread* waiter, int& exitCode,
                        bool noHang) {
    if (waiter == nullptr || waiter->getId() == threadId) {
        return -1;
    }
    InterruptSpinLockGuard guard(lock_);
    Thread* target = threads_;
    while (target != nullptr && target->getId() != threadId) {
        target = target->getTaskNext();
    }
    if (target == nullptr || target->isJoinConsumed()) {
        return -1;
    }
    if (target->getState() == ThreadState::Terminated) {
        exitCode = target->getExitCode();
        target->setJoinConsumed(true);
        if (target->isStackRetired()) reapJoinedThreadLocked(target);
        return 0;
    }
    if (noHang) {
        exitCode = 0;
        return 2;
    }
    prepareCurrentWait();
    waiter->registerWait(lock_, target->accessJoinWaiters(),
                         &target->accessJoinWaitersTail());
    waiter->setWaitNext(nullptr);
    if (target->getJoinWaitersTail() == nullptr) {
        target->setJoinWaiters(waiter);
    } else {
        target->getJoinWaitersTail()->setWaitNext(waiter);
    }
    target->setJoinWaitersTail(waiter);
    exitCode = 0;
    return 1;
}

// Caller holds lock_ and has established both join consumption and stack
// retirement. Unlink the Thread before freeing it so task traversal cannot
// follow its old taskNext link into released storage.
void Task::reapJoinedThreadLocked(Thread* thread) {
    Thread** link = &threads_;
    while (*link && *link != thread) link = &(*link)->accessTaskNext();
    if (!*link) return;
    *link = thread->getTaskNext();
    if (mainThread_ == thread) mainThread_ = threads_;
    mm::freeHeap(thread);
}

// Complete the other half of the join/stack lifetime handshake. Either join or
// stack retirement may happen first; only the second frees Thread.
void Task::retireJoinedThread(Thread* thread) {
    InterruptSpinLockGuard guard(lock_);
    thread->setStackRetired(true);
    if (thread->isJoinConsumed()) reapJoinedThreadLocked(thread);
}

// A cancelled native IPC wait may have registered in a generic wait queue.
// Avoid sleeping if that call already failed, and detach the registration
// before returning so its owner does not retain a stale stack/thread link.
void blockWaitingThread() {
    if (!currentThread()->waitError()) task::blockCurrentThread();
    if (currentThread()->waitError()) currentThread()->cancelWait();
}

}  // namespace kernel::task
