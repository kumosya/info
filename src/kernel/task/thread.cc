/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/thread.cc
 * 本文件负责三件事：创建线程、为线程准备“第一次运行”的栈帧、在 CPU 上
 * 切换执行上下文（context switch，即保存当前线程的寄存器现场并恢复下一
 * 个线程的现场）。
 */

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/protocol.h"

namespace task {

    // 编译期自检：CPU 上下文结构的大小必须正好等于 21 个 64 位寄存器。
    // 汇编里的保存/恢复代码按这个固定布局读写这块内存；一旦结构体被改动，
    // 这里会直接编译失败，而不是等到运行时悄悄踩坏栈。
    static_assert(sizeof(CpuContext) == 21 * sizeof(uint64_t));

    /*
     * 为一个新线程挑选一个负载较轻、并且允许它运行的 CPU。
     * 为什么需要：如果所有新线程都落在 0 号 CPU 上，其它处理器就会一直闲着。
     * 做法：从线程当前所在的 CPU 开始轮询，在“在线”且被 CPU 亲和性允许的
     * CPU 中，选出运行队列（run queue，每个 CPU 上等待运行的线程排成的队列）
     * 负载最小的一个。
     * 细节：亲和性（affinity）是一个位掩码，第 n 位为 1 表示允许在 n 号 CPU
     * 上运行；系统中 CPU 数量最多按 64 处理，因为掩码只有 64 位。
     * 适用范围：只在“创建线程”时挑选 CPU；线程被唤醒（wakeup，即把睡眠中的
     * 线程重新放回运行队列，让它有机会再次被调度）时仍然留在原来的 CPU 上，
     * 不会重新挑选。
     * 锁：读取每个队列的负载时要拿该队列自己的中断自旋锁，否则可能读到一半
     * 被别的 CPU 改掉（这种“读到的数据可能正在被并发修改”的情况就是竞态）。
     * 副作用：只修改线程的 CPU 编号，不负责把线程放进队列。
     */
    static void placeNewThread(Thread& thread) {
        const uint64_t preferred  = thread.getCpuId();
        const uint32_t discovered = cpu::getDiscoveredCpuCount();
        const uint32_t count      = discovered < 64 ? discovered : 64;
        uint64_t bestLoad         = UINT64_MAX;
        uint32_t best             = preferred;

        for (uint32_t n = 0; n < count && n < 64; ++n) {
            const uint64_t id                = (preferred + n) % count;
            ::cpu::CpuDescriptor* descriptor = cpu::getCpuDescriptor(id);

            if (!(thread.getCpuAffinity() & (1ULL << id)) || !descriptor ||
                descriptor->getState().load(MemoryOrder::Acquire) !=
                    cpu::CpuState::Online) {
                continue;
            }

            ::cpu::LocalData* local = descriptor->getLocalData();
            task::RunQueue* queue      = local ? local->getRunQueue() : nullptr;

            if (!queue) {
                continue;
            }

            InterruptSpinLockGuard guard(queue->getLock());
            const uint64_t load =
                queue->getRunnableCount() +
                (queue->getCurrentThread() &&
                         queue->getCurrentThread() != queue->getIdleThread()
                     ? 1
                     : 0);

            if (load < bestLoad) {
                bestLoad = load;
                best     = id;
            }
        }

        thread.setCpuId(best);
    }

    // 普通内核线程的内核栈大小：64 KiB。
    // 固定角色的服务（Sys 类型，或任务带有 serviceRole）改用更大的栈，原因
    // 见 Thread::create：它们的本地服务/加载器调用路径更深；这个调整不影响
    // 用户栈的大小。
    constexpr uint64_t kernelStackSize = 64 * 1024;

