/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sync/sync.cc
 * Sleeping synchronization, wait registration, and thread-owned cleanup.
 */

#include "kernel/sync.h"

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/task.h"

namespace kernel::task {

namespace {

// Caller holds the condition lock. Prepare the wake handshake and record the
// list owner before linking, allowing forced teardown to detach a waiter
// without knowing which synchronization object originally blocked it.
void appendWaiter(SpinLock& lock, Thread*& head, Thread*& tail,
                  Thread* thread) {
    prepareCurrentWait();
    thread->registerWait(lock, head, &tail);
    thread->setWaitNext(nullptr);
    if (tail == nullptr) {
        head = thread;
    } else {
        tail->setWaitNext(thread);
    }
    tail = thread;
}

// Detach the FIFO head and clear cancellation registration while still
// serialized with the queue owner. The caller wakes it after dropping the
// condition lock, avoiding nested scheduler work under that lock.
Thread* removeWaiter(Thread*& head, Thread*& tail) {
    Thread* thread = head;
    if (thread == nullptr) {
        return nullptr;
    }
    head = thread->getWaitNext();
    if (head == nullptr) {
        tail = nullptr;
    }
    thread->setWaitNext(nullptr);
    thread->clearWaitRegistration();
    return thread;
}

// Record completion of the startup initializer through its supplied argument.
void markOnce(void* argument) {
    *static_cast<uint32_t*>(argument) = 0x6f6e6365U;
}

}  // namespace

// Initialize an unpublished queue; no waiters may still reference it.
void WaitQueue::init() {
    lock_.init();
    head_ = nullptr;
    tail_ = nullptr;
}

// Enqueue current and block after releasing the queue lock. This queue does
// not store a business predicate or release an external condition lock;
// callers must arrange their own condition protocol rather than assume an
// atomic condition-variable wait.
void WaitQueue::wait() {
    Thread* current = currentThread();
    if (current == nullptr) {
        return;
    }
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    appendWaiter(lock_, head_, tail_, current);
    lock_.unlock();
    blockWaitingThread();
    cpu::restoreInterrupts(flags);
}

// Remove under the queue lock and wake outside it. Return whether a waiter was
// found; waking does not mean the thread has executed or consumed work.
bool WaitQueue::wakeOne() {
    Thread* thread;
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    thread = removeWaiter(head_, tail_);
    lock_.unlock();
    if (thread != nullptr) {
        wakeThread(thread);
    }
    cpu::restoreInterrupts(flags);
    return thread != nullptr;
}

// Drain current waiters through wakeOne, returning the number removed.
uint32_t WaitQueue::wakeAll() {
    uint32_t count = 0;
    while (wakeOne()) {
        ++count;
    }
    return count;
}

// Clear ownership and wait links before publishing this nonrecursive mutex.
void Mutex::init() {
    lock_.init();
    owner_       = nullptr;
    waiters_     = nullptr;
    waitersTail_ = nullptr;
}

// Acquire ownership or enqueue under the internal spinlock, then sleep with
// that spinlock released. Wakeup is an invitation to compete again, not an
// ownership handoff, so loop and recheck owner after every wake.
void Mutex::lock() {
    Thread* current = currentThread();
    if (current == nullptr) {
        return;
    }
    while (true) {
        const uint64_t flags = cpu::disableInterrupts();
        lock_.lock();
        if (owner_ == nullptr) {
            owner_ = current;
            lock_.unlock();
            cpu::restoreInterrupts(flags);
            return;
        }
        appendWaiter(lock_, waiters_, waitersTail_, current);
        lock_.unlock();
        blockWaitingThread();
        cpu::restoreInterrupts(flags);
    }
}

// Take ownership only if free and a current thread exists; never sleep.
bool Mutex::tryLock() {
    Thread* current      = currentThread();
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    const bool acquired = owner_ == nullptr && current != nullptr;
    if (acquired) {
        owner_ = current;
    }
    lock_.unlock();
    cpu::restoreInterrupts(flags);
    return acquired;
}

// Only the owning thread releases. Clear owner before waking one contender;
// that contender still rechecks ownership because another CPU may win first.
void Mutex::unlock() {
    Thread* waiter       = nullptr;
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    if (owner_ == currentThread()) {
        owner_ = nullptr;
        waiter = removeWaiter(waiters_, waitersTail_);
    }
    lock_.unlock();
    if (waiter != nullptr) {
        wakeThread(waiter);
    }
    cpu::restoreInterrupts(flags);
}

// Initialize the available permit count and empty wait list before use.
void Semaphore::init(uint32_t count) {
    lock_.init();
    count_       = count;
    waiters_     = nullptr;
    waitersTail_ = nullptr;
}

// Consume an available permit, or enqueue for direct delivery by signal. The
// blocking path does not decrement count after wake because signal hands that
// waiter a permit instead of incrementing the shared count.
void Semaphore::wait() {
    Thread* current = currentThread();
    if (current == nullptr) {
        return;
    }
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    if (count_ != 0) {
        --count_;
        lock_.unlock();
        cpu::restoreInterrupts(flags);
        return;
    }
    appendWaiter(lock_, waiters_, waitersTail_, current);
    lock_.unlock();
    blockWaitingThread();
    cpu::restoreInterrupts(flags);
}

// Consume one available permit without registering or blocking.
bool Semaphore::tryWait() {
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    const bool acquired = count_ != 0;
    if (acquired) {
        --count_;
    }
    lock_.unlock();
    cpu::restoreInterrupts(flags);
    return acquired;
}

// Hand a permit to the oldest waiter when present; otherwise accumulate it in
// count_. Doing both would let the same permit be consumed twice.
void Semaphore::signal() {
    Thread* waiter       = nullptr;
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    waiter = removeWaiter(waiters_, waitersTail_);
    if (waiter == nullptr) {
        ++count_;
    }
    lock_.unlock();
    if (waiter != nullptr) {
        wakeThread(waiter);
    }
    cpu::restoreInterrupts(flags);
}

// Set state 0 (not started); 1 means executing and 2 means published complete.
void Once::init() {
    lock_.init();
    state_       = 0;
    waiters_     = nullptr;
    waitersTail_ = nullptr;
}

// Elect the initializer under lock, execute it outside the lock, then publish
// completion and release waiters. argument is passed unchanged. This has no
// recovery protocol for a killed initializer and is not recursively callable
// on the same Once while initialization is still running.
void Once::run(Initializer initializer, void* argument) {
    if (initializer == nullptr) {
        return;
    }
    Thread* current      = currentThread();
    const uint64_t flags = cpu::disableInterrupts();
    lock_.lock();
    if (state_ == 2) {
        lock_.unlock();
        cpu::restoreInterrupts(flags);
        return;
    }
    if (state_ == 0) {
        state_ = 1;
        lock_.unlock();
        cpu::restoreInterrupts(flags);
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
            wakeThread(waiter);
        }
        return;
    }
    if (current != nullptr) {
        appendWaiter(lock_, waiters_, waitersTail_, current);
    }
    lock_.unlock();
    if (current != nullptr) {
        blockWaitingThread();
    }
    cpu::restoreInterrupts(flags);
}

