/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/schedule.cc
 * 调度器主流程：选择下一个线程、睡眠与唤醒、线程终止与停稳（quiesce）。
 *
 * 文件职责
 * 每个 CPU 都有一个独立的运行队列（run
 * queue：按虚拟运行时间排序的就绪线程集合）。
 * 本文件负责：初始化该队列和空闲线程（idle
 * 线程：没有普通线程可运行时才执行的线程）、
 * 在时钟中断里记账并唤醒到期的睡眠线程、在 schedule() 中完成一次上下文切换
 * （context switch：保存当前线程现场、恢复下一个线程现场），以及睡眠、阻塞、
 * 唤醒、终止、停稳等状态转换的公共入口。
 *
 * 关键不变量
 * 1. 运行队列的修改都在 queue->getLock()
 * 保护下进行；真正切换内核栈之前必须解锁，
 *    因为解锁后旧栈上的代码随时可能被换出，不能有线程继承这把锁的所有权。
 * 2. 只有仍处于 Running
 * 的前一个线程才会被重新入队；Blocked、Sleeping、Stopped、 Terminated
 * 已经表达了它为什么不该继续运行，重复入队会破坏这些状态的含义。
 * 3. 一次切换分两步发布：先在队列锁内发布“选中的线程”（currentThread），
 *    等物理栈切换完成后才发布 executingTasks。停稳检查必须同时看这两个位置，
 *    才能覆盖“指针已换、栈还没换”的中间窗口。
 * 4. 旧内核栈不能由自己释放：schedule() 返回时仍踩在旧栈上，因此由下一个线程在
 *    finishContextSwitch() 里释放，且要等 IPC、cleanup 等引用消失之后。
 *
 * 建议阅读顺序
 * 先读 initScheduler() 了解每个 CPU 的初始状态，再读 schedule() 与
 * finishContextSwitch() 理解一次切换的完整生命周期；然后按
 * blockCurrentThread()、 wakeThread()、sleepCurrentThread()、schedulerTick()
 * 的顺序看状态机； terminateThread() 与 quiesceTask()
 * 说明“标记死亡”和“确认停稳”为什么必须分开。
 *
 * 术语速览
 * CFS（完全公平调度器）：按“虚拟运行时间”排队的调度算法，谁实际占用 CPU 越少，
 * 虚拟运行时间越小，就越先被选中。时间片（time
 * slice）：一个线程连续运行的上限，
 * 用完就请求重新调度。抢占（preemption）：在中断返回等安全点强制换下当前线程。
 */

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace task {

    static ::Atomic<bool> schedulerInitialized{false};
    // 每个 CPU 的“交接记录”，生命周期长于旧内核栈。
    // 选中下一个线程后，cpu 的当前任务指针可能在 switchTo
    // 还没返回时就已经指向新任务； 因此 executingTasks
    // 只在物理栈/上下文切换真正完成后才发布，供停稳检查使用。
    static Thread* retiredStacks[64]{};
    static Task* executingTasks[64]{};

    /*
     * 空闲线程入口：停机（hlt）时保持中断开启，这样时钟中断和远程重调度中断还能到达，
     * 让新变成可运行的线程被看见，而不必靠忙轮询。
     */
    static void idleEntry(void*) {
        while (true) {
            asm volatile("sti; hlt");
        }
    }

    /*
     * 时钟中断处理：只在这里记账（schedulerTick），
     * 是否以及何时真正切换，由中断返回/抢占路径决定——那里才是安全的切换点。
     */
    static void timerHandler(cpu::Context*) { schedulerTick(); }

    /*
     * 初始化当前 CPU 的调度器。
     * 做什么：分配并清空本地运行队列，把启动线程设为当前线程并以它的虚拟运行时间
     * 作为队列下限，随后建立 idle 线程、注册 32 号中断为每 1000
     * 微秒一次的时钟中断。 为什么先建队列再发布 initialized：其它 CPU
     * 看到标志后会直接使用调度设施， 必须保证队列已经就绪；每个 CPU
     * 仍需各自检查自己的本地队列。 idle 线程创建时不入队，并固定到所属
     * CPU，只作为“没有普通线程可运行”时的兜底。
     * 不能先入队再摘除：普通创建路径可能把它送到别的 CPU，摘除前就已开始执行，
     * 本 CPU 随后再选它就会让两个 CPU 共用同一个线程和内核栈。
     * AP（应用处理器，即非启动 CPU）通过同一入口初始化自己的队列和本地定时器。
     * 失败路径：堆分配失败时打印错误并停机，因为此时还没有可运行的调度器。
     * 前提：在调度可用之前由每个 CPU 的启动路径调用；不需要外部持锁。
     */
    void initScheduler() {
        ::cpu::LocalData* cpu = cpu::getCurrent();
        RunQueue* queue =
            static_cast<RunQueue*>(mm::allocateHeap(sizeof(RunQueue)));

        if (queue == nullptr) {
            device::serialWrite("[SCHED] unable to allocate run queue\n");
            cpu::halt();
        }

        queue->clear();
        queue->setCurrentThread(cpu->getCurrentThread());
        queue->setMinVruntime(cpu->getCurrentThread()->getVruntime());
        queue->setClock(time::getMonotonicTime());
        cpu->setRunQueue(queue);
        __atomic_store_n(&executingTasks[cpu->getCpuId()],
                         cpu->getCurrentTask(), __ATOMIC_RELEASE);

        // 所有必要调度数据准备完成后再发布初始化状态，时钟路径据此决定能否开始记账。
        schedulerInitialized.store(true, MemoryOrder::Release);

        // 空闲线程属于本
        // CPU，不能经过普通线程的负载均衡或短暂出现在任何运行队列中。
        // 直接创建未入队线程，避免别的 CPU 在这里配置完成前就开始使用它的栈。
        Thread* idle = Thread::create(nullptr, idleEntry, nullptr,
                                      ThreadType::Kernel, false);

        if (idle == nullptr) {
            device::serialWrite(
                "[SCHED] unable to allocate per-CPU idle thread\n");
            cpu::halt();
        }

        idle->setCpuAffinity(1ULL << cpu->getCpuId());
        queue->setIdleThread(idle);
        cpu->setIdleThread(idle);

        cpu::registerHandler(32, timerHandler);
        cpu::initTimer(32, 1000);
        device::serialWrite("[SCHED] per-CPU CFS run queue initialized\n");
    }

    /*
     * 用 acquire 语义读取“调度器已初始化”标志。
     * 调用方在首次使用调度设施前应先检查；但每个 CPU
     * 仍要单独确认本地队列是否存在 （例如 AP 可能尚未完成初始化）。acquire
     * 保证看到标志后，初始化写入的队列内容可见。
     */
    bool isSchedulerInitialized() {
        return schedulerInitialized.load(MemoryOrder::Acquire);
    }

    /*
     * 在当前 CPU 上选择一个线程并切换到它（调度器主入口）。
     * 做什么：
     *   1) 关中断并锁住本 CPU 的运行队列；
     *   2) 若前一个线程仍是 Running，把它改回 Ready 并重新入队；
     *   3) 取虚拟运行时间最小的线程（红黑树最左节点），为空则选 idle 线程；
     *   4) 在锁内把选中线程置为 Running、清零时间片、绑定
     * CPU，并发布为当前线程； 5) 解锁后再切内核栈与地址空间，最后调用
     * finishContextSwitch() 收尾。
     * 为什么：选线程和改状态必须在同一把锁内完成，否则远程唤醒或终止会看到
     * “已选中但状态不对”的中间态；而切栈前必须解锁，因为旧栈马上就不能再执行，
     * 不能让任何新上下文继承这把锁。
     * 前提：可在普通内核上下文或中断返回路径调用；无需调用者持锁，函数内部关中断，
     * 返回前恢复原中断状态。
     * 边界：调度器未初始化时直接返回；没有可运行线程时落到 idle，CPU
     * 不会空转退出。 配合：wakeThread()
     * 负责把线程放回队列并请求重调度，schedulerTick() 负责记账，
     * finishContextSwitch() 负责释放旧栈并发布执行状态。
     */
    void schedule() {
        if (!task::isSchedulerInitialized()) {
            return;
        }

        const uint64_t flags     = cpu::disableInterrupts();
        ::cpu::LocalData* cpu = cpu::getCurrent();
        RunQueue* queue          = cpu->getRunQueue();
        queue->getLock().lock();
        cpu->setRescheduleRequested(false);
        Thread* previous = cpu->getCurrentThread();

        if (previous != nullptr && previous != queue->getIdleThread() &&
            previous->getState() == ThreadState::Running) {
            previous->setState(ThreadState::Ready);
            queue->insertNode(previous);
        }

        Thread* next = queue->getLeftmost();

        if (next != nullptr) {
            queue->removeNode(next);
        } else {
            next = queue->getIdleThread();
        }

        // 在“移除队列节点”的同一把锁内发布选中结果并重置时间片，
        // 这样远程唤醒/终止看到的永远是自洽的“选中线程 + 状态”组合。
        next->setState(ThreadState::Running);
        next->setSliceRuntime(0);
        next->setCpuId(cpu->getCpuId());
        cpu->setCurrentThread(next);
        cpu->setCurrentTask(next->getTask());
        queue->setCurrentThread(next);
        queue->getLock().unlock();

        if (next != previous) {
            if (previous->getState() == ThreadState::Terminated) {
                retiredStacks[cpu->getCpuId()] = previous;
            }

            cpu::getCurrent()->setKernelStack(next->getKernelStackTop());
            // 进入目标线程保存的内核续点之前，先装好它的页表。
            // 没有所属任务的 worker 线程共用内核页表根；换页表根（CR3）后，
            // TLB（页表项缓存）中属于旧地址空间的条目需要失效，否则可能读到错误映射。
            // 旧内核栈仍然有效，直到 finishContextSwitch()
            // 确认切换完成才可能被释放。
            mm::switchAddressSpace(
                next->getTask() == nullptr
                    ? mm::getKernelAddressSpaceRoot()
                    : next->getTask()->addressSpace().rootPhysicalAddress());
            previous->switchTo(next);
            finishContextSwitch();
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 完成“不能在退役栈上做”的收尾工作。
     * 做什么：在关中断下取出本 CPU 的退役线程；若它没有活跃 IPC 调用、没有
     * cleanup、 且内核栈存在，就释放内核栈，并让所属任务回收该线程的 join
     * 记录； 最后把 executingTasks 发布为本 CPU 当前任务。
     * 为什么不能更早：释放栈时可能仍有指针指向它（例如被取消的同步调用还留着
     * 指向该栈的回复地址），所以必须等这些引用消失；而 executingTasks
     * 最后发布，
     * 是为了补上“已选中但尚未真正执行”的窗口，停稳检查靠它判断对方是否还在跑这个任务。
     * 调用者：switchTo() 返回后的 schedule()，以及新线程第一次运行的
     * Thread::bootstrap()。
     * 前提：已在新线程的内核栈上执行；函数内部短暂关中断以保护退役栈记录。
     */
    void finishContextSwitch() {
        // 前一个 CPU 上下文已经不在这个栈上执行了。
        const uint64_t flags = cpu::disableInterrupts();
        Thread*& retired     = retiredStacks[cpu::getCurrentId()];
        Thread* thread       = retired;
        retired              = nullptr;
        // 被取消的同步调用仍可能收到指向该栈的回复；清理钩子（cleanup）也可能
        // 持有栈上对象的指针，所以这两种引用消失前都不能释放栈。
        if (thread && !thread->hasActiveIpcCall() && !thread->hasCleanup() &&
            thread->getKernelStack()) {
            mm::freeHeap(thread->getKernelStack());
            thread->setKernelStack(nullptr);
            thread->setKernelStackTop(nullptr);

            if (thread->getTask()) {
                thread->getTask()->retireJoinedThread(thread);
            }
        }

        __atomic_store_n(&executingTasks[cpu::getCurrentId()],
                         currentThread()->getTask(), __ATOMIC_RELEASE);
        cpu::restoreInterrupts(flags);
    }

    /*
     * 终止一个线程：从队列/睡眠表中摘除，并在其所属 CPU 的队列锁内置为
     * Terminated。 为什么分两步：置为 Terminated
     * 只表示“不再运行”；如果它正在别的 CPU 上执行，
     * 它的内核栈仍在使用中。这里只请求远程重调度，调用者之后还必须用
     * quiesceTask() 确认它真的停稳，才能释放地址空间等资源。
     * 前提：调用者不得持有该线程的队列锁；函数按 thread->getCpuId()
     * 找目标队列， 并在锁内完成摘链与状态修改。 失败/边界：thread
     * 为空直接返回；目标 CPU 或队列不存在（例如尚未初始化）时， 仍把状态置为
     * Terminated，只是不再做摘链操作。 与其它模块：睡眠链表通过 waitNext
     * 串起，摘链时必须同时清空 waitNext。
     */
    void terminateThread(Thread* thread) {
        if (!thread) {
            return;
        }

        ::cpu::LocalData* target = cpu::getLocalData(thread->getCpuId());

        if (target && target->getRunQueue()) {
            InterruptSpinLockGuard guard(target->getRunQueue()->getLock());

            // 已排队的线程先从运行树摘除，防止别的 CPU
            // 在终止状态发布前再次选中它。
            if (thread->getInRunQueue()) {
                target->getRunQueue()->removeNode(thread);
            }

            // 睡眠线程还挂在按时间排序的睡眠链，终止也必须把它摘下，不能等到超时后重新唤醒。
            Thread** link = &target->getRunQueue()->accessSleepingThreads();

            while (*link && *link != thread) {
                link = &(*link)->accessWaitNext();
            }

            if (*link) {
                *link = thread->getWaitNext();
                thread->setWaitNext(nullptr);
            }

            thread->setState(ThreadState::Terminated);
        } else {
            thread->setState(ThreadState::Terminated);
        }

        // 改成终止状态并不保证远端立即停下，通知所属 CPU
        // 尽快切走；栈的真正释放另等停稳确认。
        cpu::requestRemoteReschedule(thread->getCpuId());
    }

    /*
     * 停稳（quiesce）指定任务：等待其它所有 CPU 都不再“选中”或“正在执行”它。
     * 为什么要两步：schedule() 先发布 currentThread，再切栈，最后才发布
     * executingTasks；只查其中一个会漏掉“指针已换、栈未换”的窗口。
     * 做什么：轮询每个其它 CPU，在它的队列锁下检查 currentThread->getTask() 和
     * executingTasks[id]；命中就给该 CPU 发远程重调度，让它尽快切走，并用 pause
     * 指令降低自旋对总线的压力。
     * 前提：进入时不能持有任务、地址空间或运行队列锁——远程 CPU
     * 推进正需要这些锁； 函数会先保存并重新开启本 CPU
     * 的中断，退出时恢复原状态。
     * 输出：无；返回即表示任务已停稳，可以安全释放其资源。
     * 注意：这是主动等待，可能持续到目标 CPU 完成一次上下文切换。
     */
    void quiesceTask(Task* task) {
        // 这里不允许持有任务、地址空间或运行队列锁。
        // “选中”的上下文与已完成的切栈合起来覆盖 switchTo 的两侧。
        const uint64_t flags = cpu::disableInterrupts();
        cpu::enableInterrupts();

        while (true) {
            bool busy = false;

            for (uint32_t id = 0; id < cpu::getDiscoveredCpuCount(); ++id) {
                if (id == cpu::getCurrentId()) {
                    continue;
                }

                ::cpu::LocalData* target = cpu::getLocalData(id);

                if (!target || !target->getRunQueue()) {
                    continue;
                }

                InterruptSpinLockGuard guard(target->getRunQueue()->getLock());
                Thread* selected = target->getCurrentThread();
                Task* executing =
                    __atomic_load_n(&executingTasks[id], __ATOMIC_ACQUIRE);

                if ((selected && selected->getTask() == task) ||
                    executing == task) {
                    busy = true;
                    cpu::requestRemoteReschedule(id);
                }
            }

            if (!busy) {
                break;
            }

            asm volatile("pause" ::: "memory");
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 当前线程主动让出 CPU：先请求重调度，再立即调用 schedule()。
     * schedule() 会把它重新插回队列（状态仍是
     * Running），因此它仍可能被立刻选中；
     * 若期间状态被别的路径改掉，则不会重复入队。
     */
    void yieldThread() {
        task::requestReschedule();
        task::schedule();
    }

    /*
     * 在把当前线程挂进条件变量的等待队列之前，先清掉过期的唤醒信用（wakePending）。
     * wakePending 表示“已经有人唤醒过你”：生产者在等待者真正阻塞之前就调用
     * wakeThread() 时，用这个标记把唤醒保存下来，避免唤醒丢失。
     * 前提：调用者必须持有条件变量自己的锁，并且该锁要覆盖“检查条件 +
     * 登记等待”两步， 否则生产者可能恰好从中间插入。
     * 这里额外取运行队列锁，是为了与 wakeThread() 的写操作串行化。
     */
    void prepareCurrentWait() {
        RunQueue* queue = cpu::getCurrent()->getRunQueue();
        InterruptSpinLockGuard guard(queue->getLock());
        currentThread()->accessWakePending() = false;
    }

    /*
     * 把当前线程阻塞，直到有人唤醒它。
     * 做什么：在运行队列锁内读取并清除 wakePending；若没有待处理的唤醒，才置为
     * Blocked，并在解锁后调用 schedule() 让出 CPU。
     * 为什么这样做：等待者可能在到达这里之前就被抢占（状态是
     * Ready），此时生产者
     * 已经发过唤醒；如果直接阻塞就会丢掉这次通知，所以先消费 pending 再决定。
     * 前提：调用者应先调用 prepareCurrentWait() 完成登记；函数内部关中断。
     */
    void blockCurrentThread() {
        const uint64_t flags = cpu::disableInterrupts();
        RunQueue* queue      = cpu::getCurrent()->getRunQueue();
        Thread* thread       = currentThread();
        queue->getLock().lock();
        // 待处理唤醒与状态转换必须和 wakeThread() 用同一把锁：
        // 等待者可能在到达这里之前就被抢占（处于 Ready），此时唤醒已经发生。
        const bool pending          = thread->accessWakePending();
        thread->accessWakePending() = false;

        if (!pending) {
            thread->setState(ThreadState::Blocked);
        }

        queue->getLock().unlock();

        if (!pending) {
            schedule();
        }

        cpu::restoreInterrupts(flags);
    }

    /*
     * 唤醒一个线程，使其重新具备运行资格。
     * 为什么要按状态分支：不同状态下“唤醒”的含义不同，不能一律入队。
     *   - Running/Ready：它已经能运行，只置
     * wakePending，防止它随后阻塞时丢掉通知；
     *   - Stopped：作业控制（job control）暂停中的线程不能被唤醒绕过，只记录
     *     暂停结束后应恢复成 Ready，并置 wakePending；
     *   - Blocked/Sleeping：真正让它变成可运行——从睡眠表摘除、把虚拟运行时间
     *     抬到队列下限（避免靠睡眠积攒调度优势）、必要时插入运行队列，并请求重调度。
     * 前提：与 blockCurrentThread() 用目标 CPU
     * 的同一把队列锁串行化；函数内部关中断。 边界：thread 为空、目标 CPU
     * 或队列不存在时直接返回，不做任何状态修改。 配合：若目标线程在别的 CPU
     * 上，最后补发远程重调度，让对方尽快看到新状态。
     */
    void wakeThread(Thread* thread) {
        if (thread == nullptr) {
            return;
        }

        ::cpu::LocalData* target = cpu::getLocalData(thread->getCpuId());

        if (target == nullptr || target->getRunQueue() == nullptr) {
            return;
        }

        RunQueue* queue      = target->getRunQueue();
        const uint64_t flags = cpu::disableInterrupts();
        queue->getLock().lock();
        const ThreadState state = thread->getState();
        bool runnable           = false;

        // 已经可运行的线程只记录通知，避免重复插入运行树；通知也能阻止它紧接着误睡下。
        if (state == ThreadState::Running || state == ThreadState::Ready) {
            thread->accessWakePending() = true;
        } else if (state == ThreadState::Stopped) {
            // 暂停状态仍须保留，只记下解除暂停后应变为就绪，普通唤醒不能绕过作业控制。
            thread->setStateBeforeStop(ThreadState::Ready);
            thread->accessWakePending() = true;
        } else if (state == ThreadState::Blocked ||
                   state == ThreadState::Sleeping) {
            // 被提前唤醒的睡眠线程先从定时链摘除，避免时钟到期再处理同一节点。
            if (state == ThreadState::Sleeping) {
                Thread** link = &queue->accessSleepingThreads();

                while (*link != nullptr && *link != thread) {
                    link = &(*link)->accessWaitNext();
                }

                if (*link == thread) {
                    *link = thread->getWaitNext();
                }

                thread->setWaitNext(nullptr);
            }

            thread->setState(ThreadState::Ready);

            if (thread->getVruntime() < queue->getMinVruntime()) {
                thread->setVruntime(queue->getMinVruntime());
            }

            if (!thread->getInRunQueue()) {
                queue->insertNode(thread);
            }

            target->setRescheduleRequested(true);
            runnable = true;
        }

        queue->getLock().unlock();
        cpu::restoreInterrupts(flags);

        // 是否能入队在持锁时确定，通知目标 CPU 则放在解锁后执行。
        if (runnable && thread->getCpuId() != cpu::getCurrentId()) {
            cpu::requestRemoteReschedule(thread->getCpuId());
        }
    }

    /*
     * 只在本 CPU 上打一个“需要重新调度”的标记，不强制在当前上下文立即切换。
     * 真正的切换发生在安全点（例如中断返回或显式调用 schedule() 时）。
     */
    void requestReschedule() {
        cpu::getCurrent()->setRescheduleRequested(true);
    }

    /*
     * 每次时钟中断调用：按真实流逝时间记账，并唤醒到期的睡眠线程。
     * 为什么用时间差：中断可能被延迟或合并，不能假设每次中断都固定过了 1 ms；
     * 这里用单调时钟的实际差值记账，时间片和虚拟运行时间才准确。
     * 做什么：在队列锁内累加 elapsed 并调用
     * accountTick()（若时间片用完，或队列里
     * 出现虚拟运行时间更小的线程，就请求重调度）；然后从按唤醒时间排序的睡眠表中
     * 依次取出所有已到期的线程：Stopped 只记录暂停结束后的目标状态，Terminated
     * 直接 丢弃，其余置为 Ready、插入运行队列并请求重调度。 为什么最后才处理
     * IPC：Endpoint::expireWaits() 可能反过来唤醒线程、再次获取队列锁，
     * 所以必须在释放运行队列锁之后调用，避免持锁时重入。
     * 前提：中断上下文调用（timerHandler），本 CPU
     * 队列已初始化；未初始化则直接返回。 配合：wakeThread()
     * 也可唤醒线程，但定时唤醒只由这里按截止时间驱动。
     */
    void schedulerTick() {
        ::cpu::LocalData* cpu = cpu::getCurrent();

        if (!task::isSchedulerInitialized() || cpu->getRunQueue() == nullptr) {
            return;
        }

        RunQueue* queue = cpu->getRunQueue();
        queue->getLock().lock();
        const uint64_t now = time::getMonotonicTime();

        // 用本次单调时间减上次记账时刻，即使中断延迟也按真实流逝时间计算占用。
        const uint64_t elapsed = now - queue->getClock();
        queue->setClock(now);
        cpu->addTickCount(1);

        if (queue->accountTick(cpu->getCurrentThread(), elapsed)) {
            cpu->setRescheduleRequested(true);
        }

        // 睡眠链已经按截止时间排序，只需处理连续到期的表头，不必每次扫描所有睡眠线程。
        while (queue->getSleepingThreads() != nullptr &&
               time::deadlineReached(
                   now,
                   queue->getSleepingThreads()->getWakeDeadlineNanoseconds())) {
            Thread* thread = queue->getSleepingThreads();
            queue->setSleepingThreads(thread->getWaitNext());
            thread->setWaitNext(nullptr);

            // 暂停线程到期也不能立刻运行，只更新解除暂停后的目标状态。
            if (thread->getState() == ThreadState::Stopped) {
                thread->setStateBeforeStop(ThreadState::Ready);
                continue;
            }

            if (thread->getState() == ThreadState::Terminated) {
                continue;
            }

            thread->setState(ThreadState::Ready);
            queue->insertNode(thread);
            cpu->setRescheduleRequested(true);
        }

        queue->getLock().unlock();

        // IPC
        // 超时可能再次唤醒线程并取得运行队列锁，所以必须等上面的队列锁释放再处理。
        Endpoint::expireWaits(now);
        expireUserAddressWaits(now);
    }

    /*
     * 让当前线程睡眠指定毫秒数；0 毫秒表示只让出一次 CPU。
     * 做什么：非 0 时关中断、取队列锁，把状态置为 Sleeping，计算唤醒截止时间，
     * 按截止时间升序插入本地睡眠表，然后解锁并调用 schedule() 切走。
     * 为什么排序：schedulerTick()
     * 只需从表头依次弹出到期项，不必每次扫描整张表。 为什么做饱和加法：duration
     * + now 可能溢出成很小的值，导致“刚睡就醒”， 所以溢出时钳到最大时间值。
     * 前提：在普通内核上下文中调用；函数内部关中断，返回时恢复。
     * 返回：被唤醒后从 schedule() 返回；此时状态已由 wakeThread() 或
     * schedulerTick() 改为 Ready，重新获得被调度的资格。
     */
    void sleepCurrentThread(uint64_t milliseconds) {
        if (milliseconds == 0) {
            task::yieldThread();

            return;
        }

        const uint64_t flags = cpu::disableInterrupts();
        RunQueue* queue      = cpu::getCurrent()->getRunQueue();
        Thread* current      = currentThread();
        queue->getLock().lock();
        const uint64_t duration = time::milliseconds2Nanoseconds(milliseconds);
        const uint64_t now      = time::getMonotonicTime();
        current->setState(ThreadState::Sleeping);

        // 计算截止时间时做饱和加法，超大休眠时长不能溢出成一个已经过去的时刻。
        current->setWakeDeadlineNanoseconds(
            duration > ~0ULL - now ? ~0ULL : now + duration);

        // 找到首个不早于自己的截止时间，把线程插在它前面，使时钟唤醒只需查看表头。
        Thread** link = &queue->accessSleepingThreads();

        while (*link != nullptr &&
               !time::deadlineReached((*link)->getWakeDeadlineNanoseconds(),
                                      current->getWakeDeadlineNanoseconds())) {
            link = &(*link)->accessWaitNext();
        }

        current->setWaitNext(*link);
        *link = current;
        queue->getLock().unlock();

        // 状态和睡眠链都登记完成、队列锁也已释放以后再切走，避免切走后没人知道何时唤醒它。
        task::schedule();
        cpu::restoreInterrupts(flags);
    }

}  // namespace task