    /*
     * 把“当前已经在运行的引导上下文”包装成一个 Thread 对象，而不是另外分配
     * 一块新栈。
     * 为什么这样做：启动早期内核本来就在使用一个栈（当前 CPU 的引导栈），既
     * 没必要也没条件再切换一次；直接复用它，可以让调度器从第一天起就有
     * currentThread() 可用。
     * 前提：每个 CPU 在本地调度器初始化之前调用一次；必须在当前 CPU 上调用，
     * 因为它读取的是本 CPU 的栈指针和 CPU 编号。
     * 之后某个 Task 可以再挂到这个线程上（见 Task::attachThread）。
     * 失败路径：连 Thread 结构都分配不出来时，串口打印一行日志并停机——此时
     * 没有可用的线程抽象，内核无法继续。
     */
    void initThreads() {
        Thread* bootThread =
            static_cast<Thread*>(mm::allocateHeap(sizeof(Thread)));

        if (bootThread == nullptr) {
            device::serialWrite(
                "[THREAD] unable to allocate bootstrap thread\n");
            cpu::halt();
        }

        bootThread->clearStorage();
        bootThread->setId(task::allocateId());
        bootThread->setState(ThreadState::Running);
        bootThread->setType(ThreadType::Kernel);
        bootThread->setCpuAffinity(~0ULL);
        bootThread->setWeight(1024);
        bootThread->setTimeSlice(time::milliseconds2Nanoseconds(4));
        bootThread->setCpuId(cpu::getCurrentId());
        uint64_t stackPointer;
        asm volatile("movq %%rsp, %0" : "=r"(stackPointer));
        bootThread->setKernelStackTop(reinterpret_cast<void*>(stackPointer));

        ::cpu::LocalData* cpu = cpu::getCurrent();
        cpu->setCurrentThread(bootThread);
        cpu->setCurrentTask(nullptr);
        device::serialWrite("[THREAD] bootstrap thread initialized\n");
    }

    /*
     * 创建一个“还没挂到任务上”的线程：分配 Thread 结构和内核栈，构造一个与
     * switchThreadContext 弹栈顺序匹配的初始栈帧，最后挂到 task 上。
     * 参数：
     *   task        —— 所属任务；为空表示纯内核线程，不挂到任何进程。
     *   entryPoint  —— 线程入口函数（第一次运行时从这里开始执行）。
     *   argument    —— 传给入口函数的参数。
     *   type        —— 线程类型，决定内核栈大小（Sys 或服务任务用 256 KiB）。
     *   makeRunnable—— 为 true 时在返回前就把线程放进运行队列；用户线程创建
     *                  路径会传 false，等用户态字段填好后再自己入队。
     * 返回：成功返回线程指针；失败返回 nullptr。
     * 失败路径：入口为空、内存分配失败、attachThread 被拒绝，都会返回 nullptr，
     * 并且已经分配的资源会被释放，不会泄漏。
     * 生命周期：attach 成功之后，Thread 的生命周期转交给 Task，由 join 与栈
     * 退役流程负责最终释放（见 wait.cc）。
     * 锁/中断：本函数本身不持任务锁；内部 attachThread 会短暂获取任务锁。
     */
    Thread* Thread::create(Task* task, ThreadEntry entryPoint, void* argument,
                           ThreadType type, bool makeRunnable) {
        if (entryPoint == nullptr) {
            return nullptr;
        }

        Thread* thread = static_cast<Thread*>(mm::allocateHeap(sizeof(Thread)));

        // 服务线程可能在深调用链上处理大消息，使用较大的内核栈；普通线程使用默认栈大小。
        const uint64_t stackSize =
            type == ThreadType::Sys ||
                    (task && task->serviceRole() != services::ServiceRole::None)
                ? 256 * 1024
                : kernelStackSize;
        uint8_t* stack = static_cast<uint8_t*>(mm::allocateHeap(stackSize));

        if (thread == nullptr || stack == nullptr) {
            if (thread) {
                mm::freeHeap(thread);
            }

            if (stack) {
                mm::freeHeap(stack);
            }

            return nullptr;
        }

        // 两份存储都准备成功后再填初始现场，确保任何失败都能按原始分配指针释放。
        thread->clearStorage();
        thread->setId(task::allocateId());
        thread->setState(ThreadState::Created);
        thread->setType(type);
        thread->setKernelStack(stack);
        thread->setKernelStackTop(stack + stackSize);

        // 预先摆出与切换汇编弹栈顺序一致的现场，第一次被选中时会进入 bootstrap
        // 而不是返回随机地址。
        thread->getContext().setStackPointer(reinterpret_cast<uint64_t>(
            Thread::buildInitialStack(thread->getKernelStackTop())));
        thread->getContext().setRip(
            reinterpret_cast<uint64_t>(Thread::bootstrap));
        thread->getContext().setRflags(0x202);
        thread->setTask(task);
        thread->setCpuId(cpu::getCurrentId());
        thread->setCpuAffinity(~0ULL);
        const Thread* creator = currentThread();
        // 同一个进程内新建的线程，继承创建者当前的 nice（调度优先级）值；
        // 如果是新进程，则使用任务创建时记录的初始 nice 值。
        // 没有任何任务（纯内核线程）时 nice 记 0。
        thread->setNice(!task ? 0
                        : creator && creator->getTask() == task
                            ? creator->getNice()
                            : task->initialNice());
        thread->setWeight(task::getSchedulerWeight(thread->getNice()));
        thread->setTimeSlice(time::milliseconds2Nanoseconds(4));
        thread->setEntryPoint(entryPoint);
        thread->setArgument(argument);
        thread->setState(ThreadState::Ready);

        // 挂接成功才把线程所有权交给任务；被拒绝时栈和线程结构仍由创建者负责释放。
        if (task != nullptr && !task->attachThread(thread)) {
            mm::freeHeap(stack);
            mm::freeHeap(thread);

            return nullptr;
        }

        if (makeRunnable && task::isSchedulerInitialized()) {
            placeNewThread(*thread);
            task::enqueueThread(thread);
        }

        return thread;
    }

