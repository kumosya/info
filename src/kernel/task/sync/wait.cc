#include "kernel/cpu.h"
#include "kernel/page.h"
#include "kernel/spinlock.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace {

constexpr uint32_t bucketCount = 64;

struct WaitBucket {
    kernel::SpinLock lock;
    kernel::Thread* head = nullptr;
    kernel::Thread* tail = nullptr;
};

WaitBucket buckets[bucketCount];

uint32_t bucketIndex(uint64_t key) {
    return static_cast<uint32_t>(key % bucketCount);
}

uint64_t waitKey(kernel::Task& task, const volatile uint32_t* address) {
    const uint64_t virtualAddress = reinterpret_cast<uint64_t>(address);
    kernel::PreemptionSpinLockGuard guard(task.addressSpace().lock());
    uint64_t flags          = 0;
    const uint64_t physical = kernel::memory::getPhysicalAddressInAddressSpace(
        task.addressSpace().rootPhysicalAddress(), virtualAddress, &flags);
    return physical != 0 && ((flags & kernel::pageUser) != 0 ||
        (task.isSystemService() && virtualAddress >= 0xffff800000000000ULL)) ? physical : 0;
}

void append(WaitBucket& bucket, kernel::Thread* thread) {
    kernel::Thread::prepareWait();
    thread->registerWait(bucket.lock, bucket.head, &bucket.tail);
    thread->setWaitNext(nullptr);
    if (bucket.tail == nullptr) {
        bucket.head = thread;
    } else {
        bucket.tail->setWaitNext(thread);
    }
    bucket.tail = thread;
}

}  // namespace

namespace kernel {

int sync::waitOnUserAddress(volatile uint32_t* address, uint32_t expected) {
    Task* task      = task::getCurrent();
    Thread* current = Thread::getCurrent();
    if (task == nullptr || current == nullptr || address == nullptr) {
        return -1;
    }

    const uint64_t key = waitKey(*task, address);
    if (key == 0) return -1;
    WaitBucket& bucket   = buckets[bucketIndex(key)];
    const uint64_t flags = interrupt::disable();
    bucket.lock.lock();
    uint32_t value = 0;
    if (!syscall::copyFromUser(&value, const_cast<const uint32_t*>(address),
                               sizeof(value))) {
        bucket.lock.unlock();
        interrupt::restore(flags);
        return -1;
    }
    if (value != expected) {
        bucket.lock.unlock();
        interrupt::restore(flags);
        return 1;
    }
    current->setWaitAddress(address);
    current->setWaitKey(key);
    append(bucket, current);
    bucket.lock.unlock();
    Thread::blockCurrent();
    current->setWaitAddress(nullptr);
    current->setWaitKey(0);
    interrupt::restore(flags);
    return 0;
}

uint32_t sync::wakeUserAddress(volatile uint32_t* address, uint32_t count) {
    Task* task = task::getCurrent();
    if (task == nullptr || address == nullptr || count == 0) {
        return 0;
    }

    const uint64_t key = waitKey(*task, address);
    if (key == 0) return 0;
    WaitBucket& bucket   = buckets[bucketIndex(key)];
    Thread* wakeHead     = nullptr;
    Thread* wakeTail     = nullptr;
    uint32_t woken       = 0;
    const uint64_t flags = interrupt::disable();
    bucket.lock.lock();
    Thread* previous = nullptr;
    Thread* current  = bucket.head;
    while (current != nullptr && woken < count) {
        Thread* next = current->getWaitNext();
        if (current->getWaitKey() == key) {
            if (previous == nullptr) {
                bucket.head = next;
            } else {
                previous->setWaitNext(next);
            }
            if (bucket.tail == current) {
                bucket.tail = previous;
            }
            current->setWaitNext(nullptr);
            current->clearWaitRegistration();
            if (wakeTail == nullptr) {
                wakeHead = current;
            } else {
                wakeTail->setWaitNext(current);
            }
            wakeTail = current;
            ++woken;
        } else {
            previous = current;
        }
        current = next;
    }
    bucket.lock.unlock();

    while (wakeHead != nullptr) {
        Thread* thread = wakeHead;
        wakeHead       = wakeHead->getWaitNext();
        thread->setWaitNext(nullptr);
        Thread::wake(thread);
    }
    interrupt::restore(flags);
    return woken;
}

}  // namespace kernel
