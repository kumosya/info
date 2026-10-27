/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/reclaim.cc
 * Reference-owned work queue for deferred process resource reclamation.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace kernel::task {

// Create a dedicated ownerless worker so reclamation does not depend on the
// lifetime of a service or thread that requested another task's death.
void TaskReclaimer::init() {
    worker_ = Thread::create(
        nullptr,
        [](void* context) { static_cast<TaskReclaimer*>(context)->run(); },
        this);
    if (!worker_) {
        device::serialWrite("[TASK] unable to init task reclaimer\n");
        cpu::halt();
    }
}

// The queue lock protects both duplicate submission and head/tail updates.
// Retain before publishing resourceState_ = 1 so any observing destroy/wait
// caller can disappear without taking the cleanup obligation with it.
void TaskReclaimer::submit(Task& task) {
    InterruptSpinLockGuard guard(lock_);
    if (__atomic_load_n(&task.resourceState_, __ATOMIC_ACQUIRE) != 0) return;
    task.retain();
    __atomic_store_n(&task.resourceState_, 1U, __ATOMIC_RELEASE);
    task.reclaimNext_ = nullptr;
    if (tail_)
        tail_->reclaimNext_ = &task;
    else
        head_ = &task;
    tail_ = &task;
    wakeThread(worker_);
}

// Pop under the queue lock, reclaim outside it, then publish state 2 with
// release ordering. Empty-queue wait preparation is covered by that same lock,
// preventing submit from racing between the empty check and sleep. Keep the
// queue reference until notification work is also finished.
void TaskReclaimer::run() {
    while (true) {
        Task* task;
        {
            InterruptSpinLockGuard guard(lock_);
            task = head_;
            if (task) {
                head_ = task->reclaimNext_;
                if (!head_) tail_ = nullptr;
                task->reclaimNext_ = nullptr;
            } else
                prepareCurrentWait();
        }
        if (!task) {
            blockWaitingThread();
            continue;
        }
        const bool exited = task->destroyResources();
        __atomic_store_n(&task->resourceState_, 2U, __ATOMIC_RELEASE);
        // The requester may have died before notifying the parent.
        if (exited && !coreDump().pending(*task)) {
            auto parent = task->parentReference();
            if (parent) {
                parent->notifyChildStateChange();
                parent->notifyChildExit(*task);
            }
        }
        task->release();
    }
}

// Shared deferred-reclamation queue instance.
TaskReclaimer& taskReclaimer() {
    static TaskReclaimer instance;
    return instance;
}

// Worker-only resource teardown. First stop admission and all threads, then
// drop lock_ before waiting for CPU quiescence. Hardware shutdown and core
// capture precede page-table release; waits and cleanup registrations must
// disappear before their backing stacks. Return whether this was an observable
// process exit rather than a never-started construction failure.
bool Task::destroyResources() {
    uint64_t root;
    bool discard;
    {
        InterruptSpinLockGuard guard(lock_);
        discard         = state_ == TaskState::Created && threadCount_ == 0;
        acceptsThreads_ = false;
        for (Thread* thread = threads_; thread != nullptr;
             thread         = thread->getTaskNext()) {
            if (thread->getState() != ThreadState::Terminated) {
                task::terminateThread(thread);
            }
        }
        threadCount_ = 0;
        state_       = TaskState::Terminated;
    }
    // A Terminated state does not prove a remote CPU has left the old stack.
    // Do not destroy roots, mappings or stack-backed IPC until switches
    // finish.
    task::quiesceTask(this);
    device::resourceDomains().ownerExiting(*this);
    // Capture resident memory while the old root still exists. The snapshot
    // owns a separate buffer, allowing slow file output after task teardown.
    coreDump().capture(*this);
    {
        PreemptionSpinLockGuard guard(addressSpace_.lock());
        root = addressSpace_.replaceRoot(0);
    }
    // Revoke endpoint receiver ownership before disposing handles; recoverable
    // endpoints can preserve their identity for a replacement service
    // instance.
    Endpoint::closeOwner(this);
    for (Thread* thread = threads_; thread; thread = thread->getTaskNext()) {
        Endpoint::cancelThread(thread);
        thread->cancelWait();
        ThreadCleanup::releaseAll(*thread);
    }
    // Transfer child ownership before dropping this task's resources. Child
    // references can keep the Task object alive independently of its old
    // memory.
    adoptChildren();
    for (Thread* thread = threads_; thread;) {
        Thread* next = thread->getTaskNext();
        if (thread->getKernelStack()) mm::freeHeap(thread->getKernelStack());
        mm::freeHeap(thread);
        thread = next;
    }
    threads_    = nullptr;
    mainThread_ = nullptr;
    handles_.dispose();
    mm::maps().releaseTask(*this);
    ::kernel::mm::sharedMemory().releaseTaskMappings(*this);
    if (root != 0) mm::destroyAddressSpaceRoot(root);
    device::resourceDomains().addressSpaceReleased(*this);
    // A Created task with no threads has no useful zombie status to wait for.
    // Remove its list ownership directly. Normal exited tasks remain
    // discoverable until the parent consumes their status through wait.
    if (discard) {
        if (parent_ != nullptr) {
            InterruptSpinLockGuard parentGuard(parent_->lock_);
            Task** link = &parent_->firstChild_;
            while (*link != nullptr && *link != this) {
                link = &(*link)->nextSibling_;
            }
            if (*link == this) *link = nextSibling_;
            nextSibling_ = nullptr;
        }
        unlinkGlobal();
    }
    if (discard) release();
    return !discard;
}

}  // namespace kernel::task