    /*
     * 创建一个将来要进入用户态执行的线程。
     * 步骤：
     *   1. 先校验入口地址和用户栈地址：两者都必须非 0，且低于 0x800000000000
     *      （这是用户空间的地址上限，超过就落进内核空间，绝不能交给用户代码）；
     *      系统服务还额外限制初始栈大小不超过 16 KiB。
     *   2. 复用上面的 Thread::create，但传 makeRunnable=false，先不入队。
     *   3. 填好用户栈、用户参数、用户寄存器现场、信号掩码、浮点状态和亲和性，
     *      最后才允许任何 CPU 选中它。
     * 为什么最后才入队：线程一旦进入运行队列，就可能立刻在另一个 CPU 上被
     * 调度；此时它的字段必须已经全部初始化完毕，否则就是竞态。
     * 信号掩码：每个线程有一组“当前被阻塞、暂时不投递”的信号，新线程继承
     * 创建者的这份集合。
     * 旧式 Sys 路径：会把初始用户栈内容复制到内核拥有的存储上（见下面的分支），
     * 因为服务线程执行时使用一份跨地址空间共享的内核栈。
     * 返回：成功返回线程指针；入口/栈地址非法、Sys 服务初始栈过大、或创建
     * 失败时返回 nullptr。
     */
    Thread* Thread::createUser(Task* task, uint64_t entryPoint,
                               uint64_t userStackPointer, uint64_t argument,
                               uint64_t initialStackSize,
                               uint64_t initialAffinity, bool makeRunnable) {
        // 用户线程入口和栈首先必须处于合法用户地址范围，内核不能替用户从任意高地址开始执行。
        if (task == nullptr || entryPoint == 0 || userStackPointer == 0 ||
            entryPoint >= 0x0000800000000000ULL ||
            userStackPointer >= 0x0000800000000000ULL) {
            return nullptr;
        }

        if (task->isSystemService() && initialStackSize > 16 * 1024) {
            return nullptr;
        }

        Thread* thread = Thread::create(
            task, reinterpret_cast<ThreadEntry>(entryPoint),
            reinterpret_cast<void*>(userStackPointer),
            task->isSystemService() ? ThreadType::Sys : ThreadType::User,
            false);

        if (thread != nullptr) {
            if (task->isSystemService()) {
                // 保留栈顶处 bootstrap 的 CPU 现场不动；服务线程执行时使用的
                // 是一份由所有地址空间共享的特权栈。
                // 这里留出 32 KiB 的间隔，再把初始栈内容放在它下面，并对齐到
                // 16 字节，满足 amd64 的栈对齐要求。
                uint64_t stack =
                    (reinterpret_cast<uint64_t>(thread->getKernelStackTop()) -
                     32 * 1024 - initialStackSize) &
                    ~15ULL;

                if (initialStackSize &&
                    !mm::copyFromAddressSpace(
                        reinterpret_cast<void*>(stack),
                        task->addressSpace().rootPhysicalAddress(),
                        userStackPointer, initialStackSize)) {
                    return nullptr;
                }

                userStackPointer = stack;
            }

            thread->setUserStack(reinterpret_cast<void*>(userStackPointer));
            thread->setUserArgument(reinterpret_cast<void*>(argument));
            thread->lastUserContext            = {};
            thread->lastUserContext.userRip    = entryPoint;
            thread->lastUserContext.userRsp    = userStackPointer;
            thread->lastUserContext.userRflags = 0x202;
            thread->hasUserContext             = true;
            Thread* creator                    = currentThread();
            // 同一进程内创建的新线程，继承创建者当前的信号掩码和浮点/SIMD
            // 硬件状态；跨进程创建（creator 属于别的任务）时不继承。
            if (creator && creator->getTask() == task) {
                thread->setSignalMask(creator->getSignalMask());
                asm volatile("fxsave64 %0"
                             : "=m"(thread->floatingPointState)
                             :
                             : "memory");
            }

            thread->setCpuAffinity(initialAffinity);

            // 用户入口、参数、栈和亲和性全部填好以后才入队，防止别的 CPU
            // 提前运行半成品。
            if (makeRunnable && task::isSchedulerInitialized()) {
                placeNewThread(*thread);
                task::enqueueThread(thread);
            }
        }

        return thread;
    }

