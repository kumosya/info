/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/exit.cc
 *
 * 本文件负责“任务（进程）如何结束”，包含三部分：
 *   1. 线程退出时记账，并唤醒等待它结束的线程；
 *   2. 整个任务终止时停掉其余线程；
 *   3. 任务结束后通知父进程（发送 SIGCHLD）。
 *
 * 关键不变量：
 *   - 每个线程的退出只记账一次，threadCount_ 不会重复减一。
 *   - 最后一个用户线程退出后，任务才进入 Terminated 状态。
 *   - 句柄（内核对象的引用编号）关闭、页表（地址映射表）销毁，
 *     必须等回收器确认没有 CPU 还在执行该任务。
 *   - 父进程对同一个子进程只会收到一次退出通知。
 *
 * 建议阅读顺序：
 *   exitCurrentThread -> notifyThreadExit -> terminateOtherThreads
 *   -> notifyChildExit；资源真正释放的流程见 reclaim.cc。
 *
 * 术语提示：
 *   退出状态（exit status）是进程结束时留给父进程的小整数，
 *   父进程可用 wait 取走。
 *   僵尸进程（zombie）是已经退出、但退出状态还没被父进程取走的
 *   进程，它只剩少量记账信息等待父进程处理。
 *   回收器（reclaimer）是专门在安全时机释放任务资源的后台线程，
 *   详见 reclaim.cc。
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/task.h"

namespace task {

    /*
     * 读取任务的退出状态。
     * 退出状态（exit status）是进程结束时留给父进程的小整数，
     * 父进程之后可以用 wait 取走。
     * 为什么要加锁：其它 CPU 可能正在写 exitCode_，在任务状态锁的
     * 保护下读取才能看到完整的值，也避免和退出流程交错。
     * 输入：无。输出：退出状态。调用前提：无，函数自己加锁。
     */
    int Task::exitCode() {
        InterruptSpinLockGuard guard(lock_);

        return exitCode_;
    }

    /*
     * 线程退出的统一记账入口，由 exitCurrentThread 在关中断时调用。
     * 它做三件事：
     *   1. 摘下所有正在 join（等待该线程结束）的线程，稍后逐个唤醒；
     *   2. 保证该线程的退出只被计数一次：只有第一次看到它退出时，
     *      才把 threadCount_ 减一；
     *   3. 如果线程数减到 0 且当前不是空闲（idle）任务，说明整个任务
     *      已经结束：改为 Terminated、禁止再接受新线程，并记录退出状态。
     *
     * 为什么任务结束后不能立刻释放资源：可能还有别的 CPU 正在这个
     * 任务的内核栈或页表上运行。所以这里只把任务提交给回收器；
     * 关闭句柄、销毁页表要等回收器确认没有 CPU 还在执行该任务之后
     * 才进行（见 reclaim.cc 的 destroyResources）。
     *
     * 输入：thread，正在退出的线程。
     * 调用前提：线程应已标记为 Terminated；函数内部会自己拿任务锁。
     * 失败路径：无返回值；如果任务尚未结束，只做线程记账和唤醒。
     * 实现细节：等待者链表在锁内摘下，唤醒放在锁外，避免持锁时调度。
     */
    void Task::notifyThreadExit(Thread* thread) {
        Thread* waiters     = nullptr;
        bool taskTerminated = false;
        {
            InterruptSpinLockGuard guard(lock_);

            // 在任务锁内一次摘下 join
            // 等待链，后面唤醒放在锁外，避免唤醒过程重新进入任务状态锁。
            waiters = thread->getJoinWaiters();
            thread->setJoinWaiters(nullptr);
            thread->setJoinWaitersTail(nullptr);

            if (!thread->isExitAccounted() && threadCount_ != 0) {
                --threadCount_;
            }

            // 线程退出可能被不同路径观察，借已记账标志保证任务线程数量只减一次。
            const bool newlyExited = !thread->isExitAccounted();
            thread->setExitAccounted(true);

            if (newlyExited && threadCount_ == 0 && this != task::getIdle()) {
                state_          = TaskState::Terminated;
                acceptsThreads_ = false;
                // 信号退出优先：已经记录过信号原因时，不用线程退出码覆盖。
                if (!exitSignal_) {
                    exitCode_ = thread->getExitCode();
                }

                // 回收器会先解除端点（endpoint，任务间通信的收发端）
                // 所有权，再关闭句柄。
                taskTerminated = true;
            }
        }

        while (waiters != nullptr) {
            Thread* waiter = waiters;
            waiters        = waiters->getWaitNext();
            waiter->setWaitNext(nullptr);
            waiter->clearWaitRegistration();
            wakeThread(waiter);
        }

        if (taskTerminated) {
            // 最后一条线程退出只提交回收请求，不能在仍使用本任务栈的上下文立即释放所有资源。
            taskReclaimer().submit(*this);
        }
    }

