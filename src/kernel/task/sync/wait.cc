/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sync/wait.cc
 * Physical-address-keyed waits for mapped user synchronization words.
 */

#include "kernel/cpu.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace kernel::task {

namespace {

// Fixed hash buckets keep independent synchronization words from requiring one
// global waiter lock; collisions are resolved by comparing the full key.
constexpr uint32_t bucketCount = 64;

// A bucket owns a FIFO of threads, each carrying the exact physical wait key.
struct WaitBucket {
    SpinLock lock;
    Thread* head = nullptr;
    Thread* tail = nullptr;
};

WaitBucket buckets[bucketCount];

// Physical-address hashing chooses a bucket, not the identity of a wait.
uint32_t bucketIndex(uint64_t key) {
    return static_cast<uint32_t>(key % bucketCount);
}

// Translate while holding the address-space lock, then use the physical byte
// address so different virtual mappings of the same shared word match. This
// does not pin the page for the duration of the wait; callers must keep the
// synchronization word mapped and alive. Zero means unusable mapping.
uint64_t waitKey(Task& task, const volatile uint32_t* address) {
    const uint64_t virtualAddress = reinterpret_cast<uint64_t>(address);
    PreemptionSpinLockGuard guard(task.addressSpace().lock());
    uint64_t flags          = 0;
    const uint64_t physical = kernel::mm::getPhysicalAddressInAddressSpace(
        task.addressSpace().rootPhysicalAddress(), virtualAddress, &flags);
    return physical != 0 && ((flags & kernel::mm::pageUser) != 0 ||
                             (task.isSystemService() &&
                              virtualAddress >= 0xffff800000000000ULL))
               ? physical
               : 0;
}

// Caller holds the bucket lock across compare/register. Pair scheduler wake
// preparation with cancellation registration before exposing the list link.
void append(WaitBucket& bucket, Thread* thread) {
    prepareCurrentWait();
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

// Compare a mapped 32-bit word with expected under its bucket lock and
// register only on equality, closing the compare-versus-wake window. Return 1
// on mismatch, -1 on invalid input, and 0 after the wait path returns.
// Alignment is checked by the syscall/import wrapper; no deadline is provided.
int waitOnUserAddress(volatile uint32_t* address, uint32_t expected) {
    Task* task      = task::getCurrent();
    Thread* current = currentThread();
    if (task == nullptr || current == nullptr || address == nullptr) {
        return -1;
    }

    // Resolve the shared identity before choosing a bucket. Mapping lifetime
    // changes are not coordinated here; user synchronization must keep it
    // stable.
    const uint64_t key = waitKey(*task, address);
    if (key == 0) return -1;
    WaitBucket& bucket   = buckets[bucketIndex(key)];
    const uint64_t flags = cpu::disableInterrupts();
    bucket.lock.lock();
    uint32_t value = 0;
    if (!task::copyFromUser(&value, const_cast<const uint32_t*>(address),
                            sizeof(value))) {
        bucket.lock.unlock();
        cpu::restoreInterrupts(flags);
        return -1;
    }
    if (value != expected) {
        bucket.lock.unlock();
        cpu::restoreInterrupts(flags);
        return 1;
    }
    current->setWaitAddress(address);
    current->setWaitKey(key);
    append(bucket, current);
    bucket.lock.unlock();
    blockWaitingThread();
    current->setWaitAddress(nullptr);
    current->setWaitKey(0);
    cpu::restoreInterrupts(flags);
    return 0;
}

// Remove up to count exact-key waiters while holding the bucket lock, then
// wake the detached list outside it. A colliding key must remain queued.
// Return the number removed, not the number of threads already executing.
uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count) {
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
    const uint64_t flags = cpu::disableInterrupts();
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

    // Queue links no longer belong to the bucket; clear each temporary wake
    // link before handing the thread back to the scheduler.
    while (wakeHead != nullptr) {
        Thread* thread = wakeHead;
        wakeHead       = wakeHead->getWaitNext();
        thread->setWaitNext(nullptr);
        wakeThread(thread);
    }
    cpu::restoreInterrupts(flags);
    return woken;
}

}  // namespace kernel::task