    /*
     * fork 复制出来的子线程的创建路径：把拷下来的系统调用帧先放在堆上，等子
     * 线程第一次真正运行再交给它。
     * 子线程从同一个用户态继续点返回 0（fork 的经典约定：父进程得到子进程的
     * PID，子进程得到 0），并继承调用者的信号帧链和浮点状态；它的临时类型
     * Kernel 会在恢复保存的上下文之前改成 User。
     * 参数：task 是子线程所属任务；context 是父线程被系统调用打断时的寄存器
     * 现场；signalMask 是子线程要继承的信号掩码。
     * 返回：成功返回线程指针，失败（任务为空、地址越界、堆分配失败）返回
     * nullptr。 锁：内部会获取任务锁，调用者不要持锁。
     */
    Thread* Thread::createForkedUser(Task* task, const SyscallContext& context,
                                     uint64_t signalMask) {
        if (task == nullptr || context.userRip >= 0x0000800000000000ULL ||
            context.userRsp >= 0x0000800000000000ULL) {
            return nullptr;
        }

        ::task::SyscallContext* childContext = static_cast<SyscallContext*>(
            mm::allocateHeap(sizeof(SyscallContext)));

        if (childContext == nullptr) {
            return nullptr;
        }

        *childContext     = context;
        childContext->rax = 0;
        Thread* thread =
            Thread::create(task, Thread::forkedUserEntry, childContext,
                           ThreadType::Kernel, false);

        if (thread != nullptr) {
            thread->setUserStack(reinterpret_cast<void*>(context.userRsp));
            thread->setSignalMask(signalMask);
            thread->lastUserContext = *childContext;
            thread->hasUserContext  = true;

            if (currentThread()) {
                // 继承父线程的 CPU 亲和性，以及信号处理相关的状态：处理函数
                // 使用的备用栈、当前活动帧、以及信号嵌套深度。
                thread->setCpuAffinity(currentThread()->getCpuAffinity());
                thread->signalStack       = currentThread()->signalStack;
                thread->activeSignalFrame = currentThread()->activeSignalFrame;
                thread->signalDepth       = currentThread()->signalDepth;
            }

            asm volatile("fxsave64 %0"
                         : "=m"(thread->floatingPointState)
                         :
                         : "memory");
            // 调度器已经启动时，先挑一个合适的 CPU，再把线程放进运行队列。
            if (task::isSchedulerInitialized()) {
                placeNewThread(*thread);
                task::enqueueThread(thread);
            }
        }

        return thread;
    }

