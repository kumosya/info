#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace {

kernel::Atomic<bool> schedulerInitialized{false};
kernel::Thread* retiredStacks[64]{};
kernel::Task* executingTasks[64]{};
constexpr uint32_t niceWeights[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
    1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
    110,   87,    70,    56,    45,    36,    29,    23,    18,    15};

constexpr uint64_t calculateVruntimeDelta(uint64_t runtime, uint32_t weight) {
    return (runtime * 1024ULL + weight - 1) / weight;
}

static_assert(calculateVruntimeDelta(1000000, niceWeights[0]) <
              calculateVruntimeDelta(1000000, niceWeights[20]));
static_assert(calculateVruntimeDelta(1000000, niceWeights[20]) <
              calculateVruntimeDelta(1000000, niceWeights[39]));

void idleEntry(void*) {
    while (true) {
        asm volatile("sti; hlt");
    }
}

void timerHandler(kernel::interrupt::Context*) {
    kernel::schedule::schedulerTick();
}

}  // namespace

namespace kernel::schedule {

void init() {
    CpuLocalData* cpu = cpu::getCurrent();
    RunQueue* queue =
        static_cast<RunQueue*>(heap::kernelAllocate(sizeof(RunQueue)));
    if (queue == nullptr) {
        serial::serialWrite("[SCHED] unable to allocate run queue\n");
        cpu::halt();
    }
    RunQueue::clearRunQueue(queue);
    queue->setCurrentThread(cpu->getCurrentThread());
    queue->setMinVruntime(cpu->getCurrentThread()->getVruntime());
    queue->setClock(time::getMonotonicTime());
    cpu->setRunQueue(queue);
    __atomic_store_n(&executingTasks[cpu->getCpuId()], cpu->getCurrentTask(), __ATOMIC_RELEASE);
    schedulerInitialized.store(true, MemoryOrder::Release);

    Thread* idle = Thread::create(nullptr, idleEntry, nullptr);
    schedule::dequeueThread(idle);
    queue->setIdleThread(idle);
    cpu->setIdleThread(idle);

    interrupt::registerHandler(32, timerHandler);
    localapic::initTimer(32, 1000000);
    serial::serialWrite("[SCHED] per-CPU CFS run queue initialized\n");
}

bool isSchedulerInitialized() {
    return schedulerInitialized.load(MemoryOrder::Acquire);
}

bool enqueueThread(Thread* thread) {
    if (thread == nullptr || thread->getInRunQueue() ||
        thread->getState() == ThreadState::Terminated) {
        return thread && thread->getState() != ThreadState::Terminated;
    }
    uint32_t targetCpuId    = thread->getCpuId();
    CpuLocalData* targetCpu = smp::getCpuLocalData(targetCpuId);
    if (targetCpu == nullptr || targetCpu->getRunQueue() == nullptr ||
        (thread->getCpuAffinity() & (1ULL << targetCpuId)) == 0) {
        targetCpu = nullptr;
        for (uint32_t cpuId = 0;
             cpuId < smp::getDiscoveredCpuCount() && cpuId < 64; ++cpuId) {
            CpuDescriptor* descriptor = smp::getCpuDescriptor(cpuId);
            if ((thread->getCpuAffinity() & (1ULL << cpuId)) != 0 &&
                descriptor != nullptr &&
                descriptor->getState().load(MemoryOrder::Acquire) ==
                    CpuState::Online &&
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
    const uint64_t flags = interrupt::disable();
    queue->getLock().lock();
    if (thread->getInRunQueue() ||
        thread->getState() == ThreadState::Terminated) {
        queue->getLock().unlock();
        interrupt::restore(flags);
        return thread->getState() != ThreadState::Terminated;
    }
    if (thread->getVruntime() < queue->getMinVruntime()) {
        thread->setVruntime(queue->getMinVruntime());
    }
    thread->setState(ThreadState::Ready);
    thread->setCpuId(targetCpuId);
    queue->insertNode(thread);
    queue->getLock().unlock();
    interrupt::restore(flags);
    if (targetCpuId != cpu::getCurrentId()) {
        smp::requestRemoteReschedule(targetCpuId);
    }
    return true;
}

void dequeueThread(Thread* thread) {
    if (thread == nullptr || !thread->getInRunQueue()) {
        return;
    }
    CpuLocalData* cpu = smp::getCpuLocalData(thread->getCpuId());
    if (cpu == nullptr || cpu->getRunQueue() == nullptr) {
        return;
    }
    RunQueue* queue      = cpu->getRunQueue();
    const uint64_t flags = interrupt::disable();
    queue->getLock().lock();
    queue->removeNode(thread);
    queue->getLock().unlock();
    interrupt::restore(flags);
}

Thread* pickNextThread() {
    RunQueue* queue      = cpu::getCurrent()->getRunQueue();
    const uint64_t flags = interrupt::disable();
    queue->getLock().lock();
    Thread* thread = queue->getLeftmost() == nullptr ? queue->getIdleThread()
                                                     : queue->getLeftmost();
    queue->getLock().unlock();
    interrupt::restore(flags);
    return thread;
}

void schedule() {
    if (!schedule::isSchedulerInitialized()) {
        return;
    }
    const uint64_t flags = interrupt::disable();
    CpuLocalData* cpu    = cpu::getCurrent();
    RunQueue* queue      = cpu->getRunQueue();
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
        memory::switchAddressSpace(
            next->getTask() == nullptr
                ? memory::getKernelAddressSpaceRoot()
                : next->getTask()->addressSpace().rootPhysicalAddress());
        previous->switchTo(next);
        finishContextSwitch();
    }
    interrupt::restore(flags);
}

void finishContextSwitch() {
    // The previous CPU context is no longer executing on this stack.
    const uint64_t flags = interrupt::disable();
    Thread*& retired = retiredStacks[cpu::getCurrentId()];
    Thread* thread = retired;
    retired = nullptr;
    // A cancelled synchronous call can still have a reply targeting its stack.
    if (thread && !thread->hasActiveIpcCall() && !thread->hasCleanup() && thread->getKernelStack()) {
        heap::kernelFree(thread->getKernelStack());
        thread->setKernelStack(nullptr);
        thread->setKernelStackTop(nullptr);
        if (thread->getTask()) thread->getTask()->retireJoinedThread(thread);
    }
    __atomic_store_n(&executingTasks[cpu::getCurrentId()], Thread::getCurrent()->getTask(), __ATOMIC_RELEASE);
    interrupt::restore(flags);
}

void terminateThread(Thread* thread) {
    if (!thread) return;
    CpuLocalData* target = smp::getCpuLocalData(thread->getCpuId());
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
    smp::requestRemoteReschedule(thread->getCpuId());
}

void quiesceTask(Task* task) {
    // No task, address-space or run-queue lock may be held here. The selected
    // context and the completed stack switch cover both sides of switchTo.
    const uint64_t flags = interrupt::disable();
    interrupt::enable();
    for (;;) {
        bool busy = false;
        for (uint32_t id = 0; id < smp::getDiscoveredCpuCount(); ++id) {
            if (id == cpu::getCurrentId()) continue;
            auto* target = smp::getCpuLocalData(id);
            if (!target || !target->getRunQueue()) continue;
            InterruptSpinLockGuard guard(target->getRunQueue()->getLock());
            Thread* selected = target->getCurrentThread();
            Task* executing = __atomic_load_n(&executingTasks[id], __ATOMIC_ACQUIRE);
            if ((selected && selected->getTask() == task) || executing == task) {
                busy = true;
                smp::requestRemoteReschedule(id);
            }
        }
        if (!busy) break;
        asm volatile("pause" ::: "memory");
    }
    interrupt::restore(flags);
}

void yieldThread() {
    schedule::requestReschedule();
    schedule::schedule();
}

void prepareCurrentWait() {
    RunQueue* queue = cpu::getCurrent()->getRunQueue();
    InterruptSpinLockGuard guard(queue->getLock());
    Thread::getCurrent()->accessWakePending() = false;
}

void blockCurrentThread() {
    const uint64_t flags = interrupt::disable();
    RunQueue* queue = cpu::getCurrent()->getRunQueue();
    Thread* thread = Thread::getCurrent();
    queue->getLock().lock();
    // The pending wake and the state transition share the same lock as wake.
    // A waiter can have been preempted (Ready) before it reaches this point.
    const bool pending = thread->accessWakePending();
    thread->accessWakePending() = false;
    if (!pending) thread->setState(ThreadState::Blocked);
    queue->getLock().unlock();
    if (!pending) schedule();
    interrupt::restore(flags);
}

void wakeThread(Thread* thread) {
    if (thread == nullptr) return;
    CpuLocalData* target = smp::getCpuLocalData(thread->getCpuId());
    if (target == nullptr || target->getRunQueue() == nullptr) return;
    RunQueue* queue = target->getRunQueue();
    const uint64_t flags = interrupt::disable();
    queue->getLock().lock();
    const ThreadState state = thread->getState();
    bool runnable = false;
    if (state == ThreadState::Running || state == ThreadState::Ready) {
        thread->accessWakePending() = true;
    } else if (state == ThreadState::Stopped) {
        thread->setStateBeforeStop(ThreadState::Ready);
        thread->accessWakePending() = true;
    } else if (state == ThreadState::Blocked || state == ThreadState::Sleeping) {
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
    interrupt::restore(flags);
    if (runnable && thread->getCpuId() != cpu::getCurrentId())
        smp::requestRemoteReschedule(thread->getCpuId());
}

void requestReschedule() { cpu::getCurrent()->setRescheduleRequested(true); }

void schedulerTick() {
    CpuLocalData* cpu = cpu::getCurrent();
    if (!schedule::isSchedulerInitialized() || cpu->getRunQueue() == nullptr) {
        return;
    }
    RunQueue* queue = cpu->getRunQueue();
    queue->getLock().lock();
    const uint64_t now     = time::getMonotonicTime();
    const uint64_t elapsed = now - queue->getClock();
    queue->setClock(now);
    cpu->addTickCount(1);
    Thread* current = cpu->getCurrentThread();
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
            cpu->setRescheduleRequested(true);
        }
    }
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

void sleepCurrentThread(uint64_t milliseconds) {
    if (milliseconds == 0) {
        schedule::yieldThread();
        return;
    }
    const uint64_t flags = interrupt::disable();
    RunQueue* queue      = cpu::getCurrent()->getRunQueue();
    Thread* current      = Thread::getCurrent();
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
    schedule::schedule();
    interrupt::restore(flags);
}

uint32_t getSchedulerWeight(int32_t nice) {
    if (nice < -20) {
        nice = -20;
    } else if (nice > 19) {
        nice = 19;
    }
    return niceWeights[nice + 20];
}

}  // namespace kernel::schedule

namespace kernel {
bool Thread::migrate(uint32_t cpuId) {
    Thread* thread        = this;
    CpuDescriptor* target = smp::getCpuDescriptor(cpuId);
    if (cpuId >= 64 || target == nullptr ||
        target->getState().load(MemoryOrder::Acquire) != CpuState::Online ||
        (thread->getCpuAffinity() & (1ULL << cpuId)) == 0 ||
        thread->getState() != ThreadState::Ready) {
        return false;
    }
    schedule::dequeueThread(thread);
    thread->setCpuId(cpuId);
    // Admission is the success condition. The destination CPU can have already
    // selected or blocked the thread by the time enqueueThread returns.
    return schedule::enqueueThread(thread);
}
}  // namespace kernel

namespace kernel {
bool Thread::setAffinity(uint64_t cpuMask) {
    Thread* thread = this;
    if (cpuMask == 0) {
        return false;
    }
    thread->setCpuAffinity(cpuMask);
    if ((cpuMask & (1ULL << thread->getCpuId())) != 0) {
        return true;
    }
    for (uint32_t cpuId = 0; cpuId < smp::getDiscoveredCpuCount() && cpuId < 64;
         ++cpuId) {
        if ((cpuMask & (1ULL << cpuId)) != 0 && thread->migrate(cpuId)) {
            return true;
        }
    }
    return false;
}
}  // namespace kernel

void kernel::RunQueue::rotateLeft(kernel::Thread* node) {
    RunQueue* queue       = this;
    kernel::Thread* right = node->getRbRight();
    node->setRbRight(right->getRbLeft());
    if (right->getRbLeft() != nullptr) {
        right->getRbLeft()->setRbParent(node);
    }
    right->setRbParent(node->getRbParent());
    if (node->getRbParent() == nullptr) {
        queue->setRoot(right);
    } else if (node == node->getRbParent()->getRbLeft()) {
        node->getRbParent()->setRbLeft(right);
    } else {
        node->getRbParent()->setRbRight(right);
    }
    right->setRbLeft(node);
    node->setRbParent(right);
}

void kernel::RunQueue::rotateRight(kernel::Thread* node) {
    RunQueue* queue      = this;
    kernel::Thread* left = node->getRbLeft();
    node->setRbLeft(left->getRbRight());
    if (left->getRbRight() != nullptr) {
        left->getRbRight()->setRbParent(node);
    }
    left->setRbParent(node->getRbParent());
    if (node->getRbParent() == nullptr) {
        queue->setRoot(left);
    } else if (node == node->getRbParent()->getRbRight()) {
        node->getRbParent()->setRbRight(left);
    } else {
        node->getRbParent()->setRbLeft(left);
    }
    left->setRbRight(node);
    node->setRbParent(left);
}

void kernel::RunQueue::insertFixup(kernel::Thread* node) {
    RunQueue* queue = this;
    while (node->getRbParent() != nullptr && node->getRbParent()->getRbRed()) {
        kernel::Thread* parent      = node->getRbParent();
        kernel::Thread* grandparent = parent->getRbParent();
        if (parent == grandparent->getRbLeft()) {
            kernel::Thread* uncle = grandparent->getRbRight();
            if (RunQueue::isRed(uncle)) {
                parent->setRbRed(false);
                uncle->setRbRed(false);
                grandparent->setRbRed(true);
                node = grandparent;
            } else {
                if (node == parent->getRbRight()) {
                    node = parent;
                    queue->rotateLeft(node);
                    parent      = node->getRbParent();
                    grandparent = parent->getRbParent();
                }
                parent->setRbRed(false);
                grandparent->setRbRed(true);
                queue->rotateRight(grandparent);
            }
        } else {
            kernel::Thread* uncle = grandparent->getRbLeft();
            if (RunQueue::isRed(uncle)) {
                parent->setRbRed(false);
                uncle->setRbRed(false);
                grandparent->setRbRed(true);
                node = grandparent;
            } else {
                if (node == parent->getRbLeft()) {
                    node = parent;
                    queue->rotateRight(node);
                    parent      = node->getRbParent();
                    grandparent = parent->getRbParent();
                }
                parent->setRbRed(false);
                grandparent->setRbRed(true);
                queue->rotateLeft(grandparent);
            }
        }
    }
    queue->getRoot()->setRbRed(false);
}

void kernel::RunQueue::insertNode(kernel::Thread* thread) {
    RunQueue* queue        = this;
    kernel::Thread* parent = nullptr;
    kernel::Thread** link  = &queue->accessRoot();
    while (*link != nullptr) {
        parent = *link;
        link   = RunQueue::comesBefore(thread, parent) ? &parent->accessRbLeft()
                                                       : &parent->accessRbRight();
    }
    thread->setRbParent(parent);
    thread->setRbLeft(nullptr);
    thread->setRbRight(nullptr);
    thread->setRbRed(true);
    *link = thread;
    queue->insertFixup(thread);
    thread->setInRunQueue(true);
    queue->setRunnableCount(queue->getRunnableCount() + 1);
    queue->setLeftmost(RunQueue::minimum(queue->getRoot()));
    queue->setMinVruntime(queue->getLeftmost() == nullptr
                              ? queue->getMinVruntime()
                              : queue->getLeftmost()->getVruntime());
}

void kernel::RunQueue::transplant(kernel::Thread* target,
                                  kernel::Thread* replacement) {
    RunQueue* queue = this;
    if (target->getRbParent() == nullptr) {
        queue->setRoot(replacement);
    } else if (target == target->getRbParent()->getRbLeft()) {
        target->getRbParent()->setRbLeft(replacement);
    } else {
        target->getRbParent()->setRbRight(replacement);
    }
    if (replacement != nullptr) {
        replacement->setRbParent(target->getRbParent());
    }
}

void kernel::RunQueue::deleteFixup(kernel::Thread* node,
                                   kernel::Thread* parent) {
    RunQueue* queue = this;
    while (node != queue->getRoot() && !RunQueue::isRed(node)) {
        if (parent == nullptr) {
            break;
        }
        if (node == parent->getRbLeft()) {
            kernel::Thread* sibling = parent->getRbRight();
            if (RunQueue::isRed(sibling)) {
                sibling->setRbRed(false);
                parent->setRbRed(true);
                queue->rotateLeft(parent);
                sibling = parent->getRbRight();
            }
            if (sibling == nullptr) {
                node   = parent;
                parent = node->getRbParent();
                continue;
            }
            if (!RunQueue::isRed(sibling->getRbLeft()) &&
                !RunQueue::isRed(sibling->getRbRight())) {
                sibling->setRbRed(true);
                node   = parent;
                parent = node->getRbParent();
            } else {
                if (!RunQueue::isRed(sibling->getRbRight())) {
                    if (sibling->getRbLeft() != nullptr) {
                        sibling->getRbLeft()->setRbRed(false);
                    }
                    sibling->setRbRed(true);
                    queue->rotateRight(sibling);
                    sibling = parent->getRbRight();
                }
                sibling->setRbRed(parent->getRbRed());
                parent->setRbRed(false);
                if (sibling->getRbRight() != nullptr) {
                    sibling->getRbRight()->setRbRed(false);
                }
                queue->rotateLeft(parent);
                node   = queue->getRoot();
                parent = nullptr;
            }
        } else {
            kernel::Thread* sibling = parent->getRbLeft();
            if (RunQueue::isRed(sibling)) {
                sibling->setRbRed(false);
                parent->setRbRed(true);
                queue->rotateRight(parent);
                sibling = parent->getRbLeft();
            }
            if (sibling == nullptr) {
                node   = parent;
                parent = node->getRbParent();
                continue;
            }
            if (!RunQueue::isRed(sibling->getRbRight()) &&
                !RunQueue::isRed(sibling->getRbLeft())) {
                sibling->setRbRed(true);
                node   = parent;
                parent = node->getRbParent();
            } else {
                if (!RunQueue::isRed(sibling->getRbLeft())) {
                    if (sibling->getRbRight() != nullptr) {
                        sibling->getRbRight()->setRbRed(false);
                    }
                    sibling->setRbRed(true);
                    queue->rotateLeft(sibling);
                    sibling = parent->getRbLeft();
                }
                sibling->setRbRed(parent->getRbRed());
                parent->setRbRed(false);
                if (sibling->getRbLeft() != nullptr) {
                    sibling->getRbLeft()->setRbRed(false);
                }
                queue->rotateRight(parent);
                node   = queue->getRoot();
                parent = nullptr;
            }
        }
    }
    if (node != nullptr) {
        node->setRbRed(false);
    }
}

void kernel::RunQueue::removeNode(kernel::Thread* thread) {
    RunQueue* queue = this;
    if (!thread->getInRunQueue()) {
        return;
    }
    kernel::Thread* moved = thread;
    bool movedWasRed      = moved->getRbRed();
    kernel::Thread* child;
    kernel::Thread* childParent;
    if (thread->getRbLeft() == nullptr) {
        child       = thread->getRbRight();
        childParent = thread->getRbParent();
        queue->transplant(thread, thread->getRbRight());
    } else if (thread->getRbRight() == nullptr) {
        child       = thread->getRbLeft();
        childParent = thread->getRbParent();
        queue->transplant(thread, thread->getRbLeft());
    } else {
        moved       = RunQueue::minimum(thread->getRbRight());
        movedWasRed = moved->getRbRed();
        child       = moved->getRbRight();
        if (moved->getRbParent() == thread) {
            childParent = moved;
            if (child != nullptr) {
                child->setRbParent(moved);
            }
        } else {
            childParent = moved->getRbParent();
            queue->transplant(moved, moved->getRbRight());
            moved->setRbRight(thread->getRbRight());
            moved->getRbRight()->setRbParent(moved);
        }
        queue->transplant(thread, moved);
        moved->setRbLeft(thread->getRbLeft());
        moved->getRbLeft()->setRbParent(moved);
        moved->setRbRed(thread->getRbRed());
    }
    if (!movedWasRed) {
        queue->deleteFixup(child, childParent);
    }
    thread->setRbParent(nullptr);
    thread->setRbLeft(nullptr);
    thread->setRbRight(nullptr);
    thread->setInRunQueue(false);
    queue->setRunnableCount(queue->getRunnableCount() - 1);
    queue->setLeftmost(RunQueue::minimum(queue->getRoot()));
    if (queue->getLeftmost() != nullptr) {
        queue->setMinVruntime(queue->getLeftmost()->getVruntime());
    }
}

bool kernel::RunQueue::isRed(const kernel::Thread* node) {
    return node != nullptr && node->getRbRed();
}
bool kernel::RunQueue::comesBefore(const kernel::Thread* left,
                                   const kernel::Thread* right) {
    return left->getVruntime() < right->getVruntime() ||
           (left->getVruntime() == right->getVruntime() &&
            left->getId() < right->getId());
}
kernel::Thread* kernel::RunQueue::minimum(kernel::Thread* node) {
    while (node != nullptr && node->getRbLeft() != nullptr) {
        node = node->getRbLeft();
    }
    return node;
}
void kernel::RunQueue::clearRunQueue(kernel::RunQueue* queue) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(queue);
    for (uint64_t index = 0; index < sizeof(kernel::RunQueue); ++index) {
        bytes[index] = 0;
    }
}
