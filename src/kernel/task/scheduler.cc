#include "kernel/scheduler.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/interrupt.h"
#include "kernel/page.h"
#include "kernel/process.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/time.h"

namespace {

kernel::Atomic<bool> schedulerInitialized{false};
constexpr uint32_t niceWeights[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
    1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
    110,   87,    70,    56,    45,    36,    29,    23,    18,    15};

constexpr uint64_t calculateVruntimeDelta(uint64_t runtime,
                                          uint32_t weight) {
    return (runtime * 1024ULL + weight - 1) / weight;
}

static_assert(calculateVruntimeDelta(1000000, niceWeights[0]) <
              calculateVruntimeDelta(1000000, niceWeights[20]));
static_assert(calculateVruntimeDelta(1000000, niceWeights[20]) <
              calculateVruntimeDelta(1000000, niceWeights[39]));

bool isRed(const kernel::Thread* node) {
    return node != nullptr && node->rbRed;
}

bool comesBefore(const kernel::Thread* left, const kernel::Thread* right) {
    return left->vruntime < right->vruntime ||
           (left->vruntime == right->vruntime && left->id < right->id);
}

kernel::Thread* minimum(kernel::Thread* node) {
    while (node != nullptr && node->rbLeft != nullptr) {
        node = node->rbLeft;
    }
    return node;
}

void rotateLeft(kernel::RunQueue* queue, kernel::Thread* node) {
    kernel::Thread* right = node->rbRight;
    node->rbRight         = right->rbLeft;
    if (right->rbLeft != nullptr) {
        right->rbLeft->rbParent = node;
    }
    right->rbParent = node->rbParent;
    if (node->rbParent == nullptr) {
        queue->root = right;
    } else if (node == node->rbParent->rbLeft) {
        node->rbParent->rbLeft = right;
    } else {
        node->rbParent->rbRight = right;
    }
    right->rbLeft  = node;
    node->rbParent = right;
}

void rotateRight(kernel::RunQueue* queue, kernel::Thread* node) {
    kernel::Thread* left = node->rbLeft;
    node->rbLeft         = left->rbRight;
    if (left->rbRight != nullptr) {
        left->rbRight->rbParent = node;
    }
    left->rbParent = node->rbParent;
    if (node->rbParent == nullptr) {
        queue->root = left;
    } else if (node == node->rbParent->rbRight) {
        node->rbParent->rbRight = left;
    } else {
        node->rbParent->rbLeft = left;
    }
    left->rbRight  = node;
    node->rbParent = left;
}

void insertFixup(kernel::RunQueue* queue, kernel::Thread* node) {
    while (node->rbParent != nullptr && node->rbParent->rbRed) {
        kernel::Thread* parent      = node->rbParent;
        kernel::Thread* grandparent = parent->rbParent;
        if (parent == grandparent->rbLeft) {
            kernel::Thread* uncle = grandparent->rbRight;
            if (isRed(uncle)) {
                parent->rbRed      = false;
                uncle->rbRed       = false;
                grandparent->rbRed = true;
                node               = grandparent;
            } else {
                if (node == parent->rbRight) {
                    node = parent;
                    rotateLeft(queue, node);
                    parent      = node->rbParent;
                    grandparent = parent->rbParent;
                }
                parent->rbRed      = false;
                grandparent->rbRed = true;
                rotateRight(queue, grandparent);
            }
        } else {
            kernel::Thread* uncle = grandparent->rbLeft;
            if (isRed(uncle)) {
                parent->rbRed      = false;
                uncle->rbRed       = false;
                grandparent->rbRed = true;
                node               = grandparent;
            } else {
                if (node == parent->rbLeft) {
                    node = parent;
                    rotateRight(queue, node);
                    parent      = node->rbParent;
                    grandparent = parent->rbParent;
                }
                parent->rbRed      = false;
                grandparent->rbRed = true;
                rotateLeft(queue, grandparent);
            }
        }
    }
    queue->root->rbRed = false;
}

void insertNode(kernel::RunQueue* queue, kernel::Thread* thread) {
    kernel::Thread* parent = nullptr;
    kernel::Thread** link  = &queue->root;
    while (*link != nullptr) {
        parent = *link;
        link = comesBefore(thread, parent) ? &parent->rbLeft : &parent->rbRight;
    }
    thread->rbParent = parent;
    thread->rbLeft   = nullptr;
    thread->rbRight  = nullptr;
    thread->rbRed    = true;
    *link            = thread;
    insertFixup(queue, thread);
    thread->inRunQueue = true;
    ++queue->runnableCount;
    queue->leftmost    = minimum(queue->root);
    queue->minVruntime = queue->leftmost == nullptr ? queue->minVruntime
                                                    : queue->leftmost->vruntime;
}

void transplant(kernel::RunQueue* queue, kernel::Thread* target,
                kernel::Thread* replacement) {
    if (target->rbParent == nullptr) {
        queue->root = replacement;
    } else if (target == target->rbParent->rbLeft) {
        target->rbParent->rbLeft = replacement;
    } else {
        target->rbParent->rbRight = replacement;
    }
    if (replacement != nullptr) {
        replacement->rbParent = target->rbParent;
    }
}

void deleteFixup(kernel::RunQueue* queue, kernel::Thread* node,
                 kernel::Thread* parent) {
    while (node != queue->root && !isRed(node)) {
        if (parent == nullptr) {
            break;
        }
        if (node == parent->rbLeft) {
            kernel::Thread* sibling = parent->rbRight;
            if (isRed(sibling)) {
                sibling->rbRed = false;
                parent->rbRed  = true;
                rotateLeft(queue, parent);
                sibling = parent->rbRight;
            }
            if (sibling == nullptr) {
                node   = parent;
                parent = node->rbParent;
                continue;
            }
            if (!isRed(sibling->rbLeft) && !isRed(sibling->rbRight)) {
                sibling->rbRed = true;
                node           = parent;
                parent         = node->rbParent;
            } else {
                if (!isRed(sibling->rbRight)) {
                    if (sibling->rbLeft != nullptr) {
                        sibling->rbLeft->rbRed = false;
                    }
                    sibling->rbRed = true;
                    rotateRight(queue, sibling);
                    sibling = parent->rbRight;
                }
                sibling->rbRed = parent->rbRed;
                parent->rbRed  = false;
                if (sibling->rbRight != nullptr) {
                    sibling->rbRight->rbRed = false;
                }
                rotateLeft(queue, parent);
                node   = queue->root;
                parent = nullptr;
            }
        } else {
            kernel::Thread* sibling = parent->rbLeft;
            if (isRed(sibling)) {
                sibling->rbRed = false;
                parent->rbRed  = true;
                rotateRight(queue, parent);
                sibling = parent->rbLeft;
            }
            if (sibling == nullptr) {
                node   = parent;
                parent = node->rbParent;
                continue;
            }
            if (!isRed(sibling->rbRight) && !isRed(sibling->rbLeft)) {
                sibling->rbRed = true;
                node           = parent;
                parent         = node->rbParent;
            } else {
                if (!isRed(sibling->rbLeft)) {
                    if (sibling->rbRight != nullptr) {
                        sibling->rbRight->rbRed = false;
                    }
                    sibling->rbRed = true;
                    rotateLeft(queue, sibling);
                    sibling = parent->rbLeft;
                }
                sibling->rbRed = parent->rbRed;
                parent->rbRed  = false;
                if (sibling->rbLeft != nullptr) {
                    sibling->rbLeft->rbRed = false;
                }
                rotateRight(queue, parent);
                node   = queue->root;
                parent = nullptr;
            }
        }
    }
    if (node != nullptr) {
        node->rbRed = false;
    }
}

void removeNode(kernel::RunQueue* queue, kernel::Thread* thread) {
    if (!thread->inRunQueue) {
        return;
    }
    kernel::Thread* moved = thread;
    bool movedWasRed      = moved->rbRed;
    kernel::Thread* child;
    kernel::Thread* childParent;
    if (thread->rbLeft == nullptr) {
        child       = thread->rbRight;
        childParent = thread->rbParent;
        transplant(queue, thread, thread->rbRight);
    } else if (thread->rbRight == nullptr) {
        child       = thread->rbLeft;
        childParent = thread->rbParent;
        transplant(queue, thread, thread->rbLeft);
    } else {
        moved       = minimum(thread->rbRight);
        movedWasRed = moved->rbRed;
        child       = moved->rbRight;
        if (moved->rbParent == thread) {
            childParent = moved;
            if (child != nullptr) {
                child->rbParent = moved;
            }
        } else {
            childParent = moved->rbParent;
            transplant(queue, moved, moved->rbRight);
            moved->rbRight           = thread->rbRight;
            moved->rbRight->rbParent = moved;
        }
        transplant(queue, thread, moved);
        moved->rbLeft           = thread->rbLeft;
        moved->rbLeft->rbParent = moved;
        moved->rbRed            = thread->rbRed;
    }
    if (!movedWasRed) {
        deleteFixup(queue, child, childParent);
    }
    thread->rbParent   = nullptr;
    thread->rbLeft     = nullptr;
    thread->rbRight    = nullptr;
    thread->inRunQueue = false;
    --queue->runnableCount;
    queue->leftmost = minimum(queue->root);
    if (queue->leftmost != nullptr) {
        queue->minVruntime = queue->leftmost->vruntime;
    }
}

void idleEntry(void*) {
    while (true) {
        asm volatile("sti; hlt");
    }
}

void timerHandler(kernel::InterruptContext*) { kernel::schedulerTick(); }

void clearRunQueue(kernel::RunQueue* queue) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(queue);
    for (uint64_t index = 0; index < sizeof(kernel::RunQueue); ++index) {
        bytes[index] = 0;
    }
}

}  // namespace