    /*
     * 线程第一次被调度上来时执行的入口（bootstrap，即“首次运行/引导”）。
     * 为什么不能省掉这一步：普通线程是在 switchThreadContext 的后半段“返回”
     * 的，而新线程并没有更早的一次调度调用可以返回，因此它必须自己完成这次
     * 上下文切换的收尾记账，然后才能打开中断。
     * 之后按线程类型分流：
     *   User / Sys —— 进入用户态或服务入口，正常情况下不会返回；
     *   普通内核线程 —— 直接调用入口函数，返回后就退出本线程。
     * 另外：返回型的内核入口一旦 return，同样走退出线程的路径。
     * 前提：本函数运行在刚被选中的线程自己的内核栈上，此时中断仍关闭。
     */
    void Thread::bootstrap() {
        task::finishContextSwitch();
        cpu::enableInterrupts();
        Thread* thread = currentThread();

        if (thread->getType() == ThreadType::User) {
            enterUserMode(
                reinterpret_cast<uint64_t>(thread->getEntryPoint()),
                reinterpret_cast<uint64_t>(thread->getUserStack()),
                reinterpret_cast<uint64_t>(thread->getUserArgument()));
        }

        if (thread->getType() == ThreadType::Sys) {
            enterSystemService(
                reinterpret_cast<uint64_t>(thread->getEntryPoint()),
                reinterpret_cast<uint64_t>(thread->getUserStack()),
                reinterpret_cast<uint64_t>(thread->getUserArgument()));
        }

        thread->getEntryPoint()(thread->getArgument());
        exitCurrentThread(0);
    }

    /*
     * 切换到 next 线程：先保存自己的浮点状态、恢复对方的浮点状态，再用汇编
     * 交换两者的内核栈指针。
     * 关键点：switchThreadContext 返回之后，代码只有在本线程将来再次被调度时
     * 才会继续往下执行；也就是说这个函数对每个线程都“看起来像”一次普通调用。
     */
    void Thread::switchTo(Thread* next) {
        Thread* previous = this;
        // 内核 C++ 代码只使用通用寄存器，所以活着的浮点状态始终属于这个用户
        // 线程；即使它正阻塞在系统调用里，也要原样保存。
        asm volatile("fxsave64 %0" : "=m"(floatingPointState) : : "memory");
        next->restoreFloatingPointState();
        switchThreadContext(&previous->getContext().accessStackPointer(),
                            next->getContext().getStackPointer());
    }

    /*
     * 返回“当前 CPU 上正在运行的线程”，而不是进程范围内的主线程。
     */
    Thread* currentThread() { return cpu::getCurrent()->getCurrentThread(); }

    /*
     * 在把对象发布出去之前，先把新分配的原始存储清零并重置浮点状态。
     * 为什么不能对活着的线程调用：清零会破坏它挂在等待队列/链表里的链接，
     * 以及清理逻辑所依赖的归属信息。
     */
    void Thread::clearStorage() {
        Thread* thread = this;
        uint8_t* bytes = reinterpret_cast<uint8_t*>(thread);

        for (uint64_t index = 0; index < sizeof(Thread); ++index) {
            bytes[index] = 0;
        }

        thread->resetFloatingPointState();
    }

    /*
     * 构造一个空的 FXSAVE 镜像：x87 控制字和 MXCSR 都取默认的“已屏蔽异常”值。
     * 线程切换和信号处理恢复这份硬件布局，所以它必须是一个合法的初值。
     * 说明：浮点状态指 x87/SSE 的寄存器与控制字内容，属于线程私有现场。
     */
    void Thread::resetFloatingPointState() {
        for (uint8_t& byte : floatingPointState) {
            byte = 0;
        }

        // FXSAVE 布局：第 0、1 字节是 x87 控制字，第 24、25 字节是 MXCSR；
        // x87 标记字（tag word）保持为“空”状态，表示各寄存器里没有有效值。
        floatingPointState[0]  = 0x7f;
        floatingPointState[1]  = 0x03;
        floatingPointState[24] = 0x80;
        floatingPointState[25] = 0x1f;
    }

