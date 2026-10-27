/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/schedule.cc
 * Scheduling, sleep and wake transitions, and completed-switch tracking.
 */

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace kernel::task {

namespace {

::Atomic<bool> schedulerInitialized{false};
// Per-CPU handoff records outlive the old stack. selected currentTask can
// already name the next task before switchTo finishes, so executingTasks is
// published only after the physical stack/context switch completes.
Thread* retiredStacks[64]{};
Task* executingTasks[64]{};
// Idle keeps interrupts enabled while halted, so timer and reschedule
// interrupts can make newly runnable work visible without busy polling.
void idleEntry(void*) {
    while (true) {
        asm volatile("sti; hlt");
    }
}

// Account the tick here; interrupt-return/preemption handling decides when a
// requested context switch can safely occur.
void timerHandler(kernel::cpu::Context*) { schedulerTick(); }

}  // namespace

// Create the local run queue before publishing initialized state. The idle
// thread is created through the normal path, then removed from the tree so it
// is chosen only when no ordinary thread is ready. APs initialize their own
// queue and local timer through the same entry.
void initScheduler() {
    ::kernel::cpu::CpuLocalData* cpu = cpu::getCurrent();
    RunQueue* queue =
        static_cast<RunQueue*>(mm::allocateHeap(sizeof(RunQueue)));
    if (queue == nullptr) {
        device::serialWrite("[SCHED] unable to allocate run queue\n");
        cpu::halt();
    }
    queue->clear();
    queue->setCurrentThread(cpu->getCurrentThread());
    queue->setMinVruntime(cpu->getCurrentThread()->getVruntime());
    queue->setClock(time::getMonotonicTime());
    cpu->setRunQueue(queue);
    __atomic_store_n(&executingTasks[cpu->getCpuId()], cpu->getCurrentTask(),
                     __ATOMIC_RELEASE);
    schedulerInitialized.store(true, MemoryOrder::Release);

    Thread* idle = Thread::create(nullptr, idleEntry, nullptr);
    task::dequeueThread(idle);
    queue->setIdleThread(idle);
    cpu->setIdleThread(idle);

    cpu::registerHandler(32, timerHandler);
    cpu::initTimer(32, 1000000);
    device::serialWrite("[SCHED] per-CPU CFS run queue initialized\n");
}

// Read the publication flag with acquire ordering before using scheduler
// facilities; individual CPU paths must still check for their local queue.
bool isSchedulerInitialized() {
    return schedulerInitialized.load(MemoryOrder::Acquire);
}

// Choose the next local thread while interrupts are disabled and the queue is
// locked. Only a still-Running previous thread is requeued: blocked, sleeping,
// stopped, and terminated states already describe why it must not run. Release
// the queue lock before switching stacks; no new context may inherit ownership
// of that lock.
void schedule() {
    if (!task::isSchedulerInitialized()) {
        return;
    }
    const uint64_t flags             = cpu::disableInterrupts();
    ::kernel::cpu::CpuLocalData* cpu = cpu::getCurrent();
    RunQueue* queue                  = cpu->getRunQueue();
    queue->getLock().lock();
    cpu->setRescheduleRequested(false);
    Thread* previous = cpu->getCurrentThread();
    if (previous != nullptr && previous != queue->getIdleThread() &&
        previous->getState() == ThreadState::Running) {
        previous->setState(ThreadState::Ready);
        queue->insertNode(previous);
    }
    Thread* next = queue->getLeftmost();
    if (next != nullptr) {
        queue->removeNode(next);
    } else {
        next = queue->getIdleThread();
    }
    // Publish the selected thread and reset its slice under the same lock as
    // queue removal, so remote wake/termination sees a coherent selection.
    next->setState(ThreadState::Running);
    next->setSliceRuntime(0);
    next->setCpuId(cpu->getCpuId());
    cpu->setCurrentThread(next);
    cpu->setCurrentTask(next->getTask());
    queue->setCurrentThread(next);
    queue->getLock().unlock();

    if (next != previous) {
        if (previous->getState() == ThreadState::Terminated)
            retiredStacks[cpu->getCpuId()] = previous;
        cpu::getCurrent()->setKernelStack(next->getKernelStackTop());
        // Install the destination page tables before entering its saved kernel
        // continuation. Ownerless workers use the shared kernel root. The old
        // stack still exists until finishContextSwitch confirms the switch
        // completed.
        mm::switchAddressSpace(
            next->getTask() == nullptr
                ? mm::getKernelAddressSpaceRoot()
                : next->getTask()->addressSpace().rootPhysicalAddress());
        previous->switchTo(next);
        finishContextSwitch();
    }
    cpu::restoreInterrupts(flags);
}

// Finish work that cannot run on the retired stack itself. Active IPC or
// cleanup records can still contain pointers into that stack, so only free it
// when those references are gone. Publishing executingTasks last closes the
// selection-versus-execution gap observed by remote quiescence.
void finishContextSwitch() {
    // The previous CPU context is no longer executing on this stack.
    const uint64_t flags = cpu::disableInterrupts();
    Thread*& retired     = retiredStacks[cpu::getCurrentId()];
    Thread* thread       = retired;
    retired              = nullptr;
    // A cancelled synchronous call can still have a reply targeting its stack.
    if (thread && !thread->hasActiveIpcCall() && !thread->hasCleanup() &&
        thread->getKernelStack()) {
        mm::freeHeap(thread->getKernelStack());
        thread->setKernelStack(nullptr);
        thread->setKernelStackTop(nullptr);
        if (thread->getTask()) thread->getTask()->retireJoinedThread(thread);
    }
    __atomic_store_n(&executingTasks[cpu::getCurrentId()],
                     currentThread()->getTask(), __ATOMIC_RELEASE);
    cpu::restoreInterrupts(flags);
}

// Detach queued/sleeping work and mark it dead under its CPU queue lock. A
// remotely executing thread may still be using its stack; requesting
// reschedule is not proof that the switch has finished.
void terminateThread(Thread* thread) {
    if (!thread) return;
    ::kernel::cpu::CpuLocalData* target =
        cpu::getCpuLocalData(thread->getCpuId());
    if (target && target->getRunQueue()) {
        InterruptSpinLockGuard guard(target->getRunQueue()->getLock());
        if (thread->getInRunQueue()) target->getRunQueue()->removeNode(thread);
        Thread** link = &target->getRunQueue()->accessSleepingThreads();
        while (*link && *link != thread) link = &(*link)->accessWaitNext();
        if (*link) {
            *link = thread->getWaitNext();
            thread->setWaitNext(nullptr);
        }
        thread->setState(ThreadState::Terminated);
    } else {
        thread->setState(ThreadState::Terminated);
    }
    cpu::requestRemoteReschedule(thread->getCpuId());
}

// Wait until every other CPU has both stopped selecting and finished executing
// task. This handles the window where its current pointer has changed but its
// stack switch has not. Do not hold task, address-space, or run-queue locks on
// entry: remote CPUs may need them to make progress.
void quiesceTask(Task* task) {
    // No task, address-space or run-queue lock may be held here. The selected
    // context and the completed stack switch cover both sides of switchTo.
    const uint64_t flags = cpu::disableInterrupts();
    cpu::enableInterrupts();
    while (true) {
        bool busy = false;
        for (uint32_t id = 0; id < cpu::getDiscoveredCpuCount(); ++id) {
            if (id == cpu::getCurrentId()) continue;
            auto* target = cpu::getCpuLocalData(id);
            if (!target || !target->getRunQueue()) continue;
            InterruptSpinLockGuard guard(target->getRunQueue()->getLock());
            Thread* selected = target->getCurrentThread();
            Task* executing =
                __atomic_load_n(&executingTasks[id], __ATOMIC_ACQUIRE);
            if ((selected && selected->getTask() == task) ||
                executing == task) {
                busy = true;
                cpu::requestRemoteReschedule(id);
            }
        }
        if (!busy) break;
        asm volatile("pause" ::: "memory");
    }
    cpu::restoreInterrupts(flags);
}

// Make the current runnable thread compete again instead of sleeping; schedule
// will reinsert it unless its state changed in the meantime.
void yieldThread() {
    task::requestReschedule();
    task::schedule();
}

// Clear a stale wake credit before linking the current thread into the
// condition owner's wait list. The condition lock must cover both condition
// checking and registration so a producer cannot slip between them.
void prepareCurrentWait() {
    RunQueue* queue = cpu::getCurrent()->getRunQueue();
    InterruptSpinLockGuard guard(queue->getLock());
    currentThread()->accessWakePending() = false;
}

// Consume an early wake under the same queue lock used by wakeThread. If the
// producer ran after registration but before this call, wakePending prevents
// entering Blocked and losing that notification.
void blockCurrentThread() {
    const uint64_t flags = cpu::disableInterrupts();
    RunQueue* queue      = cpu::getCurrent()->getRunQueue();
    Thread* thread       = currentThread();
    queue->getLock().lock();
    // The pending wake and the state transition share the same lock as wake. A
    // waiter can have been preempted (Ready) before it reaches this point.
    const bool pending          = thread->accessWakePending();
    thread->accessWakePending() = false;
    if (!pending) thread->setState(ThreadState::Blocked);
    queue->getLock().unlock();
    if (!pending) schedule();
    cpu::restoreInterrupts(flags);
}

// Serialize wakeup with blockCurrentThread on the destination queue. A
// running/ready waiter gets a wake credit; a blocked/sleeping waiter becomes
// runnable. A stopped waiter only records its post-stop state, so waking a
// condition does not bypass job-control suspension.
void wakeThread(Thread* thread) {
    if (thread == nullptr) return;
    ::kernel::cpu::CpuLocalData* target =
        cpu::getCpuLocalData(thread->getCpuId());
    if (target == nullptr || target->getRunQueue() == nullptr) return;
    RunQueue* queue      = target->getRunQueue();
    const uint64_t flags = cpu::disableInterrupts();
    queue->getLock().lock();
    const ThreadState state = thread->getState();
    bool runnable           = false;
    if (state == ThreadState::Running || state == ThreadState::Ready) {
        thread->accessWakePending() = true;
    } else if (state == ThreadState::Stopped) {
        thread->setStateBeforeStop(ThreadState::Ready);
        thread->accessWakePending() = true;
    } else if (state == ThreadState::Blocked ||
               state == ThreadState::Sleeping) {
        if (state == ThreadState::Sleeping) {
            Thread** link = &queue->accessSleepingThreads();
            while (*link != nullptr && *link != thread)
                link = &(*link)->accessWaitNext();
            if (*link == thread) *link = thread->getWaitNext();
            thread->setWaitNext(nullptr);
        }
        thread->setState(ThreadState::Ready);
        if (thread->getVruntime() < queue->getMinVruntime())
            thread->setVruntime(queue->getMinVruntime());
        if (!thread->getInRunQueue()) queue->insertNode(thread);
        target->setRescheduleRequested(true);
        runnable = true;
    }
    queue->getLock().unlock();
    cpu::restoreInterrupts(flags);
    if (runnable && thread->getCpuId() != cpu::getCurrentId())
        cpu::requestRemoteReschedule(thread->getCpuId());
}

// Set a local scheduling request without forcing a switch in this context.
void requestReschedule() { cpu::getCurrent()->setRescheduleRequested(true); }

// Measure actual elapsed monotonic nanoseconds rather than assuming one fixed
// duration per interrupt. Account the running thread, then move all due
// sleepers into the tree. Expire IPC waits only after releasing the run queue
// lock because endpoint completion may itself wake threads.
void schedulerTick() {
    ::kernel::cpu::CpuLocalData* cpu = cpu::getCurrent();
    if (!task::isSchedulerInitialized() || cpu->getRunQueue() == nullptr) {
        return;
    }
    RunQueue* queue = cpu->getRunQueue();
    queue->getLock().lock();
    const uint64_t now     = time::getMonotonicTime();
    const uint64_t elapsed = now - queue->getClock();
    queue->setClock(now);
    cpu->addTickCount(1);
    if (queue->accountTick(cpu->getCurrentThread(), elapsed))
        cpu->setRescheduleRequested(true);
    while (
        queue->getSleepingThreads() != nullptr &&
        time::deadlineReached(
            now, queue->getSleepingThreads()->getWakeDeadlineNanoseconds())) {
        Thread* thread = queue->getSleepingThreads();
        queue->setSleepingThreads(thread->getWaitNext());
        thread->setWaitNext(nullptr);
        if (thread->getState() == ThreadState::Stopped) {
            thread->setStateBeforeStop(ThreadState::Ready);
            continue;
        }
        if (thread->getState() == ThreadState::Terminated) {
            continue;
        }
        thread->setState(ThreadState::Ready);
        queue->insertNode(thread);
        cpu->setRescheduleRequested(true);
    }
    queue->getLock().unlock();
    Endpoint::expireWaits(now);
}

// Zero milliseconds yields; otherwise link the current thread into the local
// deadline-sorted sleep list before switching away. Saturate deadline
// arithmetic so a large duration cannot wrap into an immediate wakeup.
void sleepCurrentThread(uint64_t milliseconds) {
    if (milliseconds == 0) {
        task::yieldThread();
        return;
    }
    const uint64_t flags = cpu::disableInterrupts();
    RunQueue* queue      = cpu::getCurrent()->getRunQueue();
    Thread* current      = currentThread();
    queue->getLock().lock();
    const uint64_t duration = time::milliseconds2Nanoseconds(milliseconds);
    const uint64_t now      = time::getMonotonicTime();
    current->setState(ThreadState::Sleeping);
    current->setWakeDeadlineNanoseconds(
        duration > ~0ULL - now ? ~0ULL : now + duration);
    Thread** link = &queue->accessSleepingThreads();
    while (*link != nullptr &&
           !time::deadlineReached((*link)->getWakeDeadlineNanoseconds(),
                                  current->getWakeDeadlineNanoseconds())) {
        link = &(*link)->accessWaitNext();
    }
    current->setWaitNext(*link);
    *link = current;
    queue->getLock().unlock();
    task::schedule();
    cpu::restoreInterrupts(flags);
}

}  // namespace kernel::task
