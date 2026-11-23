/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/wait.cc
 */

#include "kernel/mm.h"
#include "kernel/task.h"

namespace task {

    /*
     * 扫描子进程，并在需要时登记一个等待；整个检查过程不带着任务锁睡眠。
     * 参数：
     *   taskId  —— 要匹配哪种子进程：-1 表示任意子进程；>0 表示指定 PID；
     *              0 表示与本进程同一进程组；<-1 表示进程组号为 -taskId。
     *   waiter  —— 发起等待的线程；登记之后由它自己进入睡眠。
     *   status  —— 输出：按传统 wait 格式编码的退出/停止状态。
     *   options —— 位掩码：1=WNOHANG（不要阻塞），2=关心停止事件，
     *              4=关心退出事件，8=关心继续运行事件；0x01000000 表示
     *              waitid 的“看一眼但不消费”（WNOWAIT），0x10000000 表示
     *              调用方用的是 waitid 语义。
     *   childId —— 输出：命中的子进程 ID。
     *   event   —— 可选输出：结构化的子进程事件描述。
     * 返回：0=已有结果；1=已登记等待，之后靠唤醒重新驱动；2=NOHANG 且当前
     *       没有就绪事件；-1=没有符合条件的子进程。
     * 调用前提：waiter 是当前线程；函数内部自己加锁，调用者不要持锁。
     * 与其它模块配合：服务层在被唤醒之后会重新调用本函数，再走一遍上面的判断。
     */
    int Task::waitForChild(int64_t taskId, Thread* waiter, int& status,
                           uint32_t options, uint64_t& childId,
                           os::wire::ChildEvent* event) {
        if (waiter == nullptr) {
            return -1;
        }

        TaskReference reaped;
        bool found        = false;
        const bool waitId = (options & 0x10000000U) != 0;
        const bool peek   = (options & 0x01000000U) != 0;
        {
            InterruptSpinLockGuard guard(lock_);
            Task** link = &firstChild_;

            while (*link != nullptr) {
                Task* child = *link;

                // 先按 PID
                // 或进程组筛选孩子，再读取其状态；不符合选择条件的退出事件不能被本次
                // wait 消费。
                const bool matches =
                    taskId == -1 ||
                    (taskId > 0 &&
                     child->id_ == static_cast<uint64_t>(taskId)) ||
                    (taskId == 0 && child->taskGroupId_ == taskGroupId_) ||
                    (taskId < -1 &&
                     child->taskGroupId_ == static_cast<uint64_t>(-taskId));

                if (!matches) {
                    link = &child->nextSibling_;
                    continue;
                }

                found           = true;
                bool terminated = false;
                bool stateEvent = false;
                {
                    InterruptSpinLockGuard childGuard(child->lock_);
                    // 如果内核还在写 core dump（崩溃转储），就不要对外暴露最终
                    // 退出状态：dumped 标志可能在这期间又变化，调用者会读到
                    // 半成品。停止/继续事件不受影响，仍然可以单独被选中上报。
                    const bool exited =
                        child->state_ == TaskState::Terminated &&
                        !coreDump().pending(*child);
                    terminated = exited && (!waitId || (options & 4U));

                    // 把正常退出码、信号退出和已完成转储分别编码到传统 wait
                    // 状态，结构化事件另行填充。
                    if (terminated) {
                        status =
                            child->exitSignal_
                                ? static_cast<int>(child->exitSignal_) |
                                      ((child->coreState_ ==
                                            os::wire::CoreState::Complete ||
                                        child->coreState_ ==
                                            os::wire::CoreState::Truncated)
                                           ? 0x80
                                           : 0)
                                : (child->exitCode_ & 0xff) << 8;
                        stateEvent = true;

                        if (event) {
                            *event = {
                                child->id_, child->uid_,
                                child->exitSignal_
                                    ? ((child->coreState_ ==
                                            os::wire::CoreState::Complete ||
                                        child->coreState_ ==
                                            os::wire::CoreState::Truncated)
                                           ? 3U
                                           : 2U)
                                    : 1U,
                                child->exitSignal_
                                    ? static_cast<int32_t>(child->exitSignal_)
                                    : child->exitCode_ & 255};
                        }
                    } else if (!exited && (options & 2U) != 0 &&
                               child->stopped_ && !child->stopReported_) {
                        if (!peek) {
                            child->stopReported_ = true;
                        }

                        status =
                            (static_cast<int>(child->stopSignal_) << 8) | 0x7f;
                        stateEvent = true;

                        if (event) {
                            *event = {child->id_, child->uid_, 5,
                                      static_cast<int32_t>(child->stopSignal_)};
                        }
                    } else if (!exited && (options & 8U) != 0 &&
                               child->continuedPending_) {
                        if (!peek) {
                            child->continuedPending_ = false;
                        }

                        status     = 0xffff;
                        stateEvent = true;

                        if (event) {
                            *event = {child->id_, child->uid_, 6, 18};
                        }
                    }
                }

                // WNOWAIT：只上报事件，不消费停止/继续标志，也不把已退出的子
                // 进程从链表上摘掉，这样后面一次真正消费的 wait 仍然能拿到它。
                if (stateEvent && peek) {
                    childId = child->id_;

                    return 0;
                }

                if (terminated) {
                    *link               = child->nextSibling_;
                    child->nextSibling_ = nullptr;

                    // 摘下孩子后先保留局部引用，释放链表原引用也不会让本次结果处理中的对象失效。
                    reaped  = TaskReference(child);
                    childId = child->id_;
                    // 把物理清理交给 worker 线程，赶在等待者可能被杀死之前。
                    // 本地的引用和队列里的引用一起取代原来链表持有的那份所有权。
                    taskReclaimer().submit(*child);
                    child->unlinkGlobal();
                    child->release();
                    break;
                }

                if (stateEvent) {
                    childId = child->id_;

                    return 0;
                }

                link = &child->nextSibling_;
            }

            // 必须在释放这把锁之前登记。通知方用的是同一把锁，所以不会出现
            // “已经检查完、还没真正睡下”时事件被漏掉的竞态；
            // prepareCurrentWait 还会兜住那些在阻塞之前就已经到达的唤醒。
            if (reaped == nullptr && found && (options & 1U) == 0) {
                prepareCurrentWait();
                waiter->registerWait(lock_, childWaiters_);

                // 没有可领取事件时才挂到父任务等待链；登记后由上层在所有状态锁释放后真正睡眠。
                waiter->setWaitNext(childWaiters_);
                childWaiters_ = waiter;
            }
        }

        // 到这里，引用和 worker 已经独立于父进程的子进程链表持有生命周期。
        // 物理清理本身可能还要用父进程锁（孤儿收养、通知），所以在锁外等它完成。
        if (reaped != nullptr) {
            reaped->destroy();

            return 0;
        }

        if (!found) {
            return -1;
        }

        if ((options & 1U) != 0) {
            childId = 0;
            status  = 0;

            return 2;
        }

        return 1;
    }