// Check basic boot-time synchronization operations before user work depends on
// them. These checks exercise uncontended primitives, not every blocking or
// cancellation interleaving.
void runSynchronizationSelfTest() {
    SpinLock spin;
    spin.init();
    if (!spin.tryLock()) {
        device::serialWrite("[SYNC] spinlock self-test failed\n");
        cpu::halt();
    }
    spin.unlock();

    Mutex mutex;
    mutex.init();
    if (!mutex.tryLock()) {
        device::serialWrite("[SYNC] mutex self-test failed\n");
        cpu::halt();
    }
    mutex.unlock();

    Semaphore semaphore;
    semaphore.init(1);
    if (!semaphore.tryWait() || semaphore.tryWait()) {
        device::serialWrite("[SYNC] semaphore self-test failed\n");
        cpu::halt();
    }
    semaphore.signal();

    Once once;
    uint32_t marker = 0;
    once.init();
    once.run(markOnce, &marker);
    once.run(markOnce, &marker);
    if (marker != 0x6f6e6365U) {
        device::serialWrite("[SYNC] once self-test failed\n");
        cpu::halt();
    }
    device::serialWrite("[SYNC] SMP synchronization primitives initialized\n");
}

// Release the registered resource on ordinary C++ scope exit.
ThreadCleanup::~ThreadCleanup() { reset(); }
// Register object/release on the current thread with local interrupts
// excluded, before the resource can become stranded by thread termination. The
// node itself may live on the thread stack; detach before that storage or its
// registered object is transferred elsewhere.
void ThreadCleanup::attach(void* object, void (*release)(void*)) {
    const uint64_t flags = cpu::disableInterrupts();
    owner_               = currentThread();
    object_              = object;
    release_             = release;
    if (owner_) {
        next_ = owner_->cleanupList();
        if (next_) next_->previous_ = this;
        owner_->cleanupList() = this;
    }
    cpu::restoreInterrupts(flags);
}
// Unlink without calling release: use when moving responsibility to another
// cleanup node or resource owner. Clear all links to make later reset inert.
void ThreadCleanup::detach() {
    const uint64_t flags = cpu::disableInterrupts();
    if (owner_) {
        if (previous_)
            previous_->next_ = next_;
        else
            owner_->cleanupList() = next_;
        if (next_) next_->previous_ = previous_;
    }
    owner_    = nullptr;
    previous_ = next_ = nullptr;
    object_           = nullptr;
    release_          = nullptr;
    cpu::restoreInterrupts(flags);
}
// Capture payload, unlink first, then call release. A nested cleanup during
// release must not encounter this same node and free the object twice.
void ThreadCleanup::reset() {
    const uint64_t flags = cpu::disableInterrupts();
    auto* object         = object_;
    auto release         = release_;
    detach();
    if (object && release) release(object);
    cpu::restoreInterrupts(flags);
}
// Forced teardown runs the same callbacks even if C++ stack scopes never
// return. The target must no longer mutate its cleanup list concurrently.
void ThreadCleanup::releaseAll(Thread& thread) {
    while (thread.cleanupList()) thread.cleanupList()->reset();
}
// Record pointers to the owner's lock/list for cancellation; the caller still
// must link this thread under that lock. One thread has one generic waitNext,
// so it cannot be simultaneously linked in unrelated generic wait lists.
void Thread::registerWait(SpinLock& lock, Thread*& head, Thread** tail) {
    waitOwnerLock = &lock;
    waitOwnerHead = &head;
    waitOwnerTail = tail;
}

// Clear remembered queue ownership after normal unlink, before queue or thread
// storage can be released.
void Thread::clearWaitRegistration() {
    waitOwnerLock = nullptr;
    waitOwnerHead = nullptr;
    waitOwnerTail = nullptr;
}

// After target quiescence, lock the recorded queue owner and unlink this
// thread, repairing the optional tail. The owner lock serializes normal
// wakeup/removal; setting Terminated alone is insufficient to use this path
// while another CPU still executes on this thread's stack.
void Thread::cancelWait() {
    // The caller has first stopped this thread and observed a completed CPU
    // context switch. Queue owners serialize normal removal with this path.
    SpinLock* lock = waitOwnerLock;
    if (!lock) return;
    InterruptSpinLockGuard guard(*lock);
    if (!waitOwnerHead) return;
    Thread** link    = waitOwnerHead;
    Thread* previous = nullptr;
    while (*link && *link != this) {
        previous = *link;
        link     = &(*link)->accessWaitNext();
    }
    if (*link) {
        *link = waitNext;
        if (waitOwnerTail && *waitOwnerTail == this) *waitOwnerTail = previous;
    }
    waitNext = nullptr;
    clearWaitRegistration();
}

}  // namespace kernel::task
