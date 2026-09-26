#include "kernel/sync.h"

#include "kernel/cpu.h"
#include "kernel/serial.h"
#include "kernel/thread.h"

namespace {

void appendWaiter(kernel::Thread*& head, kernel::Thread*& tail,
                  kernel::Thread* thread) {
    thread->waitNext = nullptr;
    if (tail == nullptr) {
        head = thread;
    } else {
        tail->waitNext = thread;
    }
    tail = thread;
}

kernel::Thread* removeWaiter(kernel::Thread*& head, kernel::Thread*& tail) {
    kernel::Thread* thread = head;
    if (thread == nullptr) {
        return nullptr;
    }
    head = thread->waitNext;
    if (head == nullptr) {
        tail = nullptr;
    }
    thread->waitNext = nullptr;
    return thread;
}

void markOnce(void* argument) {
    *static_cast<uint32_t*>(argument) = 0x6f6e6365U;
}

}  // namespace

namespace kernel {

void WaitQueue::initialize() {
    lock_.initialize();
    head_ = nullptr;
    tail_ = nullptr;
}

void WaitQueue::wait() {
    Thread* current = getCurrentThread();
    if (current == nullptr) {
        return;
    }
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    appendWaiter(head_, tail_, current);
    lock_.unlock();
    blockThread();
    restoreInterrupts(flags);
}

bool WaitQueue::wakeOne() {
    Thread* thread;
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    thread = removeWaiter(head_, tail_);
    lock_.unlock();
    if (thread != nullptr) {
        wakeThread(thread);
    }
    restoreInterrupts(flags);
    return thread != nullptr;
}

uint32_t WaitQueue::wakeAll() {
    uint32_t count = 0;
    while (wakeOne()) {
        ++count;
    }
    return count;
}

void Mutex::initialize() {
    lock_.initialize();
    owner_ = nullptr;
    waiters_ = nullptr;
    waitersTail_ = nullptr;
}

void Mutex::lock() {
    Thread* current = getCurrentThread();
    if (current == nullptr) {
        return;
    }
    while (true) {
        const uint64_t flags = disableInterrupts();
        lock_.lock();
        if (owner_ == nullptr) {
            owner_ = current;
            lock_.unlock();
            restoreInterrupts(flags);
            return;
        }
        appendWaiter(waiters_, waitersTail_, current);
        lock_.unlock();
        blockThread();
        restoreInterrupts(flags);
    }
}

bool Mutex::tryLock() {
    Thread* current = getCurrentThread();
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    const bool acquired = owner_ == nullptr && current != nullptr;
    if (acquired) {
        owner_ = current;
    }
    lock_.unlock();
    restoreInterrupts(flags);
    return acquired;
}

void Mutex::unlock() {
    Thread* waiter = nullptr;
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    if (owner_ == getCurrentThread()) {
        owner_ = nullptr;
        waiter = removeWaiter(waiters_, waitersTail_);
    }
    lock_.unlock();
    if (waiter != nullptr) {
        wakeThread(waiter);
    }
    restoreInterrupts(flags);
}

void Semaphore::initialize(uint32_t count) {
    lock_.initialize();
    count_ = count;
    waiters_ = nullptr;
    waitersTail_ = nullptr;
}

void Semaphore::wait() {
    Thread* current = getCurrentThread();
    if (current == nullptr) {
        return;
    }
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    if (count_ != 0) {
        --count_;
        lock_.unlock();
        restoreInterrupts(flags);
        return;
    }
    appendWaiter(waiters_, waitersTail_, current);
    lock_.unlock();
    blockThread();
    restoreInterrupts(flags);
}

bool Semaphore::tryWait() {
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    const bool acquired = count_ != 0;
    if (acquired) {
        --count_;
    }
    lock_.unlock();
    restoreInterrupts(flags);
    return acquired;
}

void Semaphore::signal() {
    Thread* waiter = nullptr;
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    waiter = removeWaiter(waiters_, waitersTail_);
    if (waiter == nullptr) {
        ++count_;
    }
    lock_.unlock();
    if (waiter != nullptr) {
        wakeThread(waiter);
    }
    restoreInterrupts(flags);
}

void Once::initialize() {
    lock_.initialize();
    state_ = 0;
    waiters_ = nullptr;
    waitersTail_ = nullptr;
}

void Once::run(Initializer initializer, void* argument) {
    if (initializer == nullptr) {
        return;
    }
    Thread* current = getCurrentThread();
    const uint64_t flags = disableInterrupts();
    lock_.lock();
    if (state_ == 2) {
        lock_.unlock();
        restoreInterrupts(flags);
        return;
    }
    if (state_ == 0) {
        state_ = 1;
        lock_.unlock();
        restoreInterrupts(flags);
        initializer(argument);

        lock_.lock();
        state_ = 2;
        Thread* waiters = waiters_;
        waiters_ = nullptr;
        waitersTail_ = nullptr;
        lock_.unlock();
        while (waiters != nullptr) {
            Thread* waiter = waiters;
            waiters = waiters->waitNext;
            waiter->waitNext = nullptr;
            wakeThread(waiter);
        }
        return;
    }
    if (current != nullptr) {
        appendWaiter(waiters_, waitersTail_, current);
    }
    lock_.unlock();
    if (current != nullptr) {
        blockThread();
    }
    restoreInterrupts(flags);
}

void runSynchronizationSelfTest() {
    SpinLock spin;
    spin.initialize();
    if (!spin.tryLock()) {
        serialWrite("[SYNC] spinlock self-test failed\n");
        haltCpu();
    }
    spin.unlock();

    Mutex mutex;
    mutex.initialize();
    if (!mutex.tryLock()) {
        serialWrite("[SYNC] mutex self-test failed\n");
        haltCpu();
    }
    mutex.unlock();

    Semaphore semaphore;
    semaphore.initialize(1);
    if (!semaphore.tryWait() || semaphore.tryWait()) {
        serialWrite("[SYNC] semaphore self-test failed\n");
        haltCpu();
    }
    semaphore.signal();

    Once once;
    uint32_t marker = 0;
    once.initialize();
    once.run(markOnce, &marker);
    once.run(markOnce, &marker);
    if (marker != 0x6f6e6365U) {
        serialWrite("[SYNC] once self-test failed\n");
        haltCpu();
    }
    serialWrite("[SYNC] SMP synchronization primitives initialized\n");
}

}  // namespace kernel
