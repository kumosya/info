#include "kernel/sync.h"

#include "kernel/cpu.h"
#include "kernel/serial.h"
#include "kernel/thread.h"

namespace {

void appendWaiter(kernel::Thread*& head, kernel::Thread*& tail,
                  kernel::Thread* thread) {
    kernel::Thread::prepareWait();
    thread->setWaitNext(nullptr);
    if (tail == nullptr) {
        head = thread;
    } else {
        tail->setWaitNext(thread);
    }
    tail = thread;
}

kernel::Thread* removeWaiter(kernel::Thread*& head, kernel::Thread*& tail) {
    kernel::Thread* thread = head;
    if (thread == nullptr) {
        return nullptr;
    }
    head = thread->getWaitNext();
    if (head == nullptr) {
        tail = nullptr;
    }
    thread->setWaitNext(nullptr);
    return thread;
}

void markOnce(void* argument) {
    *static_cast<uint32_t*>(argument) = 0x6f6e6365U;
}

}  // namespace

namespace kernel {

void WaitQueue::init() {
    lock_.init();
    head_ = nullptr;
    tail_ = nullptr;
}

void WaitQueue::wait() {
    Thread* current = Thread::getCurrent();
    if (current == nullptr) {
        return;
    }
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    appendWaiter(head_, tail_, current);
    lock_.unlock();
    Thread::blockCurrent();
    interrupt::restore(flags);
}

bool WaitQueue::wakeOne() {
    Thread* thread;
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    thread = removeWaiter(head_, tail_);
    lock_.unlock();
    if (thread != nullptr) {
        Thread::wake(thread);
    }
    interrupt::restore(flags);
    return thread != nullptr;
}

uint32_t WaitQueue::wakeAll() {
    uint32_t count = 0;
    while (wakeOne()) {
        ++count;
    }
    return count;
}

void Mutex::init() {
    lock_.init();
    owner_       = nullptr;
    waiters_     = nullptr;
    waitersTail_ = nullptr;
}

void Mutex::lock() {
    Thread* current = Thread::getCurrent();
    if (current == nullptr) {
        return;
    }
    while (true) {
        const uint64_t flags = interrupt::disable();
        lock_.lock();
        if (owner_ == nullptr) {
            owner_ = current;
            lock_.unlock();
            interrupt::restore(flags);
            return;
        }
        appendWaiter(waiters_, waitersTail_, current);
        lock_.unlock();
        Thread::blockCurrent();
        interrupt::restore(flags);
    }
}

bool Mutex::tryLock() {
    Thread* current      = Thread::getCurrent();
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    const bool acquired = owner_ == nullptr && current != nullptr;
    if (acquired) {
        owner_ = current;
    }
    lock_.unlock();
    interrupt::restore(flags);
    return acquired;
}

void Mutex::unlock() {
    Thread* waiter       = nullptr;
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    if (owner_ == Thread::getCurrent()) {
        owner_ = nullptr;
        waiter = removeWaiter(waiters_, waitersTail_);
    }
    lock_.unlock();
    if (waiter != nullptr) {
        Thread::wake(waiter);
    }
    interrupt::restore(flags);
}

void Semaphore::init(uint32_t count) {
    lock_.init();
    count_       = count;
    waiters_     = nullptr;
    waitersTail_ = nullptr;
}

void Semaphore::wait() {
    Thread* current = Thread::getCurrent();
    if (current == nullptr) {
        return;
    }
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    if (count_ != 0) {
        --count_;
        lock_.unlock();
        interrupt::restore(flags);
        return;
    }
    appendWaiter(waiters_, waitersTail_, current);
    lock_.unlock();
    Thread::blockCurrent();
    interrupt::restore(flags);
}

bool Semaphore::tryWait() {
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    const bool acquired = count_ != 0;
    if (acquired) {
        --count_;
    }
    lock_.unlock();
    interrupt::restore(flags);
    return acquired;
}

void Semaphore::signal() {
    Thread* waiter       = nullptr;
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    waiter = removeWaiter(waiters_, waitersTail_);
    if (waiter == nullptr) {
        ++count_;
    }
    lock_.unlock();
    if (waiter != nullptr) {
        Thread::wake(waiter);
    }
    interrupt::restore(flags);
}

void Once::init() {
    lock_.init();
    state_       = 0;
    waiters_     = nullptr;
    waitersTail_ = nullptr;
}

void Once::run(Initializer initializer, void* argument) {
    if (initializer == nullptr) {
        return;
    }
    Thread* current      = Thread::getCurrent();
    const uint64_t flags = interrupt::disable();
    lock_.lock();
    if (state_ == 2) {
        lock_.unlock();
        interrupt::restore(flags);
        return;
    }
    if (state_ == 0) {
        state_ = 1;
        lock_.unlock();
        interrupt::restore(flags);
        initializer(argument);

        lock_.lock();
        state_          = 2;
        Thread* waiters = waiters_;
        waiters_        = nullptr;
        waitersTail_    = nullptr;
        lock_.unlock();
        while (waiters != nullptr) {
            Thread* waiter = waiters;
            waiters        = waiters->getWaitNext();
            waiter->setWaitNext(nullptr);
            Thread::wake(waiter);
        }
        return;
    }
    if (current != nullptr) {
        appendWaiter(waiters_, waitersTail_, current);
    }
    lock_.unlock();
    if (current != nullptr) {
        Thread::blockCurrent();
    }
    interrupt::restore(flags);
}

void sync::runSynchronizationSelfTest() {
    SpinLock spin;
    spin.init();
    if (!spin.tryLock()) {
        serial::serialWrite("[SYNC] spinlock self-test failed\n");
        cpu::halt();
    }
    spin.unlock();

    Mutex mutex;
    mutex.init();
    if (!mutex.tryLock()) {
        serial::serialWrite("[SYNC] mutex self-test failed\n");
        cpu::halt();
    }
    mutex.unlock();

    Semaphore semaphore;
    semaphore.init(1);
    if (!semaphore.tryWait() || semaphore.tryWait()) {
        serial::serialWrite("[SYNC] semaphore self-test failed\n");
        cpu::halt();
    }
    semaphore.signal();

    Once once;
    uint32_t marker = 0;
    once.init();
    once.run(markOnce, &marker);
    once.run(markOnce, &marker);
    if (marker != 0x6f6e6365U) {
        serial::serialWrite("[SYNC] once self-test failed\n");
        cpu::halt();
    }
    serial::serialWrite("[SYNC] SMP synchronization primitives initialized\n");
}

}  // namespace kernel
