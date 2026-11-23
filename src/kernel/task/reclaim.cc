/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/reclaim.cc
 *
 * 本文件实现“延迟回收”：任务在逻辑上结束后，并不立刻释放资源，
 * 而是把它放进一个由引用计数保护的工作队列，由专门的回收器线程
 * 在安全时机完成真正的资源释放。
 *
 * 为什么要延迟：任务状态变成 Terminated 只代表“不再运行新代码”，
 * 不代表所有 CPU 都已经离开它的内核栈和页表。如果马上释放内存，
 * 还在上面运行的 CPU 会崩溃。因此资源回收分成两个阶段：
 *   第一阶段：标记任务结束、停止线程、禁止新线程加入；
 *   第二阶段：确认没有 CPU 还在执行该任务后，释放句柄、内存、页表。
 *
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace task {

    /*
     * 创建回收器专用的工作线程。
     * 为什么单独建线程：回收工作不能依赖“请求别人死亡”的服务或线程
     * 还能活多久；即使它们先退出，回收器仍要把清理做完。
     * 第一个参数传 nullptr，表示这个线程不属于任何任务（无所有者），
     * 它只负责执行下面的 run 循环。
     *
     * 输入：无。输出：无。调用前提：在任务系统初始化阶段调用一次。
     * 失败路径：线程创建失败时打印错误并停机（halt）。内核没有回收器
     * 就无法安全结束任务，继续运行只会让资源一直泄漏。
     */
    void TaskReclaimer::init() {
        worker_ = Thread::create(
            nullptr,
            [](void* context) { static_cast<TaskReclaimer*>(context)->run(); },
            this);

        if (!worker_) {
            device::serialWrite("[TASK] unable to init task reclaimer\n");
            cpu::halt();
        }
    }

    /*
     * 把一个任务提交到回收队列。
     *
     * 队列锁同时保护“重复提交判断”和“队头/队尾指针更新”，所以检查与
     * 入队是一个原子步骤：不会有两个提交者同时把同一任务挂两次。
     *
     * 引用计数的顺序很重要：先 retain 增加一份引用，再把
     * resourceState_ 发布为 1。这样正在 destroy 中等待的调用者即使
     * 立刻死亡，清理义务仍由队列持有的引用承担，不会丢失。
     *
     * 输入：task，要回收的任务。
     * 调用前提：函数自己拿队列锁。
     * 失败路径：如果任务状态已经不是 0（已入队或已完成），直接返回，
     * 不会重复入队。成功后唤醒回收器线程。
     */
    void TaskReclaimer::submit(Task& task) {
        InterruptSpinLockGuard guard(lock_);

        // 回收队列锁内检查是否已经排队，重复提交不能再次保留引用或清理同一任务。
        if (__atomic_load_n(&task.resourceState_, __ATOMIC_ACQUIRE) != 0) {
            return;
        }

        // 队列持有自己的任务引用，提交者后来退出也不会让待回收节点失效。
        task.retain();
        __atomic_store_n(&task.resourceState_, 1U, __ATOMIC_RELEASE);
        task.reclaimNext_ = nullptr;

        if (tail_) {
            tail_->reclaimNext_ = &task;
        } else {
            head_ = &task;
        }

        tail_ = &task;
        wakeThread(worker_);
    }

    /*
     * 回收器的主循环：取任务、释放资源、发布完成状态、通知父进程。
     *
     * 为什么在锁内只做“摘链”：真正的资源释放可能很慢（要等其它 CPU、
     * 写 core dump），不能一直占着队列锁。所以锁内弹出，锁外回收。
     *
     * 空队列时先 prepareCurrentWait 再阻塞：这个“准备睡眠”的动作也在
     * 队列锁内完成，防止 submit 在“检查为空”和“真正睡下”之间插入，
     * 造成丢失唤醒（明明有任务却睡死过去）。
     *
     * resourceState_ 用 release 语义写成 2，让在 destroy 中自旋等待的
     * 调用者能可靠看到“资源已释放”。队列持有的引用要等父进程通知也
     * 做完才 release，避免对象在通知途中被释放。
     *
     * 为什么通知父进程：子进程退出后，父进程可能正阻塞在 wait 上，
     * 需要被唤醒并拿到退出状态（SIGCHLD 事件）。如果 core dump 还在
     * 等待，就先不通知，等 core dump 完成后由那条路径负责通知，
     * 避免父进程过早看到不完整的状态。
     * exited 为 false 表示任务从未真正启动，不算一次可观察的进程退出，
     * 不需要通知父进程。
     *
     * 输入/输出：无。循环永不退出（回收器是常驻线程）。
     */
    void TaskReclaimer::run() {
        while (true) {
            // 每轮先从公共队列取出一个任务，实际停稳和资源回收在队列锁外完成。
            Task* task;
            {
                InterruptSpinLockGuard guard(lock_);
                task = head_;

                if (task) {
                    head_ = task->reclaimNext_;

                    if (!head_) {
                        tail_ = nullptr;
                    }

                    task->reclaimNext_ = nullptr;
                } else {
                    prepareCurrentWait();
                }
            }

            // 没有工作时进入等待，提交方会唤醒后台线程，不需要持续空转轮询。
            if (!task) {
                blockWaitingThread();
                continue;
            }

            const bool exited = task->destroyResources();
            __atomic_store_n(&task->resourceState_, 2U, __ATOMIC_RELEASE);
            // 请求终止任务的线程可能还没来得及通知父进程就消失了，
            // 所以由回收器在这里补上通知。
            if (exited && !coreDump().pending(*task)) {
                ::task::TaskReference parent = task->parentReference();

                if (parent) {
                    parent->notifyChildStateChange();
                    parent->notifyChildExit(*task);
                }
            }

            task->release();
        }
    }

    /*
     * 全局唯一的回收队列实例。
     * 函数内静态变量在第一次调用时构造；所有任务共享同一个队列和同一个
     * 回收器线程。
     */
    TaskReclaimer& taskReclaimer() {
        static TaskReclaimer instance;

        return instance;
    }

    /*
     * 由回收器线程调用的资源释放函数。
     *
     * 前提：只能由回收器线程调用，此时任务已经不会再运行新的用户代码。
     * 返回：true 表示这是一次可观察的进程退出，需要通知父进程；
     *       false 表示任务在创建/启动阶段就失败、从未真正运行过，
     *       没有僵尸状态可等待，直接丢弃即可。
     *
     * 两阶段资源回收在这里体现为：先在锁内把任务标成 Terminated 并终止
     * 所有线程（逻辑结束），等确认没有 CPU 还在用它之后，再拆内存和
     * 页表（物理释放）。
     */
    bool Task::destroyResources() {
        uint64_t root;
        bool discard;
        // 第一步：在任务锁内完成逻辑终止。
        // - discard：任务还停在 Created 且从未有过线程，说明创建或启动
        //   失败，它没有可被 wait 的僵尸状态，走直接丢弃路径。
        // - 关闭 acceptsThreads_，阻止新线程再加入。
        // - 把还活着的线程全部终止，并把 threadCount_ 清零。
        // - 状态改为 Terminated。
        {
            InterruptSpinLockGuard guard(lock_);
            discard         = state_ == TaskState::Created && threadCount_ == 0;
            acceptsThreads_ = false;

            for (Thread* thread = threads_; thread != nullptr;
                 thread         = thread->getTaskNext()) {
                if (thread->getState() != ThreadState::Terminated) {
                    task::terminateThread(thread);
                }
            }

            threadCount_ = 0;
            state_       = TaskState::Terminated;
        }

        // 第二步：等待所有 CPU 离开这个任务。第一步的任务锁到这里已经释放。
        // 为什么 Terminated 还不够：状态只是逻辑标记，别的 CPU 可能正好
        // 停在旧内核栈上还没切换走。quiesceTask 会等这些切换完成；
        // 在它返回之前，绝对不能销毁页表根、映射或基于栈的 IPC 对象。
        task::quiesceTask(this);
        // 通知设备资源域：该任务作为所有者正在退出，由它们完成与硬件
        // 资源相关的收尾和登记。
        device::resourceDomains().ownerExiting(*this);
        // 第三步：在页表还完整时抓取 core dump（核心转储，进程崩溃时
        // 保存内存映像供调试）。快照使用独立缓冲区，所以任务拆完之后
        // 仍可以慢慢写文件，不会访问已经释放的内存。
        coreDump().capture(*this);
        // 第四步：在地址空间锁内把页表根摘下来（替换为 0），先保存旧根，
        // 等线程清理完再统一销毁。
        {
            PreemptionSpinLockGuard guard(addressSpace_.lock());
            root = addressSpace_.replaceRoot(0);
        }

        // 第五步：先撤销端点接收者所有权，再销毁句柄。
        // 顺序原因：可恢复的端点需要保留自己的身份，交给替代的服务实例
        // 继续使用；如果先销毁句柄，端点身份可能就丢了。
        Endpoint::closeOwner(this);
        // 第六步：清理每个线程的 IPC 等待和临时资源。
        // 必须在释放内核栈之前完成，因为这些等待和清理登记可能就放在
        // 栈上；先释放栈会造成悬空引用。
        for (Thread* thread = threads_; thread;
             thread         = thread->getTaskNext()) {
            Endpoint::cancelThread(thread);
            thread->cancelWait();
            ThreadCleanup::releaseAll(*thread);
        }

        // 第七步：收养子进程（adoption）。
        // 收养：父进程退出后，它的子进程不能没有父进程来等待并收取退出
        // 状态，内核会把它们转交给 init 或内核任务。必须在丢弃本任务的
        // 资源之前转移子进程所有权；子进程持有的父引用可以独立让 Task
        // 对象存活，与旧内存无关。
        adoptChildren();
        // 第八步：释放线程的内核栈和 Thread 对象本身。
        // 每个线程先记住 next，再释放，避免释放后还去读 next 指针。
        for (Thread* thread = threads_; thread;) {
            Thread* next = thread->getTaskNext();

            if (thread->getKernelStack()) {
                mm::freeHeap(thread->getKernelStack());
            }

            mm::freeHeap(thread);
            thread = next;
        }

        // 第九步：清空线程链表和主线程指针，销毁句柄表，解除内存映射，
        // 最后销毁页表根。mm::maps() 和共享内存模块分别登记过属于本任务
        // 的映射，必须逐一撤销，避免留下指向已消失地址空间的悬空记录。
        threads_    = nullptr;
        mainThread_ = nullptr;
        handles_.dispose();
        mm::maps().releaseTask(*this);
        ::mm::sharedMemory().releaseTaskMappings(*this);

        if (root != 0) {
            mm::destroyAddressSpaceRoot(root);
        }

        device::resourceDomains().addressSpaceReleased(*this);
        // 第十步：处理从未启动的任务。
        // Created 且没有线程的任务没有可等待的僵尸状态，直接把它从父进程
        // 的子进程链表和全局任务链表中摘掉。普通退出任务不摘链：它要保持
        // 可见（作为僵尸进程），直到父进程用 wait 取走退出状态。
        // 这里提前放掉创建时持有的那份引用；队列自己的引用仍由 run()
        // 在最后统一释放。
        if (discard) {
            if (parent_ != nullptr) {
                InterruptSpinLockGuard parentGuard(parent_->lock_);
                Task** link = &parent_->firstChild_;

                while (*link != nullptr && *link != this) {
                    link = &(*link)->nextSibling_;
                }

                if (*link == this) {
                    *link = nextSibling_;
                }

                nextSibling_ = nullptr;
            }

            unlinkGlobal();
        }

        if (discard) {
            release();
        }

        // 返回值告诉回收器：只有真正的进程退出才需要通知父进程。
        return !discard;
    }

}  // namespace task
