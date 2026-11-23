/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sync/sync.cc
 * 实现等待队列、互斥锁、信号量、一次性初始化及线程强制清理。
 */

#include "kernel/sync.h"

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/task.h"

namespace task {

    /*
     * 将线程追加到等待链表尾部，并登记链表所属锁，方便强制退出时安全摘除。
     */
    static void appendWaiter(SpinLock& lock, Thread*& head, Thread*& tail,
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

    /*
     * 取出队首等待线程并维护尾指针，保证空队列时头尾同时为空。
     */
    static Thread* removeWaiter(Thread*& head, Thread*& tail) {
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

    /*
     * 自检使用的初始化回调，只增加一次计数，用于确认 Once 不会重复执行初始化。
     */
    static void markOnce(void* argument) {
        *static_cast<uint32_t*>(argument) = 0x6f6e6365U;
    }

    /*
     * 初始化等待队列的锁和头尾指针。
     */
    void WaitQueue::init() {
        lock_.init();
        head_ = nullptr;
        tail_ = nullptr;
    }

    /*
     * 在队列锁下登记当前线程的等待状态，再阻塞。
     * 登记与状态切换配合，避免唤醒发生在真正睡下之前而丢失。
     */
    void WaitQueue::wait() {
        Thread* current = currentThread();

        if (current == nullptr) {
            return;
        }

        const uint64_t flags = cpu::disableInterrupts();
        lock_.lock();

        // 队列锁内先登记再睡眠，唤醒方即使抢在实际切走前到达，也能通过调度器的待唤醒标记留下通知。
        appendWaiter(lock_, head_, tail_, current);
        lock_.unlock();

        // 登记完成以后先解队列锁再阻塞，否则唤醒方拿不到锁，等待将永远无法结束。
        blockWaitingThread();
        cpu::restoreInterrupts(flags);
    }

    /*
     * 取出并唤醒一个等待线程，返回是否确实找到等待者。
     */
    bool WaitQueue::wakeOne() {
        Thread* thread;
        const uint64_t flags = cpu::disableInterrupts();
        lock_.lock();

        // 在队列锁下摘链，保证两个并发唤醒者不会选中同一个等待节点。
        thread = removeWaiter(head_, tail_);
        lock_.unlock();

        // 摘链后再交给调度器唤醒，缩短队列锁临界区并避免嵌套运行队列锁。
        if (thread != nullptr) {
            wakeThread(thread);
        }

        cpu::restoreInterrupts(flags);

        return thread != nullptr;
    }

    /*
     * 重复唤醒队列中的线程，返回本次唤醒数量。
     */
    uint32_t WaitQueue::wakeAll() {
        uint32_t count = 0;

        while (wakeOne()) {
            ++count;
        }

        return count;
    }

    /*
     * 初始化可睡眠互斥锁，清空所有者和等待队列。
     */
    void Mutex::init() {
        lock_.init();
        owner_       = nullptr;
        waiters_     = nullptr;
        waitersTail_ = nullptr;
    }

    /*
     * 获取互斥锁。
     * 空闲时直接成为所有者，已占用时加入等待队列并睡眠，被唤醒后按交接规则继续。
     */
    void Mutex::lock() {
        Thread* current = currentThread();

        if (current == nullptr) {
            return;
        }

        while (true) {
            const uint64_t flags = cpu::disableInterrupts();
            lock_.lock();

            // 所有者为空才能认领；检查和写入 owner_
            // 都在内部自旋锁内，避免两个线程同时成功。
            if (owner_ == nullptr) {
                owner_ = current;
                lock_.unlock();
                cpu::restoreInterrupts(flags);

                return;
            }

            // 锁仍被占用时先登记等待，再放内部锁睡眠；醒来后回到循环重新竞争，不能直接假定自己已拥有锁。
            appendWaiter(lock_, waiters_, waitersTail_, current);
            lock_.unlock();
            blockWaitingThread();
            cpu::restoreInterrupts(flags);
        }
    }

    /*
     * 尝试立即获取互斥锁，已被占用时返回失败，不进入睡眠。
     */
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

    /*
     * 释放当前线程持有的互斥锁，再唤醒一个等待者；被唤醒者回到加锁循环重新竞争。
     */
    void Mutex::unlock() {
        Thread* waiter       = nullptr;
        const uint64_t flags = cpu::disableInterrupts();
        lock_.lock();

        // 只允许持有者释放，其他线程不能误清
        // owner_，也不能替持有者唤醒等待队列。
        if (owner_ == currentThread()) {
            owner_ = nullptr;
            waiter = removeWaiter(waiters_, waitersTail_);
        }

        lock_.unlock();

        // 在内部锁外唤醒一个等待者；它恢复运行后仍需重新检查并取得互斥锁。
        if (waiter != nullptr) {
            wakeThread(waiter);
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 设置信号量初始数量，并清空等待队列。
     */
    void Semaphore::init(uint32_t count) {
        lock_.init();
        count_       = count;
        waiters_     = nullptr;
        waitersTail_ = nullptr;
    }

    /*
     * 消耗一个信号量名额。
     * 名额不足时先登记等待再睡眠，直到 signal 提供可用名额。
     */
    void Semaphore::wait() {
        Thread* current = currentThread();

        if (current == nullptr) {
            return;
        }

        const uint64_t flags = cpu::disableInterrupts();
        lock_.lock();

        // 有名额直接扣减后返回；这一步与 signal 的计数更新由同一把锁保护。
        if (count_ != 0) {
            --count_;
            lock_.unlock();
            cpu::restoreInterrupts(flags);

            return;
        }

        // 没有名额时把线程排入等待队列，后续 signal 优先把新名额交给等待者。
        appendWaiter(lock_, waiters_, waitersTail_, current);
        lock_.unlock();
        blockWaitingThread();
        cpu::restoreInterrupts(flags);
    }

    /*
     * 不阻塞地尝试消耗一个名额，没有名额时立即返回失败。
     */
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

    /*
     * 归还一个名额。
     * 优先交给等待者，没有等待者时才增加可用计数。
     */
    void Semaphore::signal() {
        // 先尝试取等待者；有等待者时本次名额用于放行它，不再额外增加公开计数。
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

    /*
     * 初始化一次性执行状态和等待队列。
     */
    void Once::init() {
        lock_.init();
        state_       = 0;
        waiters_     = nullptr;
        waitersTail_ = nullptr;
    }

    /*
     * 保证初始化函数只被一个线程执行。
     * 其他线程登记等待；初始化完成后统一唤醒。当前实现要求初始化回调正常返回。
     */
    void Once::run(Initializer initializer, void* argument) {
        if (initializer == nullptr) {
            return;
        }

        Thread* current      = currentThread();
        const uint64_t flags = cpu::disableInterrupts();
        lock_.lock();

        // 状态二表示初始化已经完成，后来调用者无需再执行回调，也不用进入等待队列。
        if (state_ == 2) {
            lock_.unlock();
            cpu::restoreInterrupts(flags);

            return;
        }

        // 第一个线程把状态改为进行中后放锁执行回调，避免回调过程长时间持有自旋锁。
        if (state_ == 0) {
            state_ = 1;
            lock_.unlock();
            cpu::restoreInterrupts(flags);
            initializer(argument);

            lock_.lock();

            // 回调正常返回后才发布完成状态，同时摘下整条等待链，随后在锁外逐个唤醒。
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

        // 已有线程负责初始化时，其他线程只登记并等待，不重复执行回调。
        if (current != nullptr) {
            appendWaiter(lock_, waiters_, waitersTail_, current);
        }

        lock_.unlock();

        if (current != nullptr) {
            blockWaitingThread();
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 运行同步原语的启动自检，检查互斥锁、信号量和一次性初始化的基本行为。
     */
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

        device::serialWrite(
            "[SYNC] SMP synchronization primitives initialized\n");
    }

    /*
     * 析构清理记录，执行仍未归还的资源释放动作。
     */
    ThreadCleanup::~ThreadCleanup() { reset(); }

    /*
     * 把资源和释放函数挂到当前线程清理链。
     * 记录保存在调用者对象中，强制退出路径也能找到并执行。
     */
    void ThreadCleanup::attach(void* object, void (*release)(void*)) {
        const uint64_t flags = cpu::disableInterrupts();

        // 清理节点属于当前线程，挂链与资源登记要连贯完成，强制退出才能找到这份资源。
        owner_   = currentThread();
        object_  = object;
        release_ = release;

        if (owner_) {
            next_ = owner_->cleanupList();

            if (next_) {
                next_->previous_ = this;
            }

            owner_->cleanupList() = this;
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 从线程清理链摘下记录但不释放资源，用于显式转移所有权。
     */
    void ThreadCleanup::detach() {
        const uint64_t flags = cpu::disableInterrupts();

        if (owner_) {
            if (previous_) {
                previous_->next_ = next_;
            } else {
                owner_->cleanupList() = next_;
            }

            if (next_) {
                next_->previous_ = previous_;
            }
        }

        owner_    = nullptr;
        previous_ = next_ = nullptr;
        object_           = nullptr;
        release_          = nullptr;
        cpu::restoreInterrupts(flags);
    }

    /*
     * 先摘除清理记录，再调用释放函数。
     * 先摘除可避免释放过程中再次触发同一清理动作。
     */
    void ThreadCleanup::reset() {
        const uint64_t flags   = cpu::disableInterrupts();
        void* object           = object_;
        void (*release)(void*) = release_;

        // 先从清理链移除，再调用用户提供的释放函数，避免释放过程重入时再次执行同一个节点。
        detach();

        // 空记录不做任何动作；有效记录只使用前面保存的局部值，回调可以销毁包含此节点的对象。
        if (object && release) {
            release(object);
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 在线程强制退出时执行其全部清理记录，归还不会通过正常 C++
     * 析构释放的临时资源。
     */
    void ThreadCleanup::releaseAll(Thread& thread) {
        while (thread.cleanupList()) {
            thread.cleanupList()->reset();
        }
    }

    /*
     * 记录当前线程所属的等待链及保护锁，使取消操作知道应该从哪里摘除线程。
     */
    void Thread::registerWait(SpinLock& lock, Thread*& head, Thread** tail) {
        waitOwnerLock = &lock;
        waitOwnerHead = &head;
        waitOwnerTail = tail;
    }

    /*
     * 清空线程等待登记，不改变实际队列；调用者应已完成摘链。
     */
    void Thread::clearWaitRegistration() {
        waitOwnerLock = nullptr;
        waitOwnerHead = nullptr;
        waitOwnerTail = nullptr;
    }

    /*
     * 按照登记的队列锁安全撤销当前等待，维护头尾指针，避免已退出线程继续留在等待链中。
     */
    void Thread::cancelWait() {
        // 调用者必须先让线程停止并确认 CPU
        // 已切走；正常出队和这里的取消由队列所属锁协调。

        // 先保存等待登记所用的锁，再在它保护下检查和摘链，不能直接修改其他同步对象的队列。
        SpinLock* lock = waitOwnerLock;

        if (!lock) {
            return;
        }

        InterruptSpinLockGuard guard(*lock);

        if (!waitOwnerHead) {
            return;
        }

        Thread** link    = waitOwnerHead;
        Thread* previous = nullptr;

        while (*link && *link != this) {
            previous = *link;
            link     = &(*link)->accessWaitNext();
        }

        if (*link) {
            *link = waitNext;
            if (waitOwnerTail && *waitOwnerTail == this) {
                *waitOwnerTail = previous;
            }
        }

        waitNext = nullptr;
        clearWaitRegistration();
    }

}  // namespace task
