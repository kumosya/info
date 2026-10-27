/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/cfs.cc
 * Weighted runtime accounting and per-CPU runnable-thread admission.
 */

#include "kernel/cpu.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace kernel::task {

namespace {
// Index is nice + 20. Nice 0 has weight 1024; lower nice earns more actual CPU
// time per unit of virtual runtime. Values mirror the Linux weight scale.
constexpr uint32_t niceWeights[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
    1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
    110,   87,    70,    56,    45,    36,    29,    23,    18,    15};

// Convert elapsed nanoseconds into weighted nanoseconds. Round upward so short
// charged intervals do not disappear through integer truncation.
constexpr uint64_t calculateVruntimeDelta(uint64_t runtime, uint32_t weight) {
    return (runtime * 1024ULL + weight - 1) / weight;
}

static_assert(calculateVruntimeDelta(1000000, niceWeights[0]) <
              calculateVruntimeDelta(1000000, niceWeights[20]));
static_assert(calculateVruntimeDelta(1000000, niceWeights[20]) <
              calculateVruntimeDelta(1000000, niceWeights[39]));
}  // namespace

// Admit a live, detached thread to an allowed CPU. Reuse its previous CPU when
// possible, otherwise choose an online CPU allowed by the affinity mask.
// Success means admission (or deferred admission while frozen), not that the
// thread is still Ready when another CPU observes the return value.
bool enqueueThread(Thread* thread) {
    if (thread == nullptr || thread->getInRunQueue() ||
        thread->getState() == ThreadState::Terminated) {
        return thread && thread->getState() != ThreadState::Terminated;
    }
    uint32_t targetCpuId                   = thread->getCpuId();
    ::kernel::cpu::CpuLocalData* targetCpu = cpu::getCpuLocalData(targetCpuId);

    if (targetCpu == nullptr || targetCpu->getRunQueue() == nullptr ||
        (thread->getCpuAffinity() & (1ULL << targetCpuId)) == 0) {
        targetCpu = nullptr;
        for (uint32_t cpuId = 0;
             cpuId < cpu::getDiscoveredCpuCount() && cpuId < 64; ++cpuId) {
            ::kernel::cpu::CpuDescriptor* descriptor =
                cpu::getCpuDescriptor(cpuId);
            if ((thread->getCpuAffinity() & (1ULL << cpuId)) != 0 &&
                descriptor != nullptr &&
                descriptor->getState().load(MemoryOrder::Acquire) ==
                    ::kernel::cpu::CpuState::Online &&
                descriptor->getLocalData() != nullptr &&
                descriptor->getLocalData()->getRunQueue() != nullptr) {
                targetCpu   = descriptor->getLocalData();
                targetCpuId = cpuId;
                break;
            }
        }
    }
    if (targetCpu == nullptr) {
        return false;
    }
    RunQueue* queue      = targetCpu->getRunQueue();
    const uint64_t flags = cpu::disableInterrupts();

    // Recheck membership after acquiring the destination lock: the earlier
    // checks did not exclude concurrent wakeup or termination.
    queue->getLock().lock();

    if (thread->getInRunQueue() ||
        thread->getState() == ThreadState::Terminated) {
        queue->getLock().unlock();
        cpu::restoreInterrupts(flags);
        return thread->getState() != ThreadState::Terminated;
    }
    // A pager-recovery freeze must cover newly admitted work too. Remember
    // that this thread should become Ready on thaw, without putting it in the
    // tree.
    if (thread->getTask() && thread->getTask()->memoryFrozen()) {
        thread->setStateBeforeStop(ThreadState::Ready);
        thread->setState(ThreadState::Stopped);
        queue->getLock().unlock();
        cpu::restoreInterrupts(flags);
        return true;
    }
    // Do not grant accumulated CPU credit simply because a thread slept or
    // arrived from a queue with an earlier virtual clock.
    if (thread->getVruntime() < queue->getMinVruntime()) {
        thread->setVruntime(queue->getMinVruntime());
    }
    thread->setState(ThreadState::Ready);
    thread->setCpuId(targetCpuId);
    queue->insertNode(thread);
    queue->getLock().unlock();
    cpu::restoreInterrupts(flags);

    if (targetCpuId != cpu::getCurrentId()) {
        cpu::requestRemoteReschedule(targetCpuId);
    }
    return true;
}

// Remove a queued thread under its owning CPU queue lock. This does not stop a
// thread already executing on a CPU; termination also needs a reschedule
// request and, before freeing resources, quiesceTask.
void dequeueThread(Thread* thread) {
    if (thread == nullptr || !thread->getInRunQueue()) {
        return;
    }
    ::kernel::cpu::CpuLocalData* cpu = cpu::getCpuLocalData(thread->getCpuId());
    if (cpu == nullptr || cpu->getRunQueue() == nullptr) {
        return;
    }
    RunQueue* queue      = cpu->getRunQueue();
    const uint64_t flags = cpu::disableInterrupts();
    queue->getLock().lock();
    queue->removeNode(thread);
    queue->getLock().unlock();
    cpu::restoreInterrupts(flags);
}

// Inspect the local minimum, falling back to idle. This does not remove the
// thread or reserve it across the unlocked return; schedule performs that
// selection and state transition atomically under the queue lock.
Thread* pickNextThread() {
    RunQueue* queue      = cpu::getCurrent()->getRunQueue();
    const uint64_t flags = cpu::disableInterrupts();
    queue->getLock().lock();
    Thread* thread = queue->getLeftmost() == nullptr ? queue->getIdleThread()
                                                     : queue->getLeftmost();
    queue->getLock().unlock();
    cpu::restoreInterrupts(flags);
    return thread;
}