    /*
     * 终止任务内除调用者以外的所有线程。
     *
     * 做什么：先关闭“允许新线程加入”的开关，再遍历线程链表逐个终止；
     * 如果线程数归零，就把任务状态改为 Terminated 并提交回收。
     * 为什么先关闭 acceptsThreads_：否则遍历过程中可能又有新线程被
     * 创建出来，从而逃过这次终止。
     * 为什么在这里就把兄弟线程标记为“退出已记账”：它们的退出路径
     * 之后可能还会被观察到；提前标记可以避免 threadCount_ 被第二次
     * 减一。这里主动减一之后，计数仍然准确。
     * 当前线程不在这里终止，由调用者 exitCurrent 之后通过
     * exitCurrentThread 完成。
     *
     * 输入：exitCode，整组线程统一使用的退出状态；如果此前已经因信号
     * 记录过退出原因（exitSignal_ 非 0），则保留信号信息，不覆盖。
     * 调用前提：调用者不能已经持有 lock_（函数内部会加锁）。
     * 与其它模块的配合：用 requestRemoteReschedule 通知目标 CPU
     * 重新调度，让远端线程尽快离开。
     */
    void Task::terminateOtherThreads(int exitCode) {
        InterruptSpinLockGuard guard(lock_);

        // 先禁止创建新线程，再遍历终止，防止扫描过程中又有新线程加入而逃过处理。
        acceptsThreads_ = false;

        if (!exitSignal_) {
            exitCode_ = exitCode;
        }

        for (Thread* thread = threads_; thread != nullptr;
             thread         = thread->getTaskNext()) {
            if (thread != currentThread() &&
                thread->getState() != ThreadState::Terminated) {
                task::terminateThread(thread);

                // 这里已主动减过线程数量，后续该线程自己的退出路径不应再扣一次。
                thread->setExitAccounted(true);
                cpu::requestRemoteReschedule(
                    static_cast<uint32_t>(thread->getCpuId()));

                if (threadCount_ != 0) {
                    --threadCount_;
                }
            }
        }

        // 没有用户线程剩余时才发布任务终止并交给异步回收器，调用线程仍存活时由它稍后完成最后退出。
        if (threadCount_ == 0) {
            state_ = TaskState::Terminated;
            taskReclaimer().submit(*this);
        }
    }