    /*
     * 在任务锁内把等待子进程事件的线程从链上全部摘下，解锁之后再逐个唤醒。
     * 为什么解锁后才唤醒：唤醒要让目标线程去抢别的 CPU 的运行队列锁，不应该
     * 在持锁期间做，否则会拉长别人的等待时间。
     * 每个被唤醒的线程都要自己重新检查 PID/事件过滤条件——唤醒只表示“可能
     * 有变化”，并不代表某一个子进程的结果已经被它预订。
     */
    void Task::notifyChildStateChange() {
        Thread* waiters = nullptr;
        {
            InterruptSpinLockGuard guard(lock_);
            waiters       = childWaiters_;
            childWaiters_ = nullptr;
        }

        while (waiters != nullptr) {
            Thread* waiter = waiters;
            waiters        = waiters->getWaitNext();
            waiter->setWaitNext(nullptr);
            waiter->clearWaitRegistration();
            wakeThread(waiter);
        }
    }

    /*
     * 等待同一进程内的另一个线程结束（join）。
     * 限制：只能 join 兄弟线程——自己 join 自己，或目标的结果已经被别人消费
     * 过，都直接返回 -1。
     * 返回类别与 waitForChild 一致：0=已有结果；1=已登记等待；2=noHang 下目标
     * 还没结束。
     * 一次成功的 join 会消费掉退出码，但真正释放 Thread 对象还要等旧的执行栈
     * 被上下文切换彻底退掉（见 retireJoinedThread），这两步谁先发生都可以。
     * 锁：本函数获取 lock_ 并一直持有到返回，内部会操作目标的 join 等待队列。
     */
    int Task::waitForThread(uint64_t threadId, Thread* waiter, int& exitCode,
                            bool noHang) {
        if (waiter == nullptr || waiter->getId() == threadId) {
            return -1;
        }

        InterruptSpinLockGuard guard(lock_);

        // 持任务锁查找线程，join 的目标必须属于本任务且不能是等待者自己。
        Thread* target = threads_;

        while (target != nullptr && target->getId() != threadId) {
            target = target->getTaskNext();
        }

        if (target == nullptr || target->isJoinConsumed()) {
            return -1;
        }

        if (target->getState() == ThreadState::Terminated) {
            exitCode = target->getExitCode();
            target->setJoinConsumed(true);

            if (target->isStackRetired()) {
                reapJoinedThreadLocked(target);
            }

            return 0;
        }

        // 非阻塞等待只查询当前状态，不把调用者挂入 join 等待链。
        if (noHang) {
            exitCode = 0;

            return 2;
        }

        prepareCurrentWait();
        waiter->registerWait(lock_, target->accessJoinWaiters(),
                             &target->accessJoinWaitersTail());
        waiter->setWaitNext(nullptr);

        if (target->getJoinWaitersTail() == nullptr) {
            target->setJoinWaiters(waiter);
        } else {
            target->getJoinWaitersTail()->setWaitNext(waiter);
        }

        target->setJoinWaitersTail(waiter);
        exitCode = 0;

        return 1;
    }

