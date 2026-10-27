/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/exit.cc
 * Thread exit accounting, process termination, and parent notification.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/task.h"

namespace kernel::task {

// Exit code read under the task state lock.
int Task::exitCode() {
    InterruptSpinLockGuard guard(lock_);
    return exitCode_;
}

// Consume each thread exit once, detach its join waiters, and detect the last
// user-task thread. Logical task termination submits cleanup; handle closure
// and page-table destruction must wait for the reclaimer to observe that no
// CPU is still executing the task.
void Task::notifyThreadExit(Thread* thread) {
    Thread* waiters     = nullptr;
    bool taskTerminated = false;
    {
        InterruptSpinLockGuard guard(lock_);
        waiters = thread->getJoinWaiters();
        thread->setJoinWaiters(nullptr);
        thread->setJoinWaitersTail(nullptr);
        if (!thread->isExitAccounted() && threadCount_ != 0) {
            --threadCount_;
        }
        const bool newlyExited = !thread->isExitAccounted();
        thread->setExitAccounted(true);
        if (newlyExited && threadCount_ == 0 && this != task::getIdle()) {
            state_          = TaskState::Terminated;
            acceptsThreads_ = false;
            if (!exitSignal_) exitCode_ = thread->getExitCode();
            // The reclaimer detaches endpoint ownership before closing
            // handles.
            taskTerminated = true;
        }
    }
    while (waiters != nullptr) {
        Thread* waiter = waiters;
        waiters        = waiters->getWaitNext();
        waiter->setWaitNext(nullptr);
        waiter->clearWaitRegistration();
        wakeThread(waiter);
    }
    if (taskTerminated) {
        taskReclaimer().submit(*this);
    }
}

// Close thread admission before walking siblings so concurrent creation cannot
// escape termination. Mark sibling exits accounted here, avoiding a second
// decrement if their exit path is later observed. Current-thread termination
// is completed separately by exitCurrentThread.
void Task::terminateOtherThreads(int exitCode) {
    InterruptSpinLockGuard guard(lock_);
    acceptsThreads_ = false;
    if (!exitSignal_) exitCode_ = exitCode;
    for (Thread* thread = threads_; thread != nullptr;
         thread         = thread->getTaskNext()) {
        if (thread != currentThread() &&
            thread->getState() != ThreadState::Terminated) {
            task::terminateThread(thread);
            thread->setExitAccounted(true);
            cpu::requestRemoteReschedule(
                static_cast<uint32_t>(thread->getCpuId()));
            if (threadCount_ != 0) {
                --threadCount_;
            }
        }
    }
    if (threadCount_ == 0) {
        state_ = TaskState::Terminated;
        taskReclaimer().submit(*this);
    }
}

// Submit noncurrent, non-kernel task resources and wait for completion. The
// worker owns its own reference; this waiting caller may die without
// abandoning cleanup. No waiter stack address is handed to the queue.
void Task::destroy() {
    if (this == task::getIdle() || this == task::getCurrent()) {
        return;
    }
    TaskReference lifetime(this);
    taskReclaimer().submit(*this);
    const uint64_t flags = cpu::disableInterrupts();
    cpu::enableInterrupts();
    // No stack pointer from this waiter is stored in the reclaimer. Killing or
    // cancelling it cannot strand the queue or its owned task reference.
    while (__atomic_load_n(&resourceState_, __ATOMIC_ACQUIRE) != 2U)
        task::yieldThread();
    cpu::restoreInterrupts(flags);
}

// Process exit first stops siblings, then exits the calling thread. A kernel
// task/ownerless worker only exits its thread, never the kernel task.
[[noreturn]] void exitCurrent(int exitCode) {
    Task* task = getCurrent();
    if (task == nullptr || task == task::getIdle()) {
        exitCurrentThread(exitCode);
    }
    task->terminateOtherThreads(exitCode);
    exitCurrentThread(exitCode);
}

// Cancel stack-backed IPC and temporary references before notifying the owner.
// Mark the thread dead and switch away without returning; its own kernel stack
// must be freed by a later context or the task reclaimer.
[[noreturn]] void exitCurrentThread(int exitCode) {
    const uint64_t flags = cpu::disableInterrupts();
    Thread* thread       = currentThread();
    thread->setExitCode(exitCode);
    thread->setState(ThreadState::Terminated);
    Endpoint::cancelThread(thread);
    ThreadCleanup::releaseAll(*thread);
    if (thread->getTask() != nullptr) {
        thread->getTask()->notifyThreadExit(thread);
    }
    task::requestReschedule();
    task::schedule();
    cpu::restoreInterrupts(flags);
    cpu::halt();
}

// Publish one SIGCHLD event after core status is final or no longer pending.
// The atomic once flag prevents duplicate notification by the reclaimer and
// core writer/watchdog. CLD_DUMPED requires actual Complete/Truncated output,
// not merely a requested or failed core snapshot.
void Task::notifyChildExit(Task& child) {
    if (__atomic_exchange_n(&child.exitNotified_, true, __ATOMIC_ACQ_REL))
        return;
    os::wire::ExitInfo info{};
    child.getExitInfo(info);
    {
        InterruptSpinLockGuard guard(lock_);
        childSignalInfo_       = {};
        childSignalInfo_.signo = 17;
        childSignalInfo_.pid   = child.id();
        childSignalInfo_.uid   = child.uid();
        childSignalInfo_.code =
            !info.signal ? 1
            : (info.coreState ==
                   static_cast<uint32_t>(os::wire::CoreState::Complete) ||
               info.coreState ==
                   static_cast<uint32_t>(os::wire::CoreState::Truncated))
                ? 3
                : 2;
        childSignalInfo_.status = info.signal ? static_cast<int>(info.signal)
                                              : child.exitCode() & 0xff;
    }
    notifyChildStateChange();
    deliverSignal(17);
}

}  // namespace kernel::task
