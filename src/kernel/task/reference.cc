#include "kernel/thread.h"
#include "kernel/cpu.h"
#include "kernel/spinlock.h"

namespace kernel {
ThreadCleanup::~ThreadCleanup() { reset(); }
void ThreadCleanup::attach(void* object, void (*release)(void*)) {
    const uint64_t flags = interrupt::disable();
    owner_ = Thread::getCurrent();
    object_ = object;
    release_ = release;
    if (owner_) {
        next_ = owner_->cleanupList();
        if (next_) next_->previous_ = this;
        owner_->cleanupList() = this;
    }
    interrupt::restore(flags);
}
void ThreadCleanup::detach() {
    const uint64_t flags = interrupt::disable();
    if (owner_) {
        if (previous_) previous_->next_ = next_;
        else owner_->cleanupList() = next_;
        if (next_) next_->previous_ = previous_;
    }
    owner_ = nullptr;
    previous_ = next_ = nullptr;
    object_ = nullptr;
    release_ = nullptr;
    interrupt::restore(flags);
}
void ThreadCleanup::reset() {
    const uint64_t flags = interrupt::disable();
    auto* object = object_;
    auto release = release_;
    detach();
    if (object && release) release(object);
    interrupt::restore(flags);
}
void ThreadCleanup::releaseAll(Thread& thread) {
    while (thread.cleanupList()) thread.cleanupList()->reset();
}
void Thread::registerWait(SpinLock& lock, Thread*& head, Thread** tail) {
    waitOwnerLock = &lock;
    waitOwnerHead = &head;
    waitOwnerTail = tail;
}

void Thread::clearWaitRegistration() {
    waitOwnerLock = nullptr;
    waitOwnerHead = nullptr;
    waitOwnerTail = nullptr;
}

void Thread::cancelWait() {
    // The caller has first stopped this thread and observed a completed CPU
    // context switch. Queue owners serialize normal removal with this path.
    SpinLock* lock = waitOwnerLock;
    if (!lock) return;
    InterruptSpinLockGuard guard(*lock);
    if (!waitOwnerHead) return;
    Thread** link = waitOwnerHead;
    Thread* previous = nullptr;
    while (*link && *link != this) {
        previous = *link;
        link = &(*link)->accessWaitNext();
    }
    if (*link) {
        *link = waitNext;
        if (waitOwnerTail && *waitOwnerTail == this) *waitOwnerTail = previous;
    }
    waitNext = nullptr;
    clearWaitRegistration();
}

}  // namespace kernel
