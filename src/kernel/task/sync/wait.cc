/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sync/wait.cc
 * 把用户地址的值检查与睡眠登记结合，避免检查后错过唤醒。
 */

#include "kernel/cpu.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace task {

    // 固定数量散列桶分散不同地址的等待竞争；散列冲突通过完整等待键区分。
    constexpr uint32_t bucketCount = 64;

    // 每个桶维护先进先出线程链，线程自己保存完整的物理等待键。
    struct WaitBucket {
            SpinLock lock;
            Thread* head        = nullptr;
            Thread* tail        = nullptr;
            uint64_t nextExpiry = UINT64_MAX;
    };

    static WaitBucket buckets[bucketCount];
    constexpr uint64_t readinessKey = UINT64_MAX;
    static uint64_t readinessSequence;

    uint64_t readinessGeneration() {
        return __atomic_load_n(&readinessSequence, __ATOMIC_ACQUIRE);
    }

    /*
     * 把等待键散列到固定桶，减少不同用户地址等待时的相互干扰。
     */
    static uint32_t bucketIndex(uint64_t key) {
        return static_cast<uint32_t>(key % bucketCount);
    }

    /*
     * 构造用户地址等待键。
     * 结合任务或共享页面身份，使独立进程的同一虚拟地址不会错误互相唤醒。
     */
    static uint64_t waitKey(Task& task, const volatile uint32_t* address) {
        const uint64_t virtualAddress = reinterpret_cast<uint64_t>(address);
        PreemptionSpinLockGuard guard(task.addressSpace().lock());
        uint64_t flags          = 0;
        const uint64_t physical = mm::getPhysicalAddressInAddressSpace(
            task.addressSpace().rootPhysicalAddress(), virtualAddress, &flags);

        return physical != 0 && ((flags & mm::pageUser) != 0 ||
                                 (task.isSystemService() &&
                                  virtualAddress >= 0xffff800000000000ULL))
                   ? physical
                   : 0;
    }

    /*
     * 把线程追加到等待桶，并登记取消等待所需的信息。
     */
    static void append(WaitBucket& bucket, Thread* thread) {
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

    /*
     * 仅当用户地址的值仍等于 expected 时睡眠。
     * 在桶锁下再次检查值并登记等待，避免检查之后、睡眠之前错过唤醒。
     */
    static int waitForKey(volatile uint32_t* address, uint64_t key,
                          uint64_t expected, uint64_t deadline,
                          uint64_t signals) {
        Task* task      = task::getCurrent();
        Thread* current = currentThread();
        if (!task || !current || !key) return -1;

        if (!address && (current->signalGeneration != signals ||
                         task->hasInterruptingSignal(current)))
            return -10;

        WaitBucket& bucket   = buckets[bucketIndex(key)];
        const uint64_t flags = cpu::disableInterrupts();
        bucket.lock.lock();
        uint32_t userValue = 0;
        if (address && !task::copyFromUser(&userValue,
                                           const_cast<const uint32_t*>(address),
                                           sizeof(userValue))) {
            bucket.lock.unlock();
            cpu::restoreInterrupts(flags);

            return -1;
        }

        const uint64_t value = address ? userValue : readinessGeneration();

        // 持桶锁后再次读值；值已改变就不睡眠，堵住“检查完成后错过唤醒”的窗口。
        if (value != expected) {
            bucket.lock.unlock();
            cpu::restoreInterrupts(flags);

            return 1;
        }

        if (deadline && time::getMonotonicTime() >= deadline) {
            bucket.lock.unlock();
            cpu::restoreInterrupts(flags);
            return 2;
        }

        current->setWaitAddress(address);
        current->setWaitKey(key);
        current->setWakeDeadlineNanoseconds(deadline);
        current->setAddressWaitResult(0);
        if (deadline && deadline < bucket.nextExpiry) {
            __atomic_store_n(&bucket.nextExpiry, deadline, __ATOMIC_RELEASE);
        }
        append(bucket, current);
        bucket.lock.unlock();

        // 已经登记到等待桶才放锁睡眠，返回后按登记状态完成清理。
        if (task->hasInterruptingSignal(current))
            interruptUserAddressWait(current);
        blockWaitingThread();
        const int result = current->getAddressWaitResult();
        current->cancelWait();
        current->setWaitAddress(nullptr);
        current->setWaitKey(0);
        current->setWakeDeadlineNanoseconds(0);
        cpu::restoreInterrupts(flags);

        return result;
    }

    int waitOnUserAddress(volatile uint32_t* address, uint32_t expected,
                          uint64_t deadline) {
        Task* owner = task::getCurrent();
        if (!owner || !address) return -1;
        return waitForKey(address, waitKey(*owner, address), expected, deadline,
                          0);
    }

    int waitOnReadiness(uint64_t expected, uint64_t deadline,
                        uint64_t signals) {
        return waitForKey(nullptr, readinessKey, expected, deadline, signals);
    }

    /*
     * 唤醒同一用户地址等待键上的指定数量线程。
     * 逐个摘链并唤醒，不影响散列到同桶的其他地址。
     */
    static uint32_t wakeWaitKey(uint64_t key, uint32_t count) {
        WaitBucket& bucket = buckets[bucketIndex(key)];

        // 先准备临时唤醒链，在桶锁下只摘节点，后面再交给调度器，避免长时间占桶锁。
        Thread* wakeHead     = nullptr;
        Thread* wakeTail     = nullptr;
        uint32_t woken       = 0;
        const uint64_t flags = cpu::disableInterrupts();
        bucket.lock.lock();
        Thread* previous = nullptr;
        Thread* current  = bucket.head;

        // 逐个比较完整等待键，散列到同一个桶的其他地址不应被唤醒；达到数量上限就停止摘链。
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
                current->setWakeDeadlineNanoseconds(0);

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

        // 线程已从桶队列摘下，交还调度器前清空临时唤醒链接，防止它继续被当作队列节点。

        // 已经摘出的节点在桶锁外交还调度器，避免唤醒运行队列时继续持有用户地址桶锁。
        while (wakeHead != nullptr) {
            Thread* thread = wakeHead;
            wakeHead       = wakeHead->getWaitNext();
            thread->setWaitNext(nullptr);
            wakeThread(thread);
        }

        cpu::restoreInterrupts(flags);

        return woken;
    }

    uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count) {
        Task* owner = task::getCurrent();
        if (!owner || !address || !count) return 0;
        const uint64_t key = waitKey(*owner, address);
        return key ? wakeWaitKey(key, count) : 0;
    }

    /* 序号先发布，等待者在同一桶锁下复查并登记；通知不包含用户数据。 */
    void notifyReadiness() {
        __atomic_add_fetch(&readinessSequence, 1ULL, __ATOMIC_RELEASE);
        wakeWaitKey(readinessKey, UINT32_MAX);
    }

    /*
     * 只有桶内最早期限已到才扫描链表。超时与普通唤醒共用桶锁，
     * 因而同一个等待者只会被摘除一次；线程退出时的 cancelWait 也使用该锁。
     */
    void expireUserAddressWaits(uint64_t now) {
        for (WaitBucket& bucket : buckets) {
            if (__atomic_load_n(&bucket.nextExpiry, __ATOMIC_ACQUIRE) > now)
                continue;
            const uint64_t flags = cpu::disableInterrupts();
            bucket.lock.lock();
            uint64_t nextExpiry = UINT64_MAX;
            Thread* previous    = nullptr;
            Thread* current     = bucket.head;
            while (current) {
                Thread* next            = current->getWaitNext();
                const uint64_t deadline = current->getWakeDeadlineNanoseconds();
                if (deadline && deadline <= now) {
                    if (previous)
                        previous->setWaitNext(next);
                    else
                        bucket.head = next;
                    if (bucket.tail == current) bucket.tail = previous;
                    current->setWaitNext(nullptr);
                    current->clearWaitRegistration();
                    current->setWakeDeadlineNanoseconds(UINT64_MAX);
                    current->setAddressWaitResult(2);
                    wakeThread(current);
                } else {
                    if (deadline && deadline < nextExpiry)
                        nextExpiry = deadline;
                    previous = current;
                }
                current = next;
            }
            __atomic_store_n(&bucket.nextExpiry, nextExpiry, __ATOMIC_RELEASE);
            bucket.lock.unlock();
            cpu::restoreInterrupts(flags);
        }
    }

    /* 信号与超时、普通唤醒共用同一个桶锁，摘链后才让线程返回用户态。 */
    void interruptUserAddressWait(Thread* thread) {
        if (!thread) return;
        const uint64_t key = thread->getWaitKey();
        if (!key) return;
        WaitBucket& bucket   = buckets[bucketIndex(key)];
        const uint64_t flags = cpu::disableInterrupts();
        bucket.lock.lock();
        Thread* previous = nullptr;
        Thread* current  = bucket.head;
        while (current && current != thread) {
            previous = current;
            current  = current->getWaitNext();
        }
        if (current) {
            Thread* next = current->getWaitNext();
            if (previous)
                previous->setWaitNext(next);
            else
                bucket.head = next;
            if (bucket.tail == current) bucket.tail = previous;
            current->setWaitNext(nullptr);
            current->clearWaitRegistration();
            current->setAddressWaitResult(-10);
            wakeThread(current);
        }
        bucket.lock.unlock();
        cpu::restoreInterrupts(flags);
    }

}  // namespace task
