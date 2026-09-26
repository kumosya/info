#include "kernel/user_wait.h"

#include "kernel/cpu.h"
#include "kernel/page.h"
#include "kernel/process.h"
#include "kernel/spinlock.h"
#include "kernel/syscall.h"
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

uint64_t waitKey(kernel::Process& process,
                 const volatile uint32_t* address) {
    const uint64_t virtualAddress = reinterpret_cast<uint64_t>(address);
    kernel::PreemptionSpinLockGuard guard(process.addressSpace().lock());
    uint64_t flags = 0;
    const uint64_t physical = kernel::getPhysicalAddressInAddressSpace(
        process.addressSpace().rootPhysicalAddress(), virtualAddress, &flags);
    return physical != 0 && (flags & kernel::pageUser) != 0 ? physical : 0;
}

void append(WaitBucket& bucket, kernel::Thread* thread) {
    thread->waitNext = nullptr;
    if (bucket.tail == nullptr) {
        bucket.head = thread;
    } else {
        bucket.tail->waitNext = thread;
    }
    bucket.tail = thread;
}

}  // namespace

namespace kernel {

int waitOnUserAddress(volatile uint32_t* address, uint32_t expected) {
    Process* process = getCurrentProcess();
    Thread* current = getCurrentThread();
    if (process == nullptr || current == nullptr || address == nullptr) {
        return -1;
    }

    const uint64_t key = waitKey(*process, address);
    if (key == 0) return -1;
    WaitBucket& bucket = buckets[bucketIndex(key)];
    const uint64_t flags = disableInterrupts();
    bucket.lock.lock();
    uint32_t value = 0;
    if (!copyFromUser(&value, const_cast<const uint32_t*>(address),
                      sizeof(value))) {
        bucket.lock.unlock();
        restoreInterrupts(flags);
        return -1;
    }
    if (value != expected) {
        bucket.lock.unlock();
        restoreInterrupts(flags);
        return 1;
    }
    current->waitAddress = address;
    current->waitKey = key;
    append(bucket, current);
    bucket.lock.unlock();
    blockThread();
    current->waitAddress = nullptr;
    current->waitKey = 0;
    restoreInterrupts(flags);
    return 0;
}

uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count) {
    Process* process = getCurrentProcess();
    if (process == nullptr || address == nullptr || count == 0) {
        return 0;
    }

    const uint64_t key = waitKey(*process, address);
    if (key == 0) return 0;
    WaitBucket& bucket = buckets[bucketIndex(key)];
    Thread* wakeHead = nullptr;
    Thread* wakeTail = nullptr;
    uint32_t woken = 0;
    const uint64_t flags = disableInterrupts();
    bucket.lock.lock();
    Thread* previous = nullptr;
    Thread* current = bucket.head;
    while (current != nullptr && woken < count) {
        Thread* next = current->waitNext;
        if (current->waitKey == key) {
            if (previous == nullptr) {
                bucket.head = next;
            } else {
                previous->waitNext = next;
            }
            if (bucket.tail == current) {
                bucket.tail = previous;
            }
            current->waitNext = nullptr;
            if (wakeTail == nullptr) {
                wakeHead = current;
            } else {
                wakeTail->waitNext = current;
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
        wakeHead = wakeHead->waitNext;
        thread->waitNext = nullptr;
        wakeThread(thread);
    }
    restoreInterrupts(flags);
    return woken;
}

}  // namespace kernel