namespace kernel {

void initScheduler() {
    CpuLocalData* cpu = getCurrentCpu();
    RunQueue* queue = static_cast<RunQueue*>(kernelAllocate(sizeof(RunQueue)));
    if (queue == nullptr) {
        serialWrite("[SCHED] unable to allocate run queue\n");
        haltCpu();
    }
    clearRunQueue(queue);
    queue->currentThread = cpu->currentThread;
    queue->minVruntime   = cpu->currentThread->vruntime;
    queue->clock         = getMonotonicTime();
    cpu->runQueue        = queue;
    schedulerInitialized.store(true, MemoryOrder::Release);

    Thread* idle = createThread(nullptr, idleEntry, nullptr);
    dequeueThread(idle);
    queue->idleThread = idle;
    cpu->idleThread   = idle;

    registerInterruptHandler(32, timerHandler);
    configureLocalApicTimer(32, 1000000);
    serialWrite("[SCHED] per-CPU CFS run queue initialized\n");
}

bool isSchedulerInitialized() {
    return schedulerInitialized.load(MemoryOrder::Acquire);
}

void enqueueThread(Thread* thread) {
    if (thread == nullptr || thread->inRunQueue ||
        thread->state == ThreadState::Terminated) {
        return;
    }
    uint32_t targetCpuId    = thread->cpuId;
    CpuLocalData* targetCpu = getCpuLocalData(targetCpuId);
    if (targetCpu == nullptr || targetCpu->runQueue == nullptr ||
        (thread->cpuAffinity & (1ULL << targetCpuId)) == 0) {
        targetCpu = nullptr;
        for (uint32_t cpuId = 0; cpuId < getDiscoveredCpuCount() && cpuId < 64;
             ++cpuId) {
            CpuDescriptor* descriptor = getCpuDescriptor(cpuId);
            if ((thread->cpuAffinity & (1ULL << cpuId)) != 0 &&
                descriptor != nullptr &&
                descriptor->state.load(MemoryOrder::Acquire) ==
                    CpuState::Online &&
                descriptor->localData != nullptr &&
                descriptor->localData->runQueue != nullptr) {
                targetCpu   = descriptor->localData;
                targetCpuId = cpuId;
                break;
            }
        }
    }
    if (targetCpu == nullptr) {
        return;
    }
    RunQueue* queue      = targetCpu->runQueue;
    const uint64_t flags = disableInterrupts();
    queue->lock.lock();
    if (thread->vruntime < queue->minVruntime) {
        thread->vruntime = queue->minVruntime;
    }
    thread->state = ThreadState::Ready;
    thread->cpuId = targetCpuId;
    insertNode(queue, thread);
    queue->lock.unlock();
    restoreInterrupts(flags);
    if (targetCpuId != getCurrentCpuId()) {
        requestRemoteReschedule(targetCpuId);
    }
}

void dequeueThread(Thread* thread) {
    if (thread == nullptr || !thread->inRunQueue) {
        return;
    }
    CpuLocalData* cpu = getCpuLocalData(thread->cpuId);
    if (cpu == nullptr || cpu->runQueue == nullptr) {
        return;
    }
    RunQueue* queue      = cpu->runQueue;
    const uint64_t flags = disableInterrupts();
    queue->lock.lock();
    removeNode(queue, thread);
    queue->lock.unlock();
    restoreInterrupts(flags);
}

Thread* pickNextThread() {
    RunQueue* queue      = getCurrentCpu()->runQueue;
    const uint64_t flags = disableInterrupts();
    queue->lock.lock();
    Thread* thread =
        queue->leftmost == nullptr ? queue->idleThread : queue->leftmost;
    queue->lock.unlock();
    restoreInterrupts(flags);
    return thread;
}

void schedule() {
    if (!isSchedulerInitialized()) {
        return;
    }
    const uint64_t flags = disableInterrupts();
    CpuLocalData* cpu    = getCurrentCpu();
    RunQueue* queue      = cpu->runQueue;
    queue->lock.lock();
    cpu->rescheduleRequested = false;
    Thread* previous         = cpu->currentThread;
    if (previous != nullptr && previous != queue->idleThread &&
        previous->state == ThreadState::Running) {
        previous->state = ThreadState::Ready;
        insertNode(queue, previous);
    }
    Thread* next = queue->leftmost;
    if (next != nullptr) {
        removeNode(queue, next);
    } else {
        next = queue->idleThread;
    }
    next->state          = ThreadState::Running;
    next->sliceRuntime   = 0;
    next->cpuId          = cpu->cpuId;
    cpu->currentThread   = next;
    cpu->currentProcess  = next->process;
    queue->currentThread = next;
    queue->lock.unlock();

    if (next != previous) {
        setKernelStack(next->kernelStackTop);
        switchAddressSpace(
            next->process == nullptr
                ? getKernelAddressSpaceRoot()
                : next->process->addressSpace().rootPhysicalAddress());
        switchThread(previous, next);
    }
    restoreInterrupts(flags);
}

void yieldThread() {
    requestReschedule();
    schedule();
}

void requestReschedule() { getCurrentCpu()->rescheduleRequested = true; }

void schedulerTick() {
    CpuLocalData* cpu = getCurrentCpu();
    if (!isSchedulerInitialized() || cpu->runQueue == nullptr) {
        return;
    }
    RunQueue* queue = cpu->runQueue;
    queue->lock.lock();
    const uint64_t now = getMonotonicTime();
    const uint64_t elapsed = now - queue->clock;
    queue->clock = now;
    ++cpu->tickCount;
    Thread* current = cpu->currentThread;
    if (current != nullptr && current != queue->idleThread &&
        current->state == ThreadState::Running) {
        current->runtime += elapsed;
        current->sliceRuntime += elapsed;
        const uint64_t delta =
            calculateVruntimeDelta(elapsed, current->weight);
        current->vruntime += delta;
        if (current->sliceRuntime >= current->timeSlice ||
            (queue->leftmost != nullptr &&
             queue->leftmost->vruntime < current->vruntime)) {
            current->sliceRuntime = 0;
            cpu->rescheduleRequested = true;
        }
    }
    while (queue->sleepingThreads != nullptr &&
           deadlineReached(now,
                           queue->sleepingThreads
                               ->wakeDeadlineNanoseconds)) {
        Thread* thread         = queue->sleepingThreads;
        queue->sleepingThreads = thread->waitNext;
        thread->waitNext       = nullptr;
        if (thread->state == ThreadState::Stopped) {
            thread->stateBeforeStop = ThreadState::Ready;
            continue;
        }
        if (thread->state == ThreadState::Terminated) {
            continue;
        }
        thread->state          = ThreadState::Ready;
        insertNode(queue, thread);
        cpu->rescheduleRequested = true;
    }
    queue->lock.unlock();
}

void sleepCurrentThread(uint64_t milliseconds) {
    if (milliseconds == 0) {
        yieldThread();
        return;
    }
    const uint64_t flags = disableInterrupts();
    RunQueue* queue      = getCurrentCpu()->runQueue;
    Thread* current      = getCurrentThread();
    queue->lock.lock();
    const uint64_t duration = millisecondsToNanoseconds(milliseconds);
    const uint64_t now = getMonotonicTime();
    current->state = ThreadState::Sleeping;
    current->wakeDeadlineNanoseconds =
        duration > ~0ULL - now ? ~0ULL : now + duration;
    Thread** link     = &queue->sleepingThreads;
    while (*link != nullptr &&
           !deadlineReached((*link)->wakeDeadlineNanoseconds,
                            current->wakeDeadlineNanoseconds)) {
        link = &(*link)->waitNext;
    }
    current->waitNext = *link;
    *link             = current;
    queue->lock.unlock();
    schedule();
    restoreInterrupts(flags);
}

bool migrateThread(Thread* thread, uint32_t cpuId) {
    CpuDescriptor* target = getCpuDescriptor(cpuId);
    if (thread == nullptr || cpuId >= 64 || target == nullptr ||
        target->state.load(MemoryOrder::Acquire) != CpuState::Online ||
        (thread->cpuAffinity & (1ULL << cpuId)) == 0 ||
        thread->state != ThreadState::Ready) {
        return false;
    }
    dequeueThread(thread);
    thread->cpuId = cpuId;
    enqueueThread(thread);
    return thread->inRunQueue && thread->cpuId == cpuId;
}

bool setThreadAffinity(Thread* thread, uint64_t cpuMask) {
    if (thread == nullptr || cpuMask == 0) {
        return false;
    }
    thread->cpuAffinity = cpuMask;
    if ((cpuMask & (1ULL << thread->cpuId)) != 0) {
        return true;
    }
    for (uint32_t cpuId = 0; cpuId < getDiscoveredCpuCount() && cpuId < 64;
         ++cpuId) {
        if ((cpuMask & (1ULL << cpuId)) != 0 && migrateThread(thread, cpuId)) {
            return true;
        }
    }
    return false;
}

uint32_t getSchedulerWeight(int32_t nice) {
    if (nice < -20) {
        nice = -20;
    } else if (nice > 19) {
        nice = 19;
    }
    return niceWeights[nice + 20];
}

}  // namespace kernel