// Clamp the requested nice before indexing the fixed forty-entry table.
uint32_t getSchedulerWeight(int32_t nice) {
    if (nice < -20) {
        nice = -20;
    } else if (nice > 19) {
        nice = 19;
    }
    return niceWeights[nice + 20];
}

// Change one thread's nice while serialized with its CPU accounting. value is
// an increment when relative, otherwise an absolute nice. result receives the
// clamped value; return a positive errno on failure. Only privileged callers
// or RLIMIT_NICE authorization may lower nice. Retry if the thread moved CPUs
// before its queue lock was acquired.
int changeNice(Thread& thread, int64_t value, bool relative, bool privileged,
               uint64_t limit, int& result) {
    while (true) {
        const auto id   = thread.getCpuId();
        auto* targetCpu = cpu::getCpuLocalData(id);
        if (!targetCpu || !targetCpu->getRunQueue()) return 3;
        RunQueue& queue = *targetCpu->getRunQueue();
        InterruptSpinLockGuard guard(queue.getLock());
        if (id != thread.getCpuId()) continue;
        if (thread.getState() == ThreadState::Terminated) return 3;
        const int old = thread.getNice();
        int64_t next  = relative ? static_cast<int64_t>(old) + value : value;
        if (next < -20) next = -20;
        if (next > 19) next = 19;
        // RLIMIT_NICE encodes the lowest permitted nice as 20 - limit. Zero
        // grants no priority increase, while a limit of at least 40 permits
        // nice -20.
        const int floor = limit >= 40 ? -20 : 20 - static_cast<int>(limit);
        if (next < old && !privileged && (!limit || next < floor)) return 13;
        // Charge elapsed runtime with the old weight before changing it.
        const uint64_t now     = time::getMonotonicTime();
        const uint64_t elapsed = now - queue.getClock();
        queue.setClock(now);
        Thread* running = queue.getCurrentThread();
        if (running && running != queue.getIdleThread() &&
            running->getState() == ThreadState::Running) {
            running->setRuntime(running->getRuntime() + elapsed);
            running->setSliceRuntime(running->getSliceRuntime() + elapsed);
            running->setVruntime(
                running->getVruntime() +
                calculateVruntimeDelta(elapsed, running->getWeight()));
            if (running->getTask()) running->getTask()->accountCpu(elapsed);
        }
        thread.setNice(static_cast<int>(next));
        thread.setWeight(getSchedulerWeight(next));
        targetCpu->setRescheduleRequested(true);
        if (id != cpu::getCurrentId()) cpu::requestRemoteReschedule(id);
        result = static_cast<int>(next);
        return 0;
    }
}

// Re-admit a Ready thread on an online CPU allowed by its affinity mask.
// Check the destination bit: the old CPU can intentionally be excluded by a
// newly installed mask. Store the destination before enqueueThread selects a
// queue, otherwise admission would reuse the previous CPU instead.
bool Thread::migrate(uint32_t cpu_id) {
    ::kernel::cpu::CpuDescriptor* target = cpu::getCpuDescriptor(cpu_id);
    if (cpu_id >= 64 || target == nullptr ||
        target->getState().load(MemoryOrder::Acquire) !=
            ::kernel::cpu::CpuState::Online ||
        (cpuAffinity & (1ULL << cpu_id)) == 0 || state != ThreadState::Ready) {
        return false;
    }
    task::dequeueThread(this);
    cpuId = cpu_id;
    // Admission is the success condition. The destination CPU can have already
    // selected or blocked the thread by the time enqueueThread returns.
    return task::enqueueThread(this);
}

// Store a nonzero allowed-CPU mask, keeping the current placement if allowed.
// Otherwise try migration. The new mask remains stored if migration fails;
// callers must not interpret false as a rollback of the affinity change.
bool Thread::setAffinity(uint64_t cpuMask) {
    if (cpuMask == 0) {
        return false;
    }
    cpuAffinity = cpuMask;
    if ((cpuMask & (1ULL << cpuId)) != 0) {
        return true;
    }
    for (uint32_t i = 0; i < cpu::getDiscoveredCpuCount() && i < 64; ++i) {
        if ((cpuMask & (1ULL << i)) != 0 && migrate(i)) {
            return true;
        }
    }
    return false;
}

// Charge elapsed actual and virtual time to the non-idle running thread. The
// caller holds this queue lock. Return whether the CPU should request
// rescheduling; queue selection and the actual context switch happen later.
bool RunQueue::accountTick(Thread* current, uint64_t elapsed) {
    RunQueue* queue = this;
    if (current != nullptr && current != queue->getIdleThread() &&
        current->getState() == ThreadState::Running) {
        current->setRuntime(current->getRuntime() + (elapsed));
        if (current->getTask()) current->getTask()->accountCpu(elapsed);
        current->setSliceRuntime(current->getSliceRuntime() + (elapsed));
        const uint64_t delta =
            calculateVruntimeDelta(elapsed, current->getWeight());
        current->setVruntime(current->getVruntime() + (delta));
        if (current->getSliceRuntime() >= current->getTimeSlice() ||
            (queue->getLeftmost() != nullptr &&
             queue->getLeftmost()->getVruntime() < current->getVruntime())) {
            current->setSliceRuntime(0);
            return true;
        }
    }
    return false;
}

}  // namespace kernel::task