    /*
     * 销毁一个“不是当前任务、也不是内核任务”的任务，并等待回收完成。
     * 使用场景：外部需要确认某个任务的所有资源都已释放时调用。
     * 为什么不能销毁当前任务或 idle 任务：当前任务还在用自己的栈，
     * idle 任务是内核常驻任务，都不能在这里拆掉。
     *
     * 流程：
     *   1. 先用 TaskReference 持有一份对象引用，保证等待期间对象不被释放；
     *   2. 提交给回收器。回收器自己持有一份引用，所以即使发起等待的
     *      线程之后被杀掉，清理工作也不会半途而废；
     *   3. 等待 resourceState_ 变成 2（表示资源释放完毕）。等待方式是
     *      让出 CPU（yieldThread），不会霸占处理器空转。
     *
     * 引用计数（reference counting）：对象内部记录“还有多少使用者”，
     * 每多一个使用者就加一，用完后减一；减到 0 才能释放对象。这样多个
     * 线程可以安全地共享同一个 Task 对象，而不必关心谁先退出。
     *
     * 输入：无。输出：无返回值。
     * 调用前提：不能对当前任务或 idle 任务调用。
     * 失败路径：没有失败返回；等待期间允许中断和调度，结束后按调用前
     * 保存的中断状态恢复。
     * 注意：等待者的栈地址不会交给回收器，因此等待者被杀不会让队列
     * 或任务引用悬空（见 reclaim.cc 的说明）。
     */
    void Task::destroy() {
        if (this == task::getIdle() || this == task::getCurrent()) {
            return;
        }

        TaskReference lifetime(this);
        taskReclaimer().submit(*this);
        const uint64_t flags = cpu::disableInterrupts();
        cpu::enableInterrupts();
        // 回收器不会保存这个等待者的栈指针。因此杀死或取消该等待者，
        // 也不会让队列或它持有的任务引用无人处理。
        while (__atomic_load_n(&resourceState_, __ATOMIC_ACQUIRE) != 2U) {
            task::yieldThread();
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 结束当前进程（任务），并保证不返回。
     * 做什么：先让任务内其它线程全部退出，再退出调用线程自己。
     * 为什么是这个顺序：如果不先停掉兄弟线程，它们可能继续访问即将
     * 被回收的地址空间和句柄。
     *
     * 特殊情况：如果当前没有任务（内核上下文）或当前是 idle 任务，
     * 只退出当前线程，绝不拆掉内核任务本身；无所有者的工作线程也
     * 走同一条路径。
     *
     * 输入：exitCode，退出状态；普通用户任务结束后，父进程可以通过
     * wait 读到它。[[noreturn]] 表示这个函数不会返回到调用点。
     */
    [[noreturn]] void exitCurrent(int exitCode) {
        Task* task = getCurrent();

        if (task == nullptr || task == task::getIdle()) {
            exitCurrentThread(exitCode);
        }

        task->terminateOtherThreads(exitCode);
        exitCurrentThread(exitCode);
    }

    /*
     * 退出当前线程，永不返回。
     * 做什么（按顺序）：
     *   1. 关中断，记录退出状态，并把线程状态标成 Terminated；
     *   2. 取消该线程基于栈的 IPC 等待，并释放栈上保存的临时引用，
     *      否则这些对象会一直以为还有人使用；
     *   3. 如果这是任务内的线程，通知所属任务做退出记账
     *      （notifyThreadExit 可能因此把整个任务交给回收器）；
     *   4. 请求重新调度，并切换到别的线程。
     *
     * 为什么最后要 halt：正常路径会在 schedule() 里切走，不会返回；
     * halt 只是兜底，防止异常情况下继续往下执行。
     * 为什么不能自己释放内核栈：代码还在这个栈上运行。内核栈必须由
     * 之后的上下文或任务回收器释放。
     *
     * 输入：exitCode，退出状态。输出：无，函数不返回。
     * 调用前提：可以在任务线程或内核线程中调用；函数自己关中断。
     */
    [[noreturn]] void exitCurrentThread(int exitCode) {
        const uint64_t flags = cpu::disableInterrupts();
        Thread* thread       = currentThread();
        thread->setExitCode(exitCode);
        thread->setState(ThreadState::Terminated);
        Endpoint::cancelThread(thread);
        ThreadCleanup::releaseAll(*thread);

        if (thread->getTask() != nullptr) {
            thread->getTask()->notifyThreadExit(thread);
        }

        task::requestReschedule();
        task::schedule();
        cpu::restoreInterrupts(flags);
        cpu::halt();
    }

    /*
     * 向父进程发布一次子进程退出事件（SIGCHLD）。
     *
     * 什么是 SIGCHLD：子进程退出或状态变化时，内核发给父进程的信号；
     * 这里是 17 号信号。父进程通常用 wait 读取子进程的退出状态。
     * 为什么要“只发一次”：回收器和 core dump 的写入者/看门狗都可能
     * 来通知；exitNotified_ 用原子交换保证只有一个调用真正发信号。
     *
     * core dump（核心转储）：进程崩溃时把内存映像写到文件，方便调试。
     * 事件代码（code）的含义：
     *   1 = 正常退出（CLD_EXITED）；
     *   2 = 被信号杀死（CLD_KILLED）；
     *   3 = 被信号杀死且留下了 core dump（CLD_DUMPED）。
     * 注意：只有 core dump 真正完成或部分完成（Complete/Truncated）
     * 才算 3；只是请求过、或者写失败，都不算。
     * status 字段：正常退出时放退出码的低 8 位，信号退出时放信号编号。
     *
     * 输入：child，已经退出的子进程。
     * 调用前提：函数内部会拿父进程（this）的锁；子进程的退出状态必须
     * 已经确定，或者 core dump 已经不再等待。
     * 失败路径：如果已经通知过，直接返回，不会重复发送。
     * 与其它模块的配合：先更新 childSignalInfo_ 并唤醒 wait 的等待者，
     * 再投递信号。
     */
    void Task::notifyChildExit(Task& child) {
        // 回收器和转储结束路径都可能来通知，原子交换只放行第一次，避免父任务收到重复退出事件。
        if (__atomic_exchange_n(&child.exitNotified_, true, __ATOMIC_ACQ_REL)) {
            return;
        }

        services::wire::ExitInfo info{};

        // 先取得已经确定的退出详情，再写入父任务事件；是否真的生成转储决定事件代码。
        child.getExitInfo(info);
        {
            InterruptSpinLockGuard guard(lock_);
            childSignalInfo_       = {};
            childSignalInfo_.signo = 17;
            childSignalInfo_.pid   = child.id();
            childSignalInfo_.uid   = child.uid();
            childSignalInfo_.code =
                !info.signal ? 1
                : (info.coreState ==
                       static_cast<uint32_t>(services::wire::CoreState::Complete) ||
                   info.coreState ==
                       static_cast<uint32_t>(services::wire::CoreState::Truncated))
                    ? 3
                    : 2;
            childSignalInfo_.status = info.signal
                                          ? static_cast<int>(info.signal)
                                          : child.exitCode() & 0xff;
        }

        // 先唤醒等待子任务状态的线程，再发送 SIGCHLD；父任务可以通过 wait
        // 读取完整事件。
        notifyChildStateChange();
        deliverSignal(17);
    }

}  // namespace task