    /*
     * 调用者必须已经持有 lock_，并且已经确认“join 已消费”和“栈已退役”两件事。
     * 释放 Thread 之前先从任务的线程链表里摘掉它，否则任务遍历会顺着它旧的
     * taskNext 指针走进已经释放的内存。
     */
    void Task::reapJoinedThreadLocked(Thread* thread) {
        Thread** link = &threads_;

        while (*link && *link != thread) {
            link = &(*link)->accessTaskNext();
        }

        if (!*link) {
            return;
        }

        *link = thread->getTaskNext();
        if (mainThread_ == thread) {
            mainThread_ = threads_;
        }

        mm::freeHeap(thread);
    }

    /*
     * 完成 join/栈生命周期握手的另一半：join 与栈退役谁先发生都可以，只有
     * 后到的那个才会真正释放 Thread（先到者只留下标记）。
     * 锁：内部获取 lock_，调用者在调用前不要持有它。
     */
    void Task::retireJoinedThread(Thread* thread) {
        InterruptSpinLockGuard guard(lock_);
        thread->setStackRetired(true);

        if (thread->isJoinConsumed()) {
            reapJoinedThreadLocked(thread);
        }
    }

    /*
     * 被取消的原生 IPC 等待，可能已经登记进了某个通用等待队列。
     * 如果那次调用其实已经失败，就不要再睡下去；返回之前必须把登记摘掉，
     * 否则队列的拥有者会一直保留一个指向已失效栈/线程的链接。
     */
    void blockWaitingThread() {
        if (!currentThread()->waitError()) {
            task::blockCurrentThread();
        }

        if (currentThread()->waitError()) {
            currentThread()->cancelWait();
        }
    }

}  // namespace task