    /*
     * 从对齐的 512 字节原生浮点保存区恢复浮点/SIMD 硬件状态。
     */
    void Thread::restoreFloatingPointState() {
        asm volatile("fxrstor64 %0" : : "m"(floatingPointState) : "memory");
    }

    /*
     * 从高地址往低地址摆出一个“假的”初始栈帧，使它与 switchThreadContext 中
     * 的 15 次通用寄存器出栈、popfq 和 ret 完全对应。
     * 为什么需要假栈帧：新线程第一次被恢复时，汇编并不区分真实现场和构造的
     * 现场，它只会照常弹栈；所以这里必须按同样的顺序放好 rflags 和入口地址。
     * 合成出来的返回地址会进入 bootstrap，并且保持 ABI 要求的栈对齐。
     * 改这里的布局时，必须同步修改汇编里的 switchThreadContext。
     * 返回：新的栈顶指针（栈向低地址增长，所以返回值小于传入的 stackTop）。
     */
    uint64_t* Thread::buildInitialStack(void* stackTop) {
        uint64_t* stack = static_cast<uint64_t*>(stackTop);
        *--stack        = 0;
        *--stack        = reinterpret_cast<uint64_t>(Thread::bootstrap);
        *--stack        = 0x202;
        for (uint32_t registerIndex = 0; registerIndex < 15; ++registerIndex) {
            *--stack = 0;
        }

        return stack;
    }

    /*
     * fork 子线程第一次运行时进入这里：先把这份临时放在堆上的系统调用帧复制
     * 到本线程的栈上，再释放堆内存。
     * 为什么要复制：resumeUserContext 会把这份帧再展开成统一的 IRET 返回布局，
     * 之后临时分配就没有用了，继续留着只会泄漏。
     * 参数 argument 就是 createForkedUser 里分配的那个 SyscallContext。
     * 前提：运行在子线程自己的内核栈上；context 或线程为空说明状态已损坏，
     * 只能停机。
     */
    void Thread::forkedUserEntry(void* argument) {
        ::task::SyscallContext* context =
            static_cast<SyscallContext*>(argument);
        Thread* thread = currentThread();

        if (context == nullptr || thread == nullptr) {
            cpu::halt();
        }

        SyscallContext saved = *context;
        mm::freeHeap(context);
        thread->setType(ThreadType::User);
        resumeUserContext(&saved);
    }

    /*
     * 在任务锁的保护下把线程发布到任务里，并且只在任务仍然接受新线程时成功。
     * 第一个挂上来的线程成为主线程，并直接使用 Task 的 ID；之后挂上来的线程
     * 保留各自单独分配的 ID（因为任务 ID 已经被主线程占了）。
     * 锁/中断前提：函数内部获取 lock_，调用者不要再持有它。
     * 返回：false 表示任务已不再接受线程（例如正在退出），此时线程没有被发布，
     * 调用者需要自行释放它；true 表示线程已进入线程链表，状态转为 Running。
     */
    bool Task::attachThread(Thread* thread) {
        if (thread == nullptr) {
            return false;
        }

        InterruptSpinLockGuard guard(lock_);

        // 退出或 exec 的不可逆阶段不接受新线程，检查与挂链必须共用任务锁。
        if (!acceptsThreads_) {
            return false;
        }

        thread->setTask(this);
        thread->setTaskNext(threads_);
        threads_ = thread;
        ++threadCount_;

        // 第一个线程成为主线程并沿用 PID，后续线程使用各自单独分配的 TID。
        if (mainThread_ == nullptr) {
            thread->setId(id_);
            mainThread_ = thread;
        }

        state_ = TaskState::Running;

        return true;
    }

}  // namespace task
