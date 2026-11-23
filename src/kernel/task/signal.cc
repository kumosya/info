/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/signal.cc
 * 维护待处理信号，在用户栈安装处理现场，并安全恢复用户执行状态。
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace task {

    // 现场标记用于识别信号返回格式，但还需检查活动现场链，只有标记正确不能恢复任意旧现场。
    constexpr uint64_t signalFrameMagic   = 0x5349474652414d46ULL;
    constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
    constexpr uint64_t unmaskable         = (1ULL << 8) | (1ULL << 18);

    // 用户栈现场保存前一层链接、可编辑寄存器和浮点状态、掩码以及信号来源，支持嵌套处理。
    struct alignas(16) SignalFrame {
            uint64_t magic, previous;
            OsUserContext user;
            OsSignalInfo info;
    };

    /*
     * 检查地址是否位于可供用户态访问的范围，用于校验信号处理入口和用户上下文。
     */
    static bool userAddress(uint64_t address) {
        return address >= 0x10000 && address < maximumUserAddress;
    }

    /*
     * 判断栈指针是否落在指定信号备用栈内，避免重复切栈或错误判断栈边界。
     */
    static bool onStack(const OsSignalStack& stack, uint64_t rsp) {
        const uint64_t base = reinterpret_cast<uint64_t>(stack.pointer);

        return stack.size && rsp >= base && rsp - base < stack.size;
    }

    /*
     * 把中断保存的寄存器现场转换成系统调用现场格式，复用同一套信号投递逻辑。
     */
    static void interruptToSyscall(const cpu::Context& in,
                                   SyscallContext& out) {
        static_assert(offsetof(cpu::Context, vector) ==
                      offsetof(SyscallContext, userRip));
        uint8_t* destination  = reinterpret_cast<uint8_t*>(&out);
        const uint8_t* source = reinterpret_cast<const uint8_t*>(&in);

        for (size_t i = 0; i < 15 * sizeof(uint64_t); ++i) {
            destination[i] = source[i];
        }

        out.userRip    = in.rip;
        out.userRsp    = in.rsp;
        out.userRflags = in.rflags;
    }

    /*
     * 把修改后的系统调用现场写回中断现场，使中断返回时进入信号处理函数或恢复用户代码。
     */
    static void syscallToInterrupt(const SyscallContext& in,
                                   cpu::Context& out) {
        uint8_t* destination  = reinterpret_cast<uint8_t*>(&out);
        const uint8_t* source = reinterpret_cast<const uint8_t*>(&in);

        for (size_t i = 0; i < 15 * sizeof(uint64_t); ++i) {
            destination[i] = source[i];
        }

        out.rip    = in.userRip;
        out.rsp    = in.userRsp;
        out.rflags = in.userRflags;
    }

    /*
     * 在用户栈上建立信号现场，保存旧寄存器和信号掩码，再把返回现场改成信号处理入口。
     * 写入失败时不能带着半份现场返回用户态。
     */
    static bool install(SyscallContext& context, uint32_t signo,
                        const SignalAction& action, const FaultRecord* fault) {
        // 处理现场属于当前执行线程，其信号栈、掩码和嵌套链都不能写入任务中的其他线程。
        Thread& thread = *currentThread();

        if (thread.signalDepth >= 8 || !userAddress(action.getHandler()) ||
            !userAddress(action.getRestorer())) {
            return false;
        }

        {
            Task* owner = task::getCurrent();
            PreemptionSpinLockGuard guard(owner->addressSpace().lock());
            const uint64_t entries[] = {action.getHandler(),
                                        action.getRestorer()};

            for (uint64_t entry : entries) {
                uint64_t flags = 0;

                if (!mm::getPhysicalAddressInAddressSpace(
                        owner->addressSpace().rootPhysicalAddress(), entry,
                        &flags) ||
                    !(flags & ::mm::pageUser) ||
                    (flags & ::mm::pageNoExecute)) {
                    return false;
                }
            }
        }

        SignalFrame frame{};
        frame.magic                = signalFrameMagic;
        frame.previous             = thread.activeSignalFrame;
        const uint64_t handlerMask = thread.getSignalMask();
        /* pselect 类等待结束后，处理函数返回时应恢复等待前的掩码。 */
        frame.user.mask  = thread.signalRestoreMask();
        frame.user.stack = thread.signalStack;
        frame.user.stack.flags =
            thread.signalStack.size
                ? (onStack(thread.signalStack, context.userRsp) ? 1 : 0)
                : 2;
        task::toMachineContext(context, frame.user.machine);
        thread.saveFloatingPointState();

        for (size_t i = 0; i < 512; ++i) {
            reinterpret_cast<uint8_t*>(frame.user.fpstate)[i] =
                thread.fpState()[i];
        }

        frame.info.signo = signo;

        if (signo == static_cast<uint32_t>(Signal::Child)) {
            frame.info = task::getCurrent()->childSignalInfo();
        }

        if (fault) {
            frame.info.code                             = fault->code;
            frame.info.address                          = fault->addressValid
                                                              ? reinterpret_cast<void*>(fault->address)
                                                              : nullptr;
            frame.user.machine.registers[OS_REG_ERR]    = fault->error;
            frame.user.machine.registers[OS_REG_TRAPNO] = fault->vector;
            frame.user.machine.registers[OS_REG_CR2] =
                fault->addressValid ? fault->address : 0;
        }

        uint64_t top = context.userRsp;

        if ((action.getFlags() & signalFlagOnStack) &&
            thread.signalStack.size && !onStack(thread.signalStack, top)) {
            top = reinterpret_cast<uint64_t>(thread.signalStack.pointer) +
                  thread.signalStack.size;
        }

        if (top < sizeof(frame) + 136 || top >= maximumUserAddress) {
            return false;
        }

        // 保留用户 ABI 的栈下方 128 字节区域，嵌套信号也不能覆盖它。
        const uint64_t address = (top - 128 - sizeof(frame)) & ~15ULL;

        // 选择普通用户栈或备用信号栈后，再为现场和栈对齐留空间，不能覆盖尚需保留的用户栈内容。
        const uint64_t stack = address - 8;

        if (onStack(thread.signalStack, top - 1) &&
            stack < reinterpret_cast<uint64_t>(thread.signalStack.pointer)) {
            return false;
        }

        // 用户可见浮点指针必须指向本现场内嵌的保存区，恢复时拒绝任意其他用户地址。
        frame.user.machine.fpregs =
            reinterpret_cast<void*>(address + offsetof(SignalFrame, user) +
                                    offsetof(OsUserContext, fpstate));
        const uint64_t restorer = action.getRestorer();

        // 现场必须完整写入用户内存才能跳进处理函数，写入失败时不能把半份现场设为活动现场。
        if (!task::copyToUser(reinterpret_cast<void*>(address), &frame,
                              sizeof(frame)) ||
            !task::copyToUser(reinterpret_cast<void*>(stack), &restorer,
                              sizeof(restorer))) {
            return false;
        }

        // 整份现场写完才设为活动现场；保留上一层链接，并从新掩码中清除不可屏蔽的信号。
        thread.activeSignalFrame = address;
        ++thread.signalDepth;
        thread.finishTemporarySignalMask();
        thread.interruptReadinessMask();
        ++thread.signalGeneration;
        thread.setSignalMask((handlerMask | action.getMask() |
                              ((action.getFlags() & signalFlagNoDefer)
                                   ? 0
                                   : (1ULL << (signo - 1)))) &
                             ~unmaskable);

        // 保存好旧现场后，把本次返回用户态的位置改为处理函数，并按调用约定放置信号参数。
        context.userRip = action.getHandler();
        context.userRsp = stack;
        context.userRflags &= ~((1ULL << 8) | (1ULL << 10) | (1ULL << 18));
        context.rdi = signo;
        context.rsi = address + offsetof(SignalFrame, info);
        context.rdx = address + offsetof(SignalFrame, user);
        // 处理函数不能继承尚未处理的未屏蔽浮点异常，先清理其初始浮点环境。
        thread.resetFloatingPointState();
        thread.restoreFloatingPointState();

        return true;
    }

    /*
     * 把系统调用寄存器转换成用户可见的机器上下文，供信号处理函数查看和恢复。
     */
    void toMachineContext(const SyscallContext& s, OsMachineContext& target) {
        int64_t* r = target.registers;
        r[0]       = s.r8;
        r[1]       = s.r9;
        r[2]       = s.r10;
        r[3]       = s.r11;
        r[4]       = s.r12;
        r[5]       = s.r13;
        r[6]       = s.r14;
        r[7]       = s.r15;
        r[8]       = s.rdi;
        r[9]       = s.rsi;
        r[10]      = s.rbp;
        r[11]      = s.rbx;
        r[12]      = s.rdx;
        r[13]      = s.rax;
        r[14]      = s.rcx;
        r[15]      = s.userRsp;
        r[16]      = s.userRip;
        r[17]      = s.userRflags;
        r[18]      = 0x23;
    }

    /*
     * 从经过检查的用户机器上下文恢复寄存器，供信号返回路径使用。
     */
    void fromMachineContext(const OsMachineContext& source, SyscallContext& s) {
        const int64_t* r = source.registers;
        s.r8             = r[0];
        s.r9             = r[1];
        s.r10            = r[2];
        s.r11            = r[3];
        s.r12            = r[4];
        s.r13            = r[5];
        s.r14            = r[6];
        s.r15            = r[7];
        s.rdi            = r[8];
        s.rsi            = r[9];
        s.rbp            = r[10];
        s.rbx            = r[11];
        s.rdx            = r[12];
        s.rax            = r[13];
        s.rcx            = r[14];
        s.userRsp        = r[15];
        s.userRip        = r[16];
        s.userRflags     = r[17];
    }

    /*
     * 判断某信号的默认动作是否包含核心转储。
     */
    bool hasCoreDefault(uint32_t s) {
        return s == 3 || s == 4 || s == 5 || s == 6 || s == 7 || s == 8 ||
               s == 11 || s == 24 || s == 25 || s == 31;
    }

    /*
     * 处理无法继续执行的致命信号。
     * 先记录信号与故障信息，按默认策略申请转储，再结束当前任务。
     */
    [[noreturn]] void fatal(uint32_t signo, const FaultRecord* fault) {
        Task* task = task::getCurrent();

        if (!task || !currentThread()) {
            cpu::halt();
        }

        device::serialWrite("[SIGNAL] fatal pid=");
        device::serialWriteDecimal(task->id());
        device::serialWrite(" signal=");
        device::serialWriteDecimal(signo);

        if (fault) {
            device::serialWrite(" vector=");
            device::serialWriteDecimal(fault->vector);
            device::serialWrite(" rip=");
            device::serialWriteHex(fault->rip);
            device::serialWrite(" rsp=");
            device::serialWriteHex(fault->rsp);
        }

        device::serialWrite("\n");
        task->recordSignalExit(signo, fault);
        task::exitCurrent(-static_cast<int>(signo));
    }

    /*
     * 从未屏蔽的待处理信号中取出一个，按忽略、默认或用户处理动作修改返回现场。
     */
    bool deliverPendingSignal(SyscallContext* context) {
        Task* task     = task::getCurrent();
        Thread* thread = currentThread();

        if (!context || !task || !thread) {
            return false;
        }

        for (uint32_t n = 0; n < maximumSignal; ++n) {
            SignalAction action{};
            const uint32_t signo =
                task->takePendingSignal(thread, ~0ULL, action);

            if (!signo) {
                return false;
            }

            if (action.getHandler() == signalIgnore) {
                continue;
            }

            if (action.getHandler() == signalDefault) {
                task->deliverSignal(signo);
                continue;
            }

            if (!install(*context, signo, action, nullptr)) {
                fatal(static_cast<uint32_t>(Signal::SegmentationViolation));
            }

            return true;
        }

        return false;
    }

    /*
     * 校验用户提交的信号现场地址、内容和返回权限，再恢复寄存器、信号掩码及信号栈状态。
     */
    bool restoreSignalFrame(SyscallContext* context, uint64_t address) {
        Thread* thread = currentThread();
        SignalFrame frame{};

        // 用户栈中的信号现场可以被用户修改，恢复前需要检查活动链、标记和地址，不能只凭一个用户指针恢复寄存器。
        if (!context || !thread || !thread->signalDepth ||
            address != thread->activeSignalFrame ||
            !task::copyFromUser(&frame, reinterpret_cast<void*>(address),
                                sizeof(frame)) ||
            frame.magic != signalFrameMagic) {
            return false;
        }

        SyscallContext restored{};

        // 验证完成后才恢复通用寄存器、浮点状态和掩码，随后返回原来被信号打断的位置。
        fromMachineContext(frame.user.machine, restored);

        if (!userAddress(restored.userRip) || !userAddress(restored.userRsp) ||
            frame.user.machine.registers[OS_REG_CSGSFS] != 0x23 ||
            frame.user.machine.fpregs !=
                reinterpret_cast<void*>(address + offsetof(SignalFrame, user) +
                                        offsetof(OsUserContext, fpstate))) {
            return false;
        }

        alignas(16) uint8_t live[512];
        asm volatile("fxsave64 %0" : "=m"(live) : : "memory");
        const uint32_t mask = *reinterpret_cast<uint32_t*>(live + 28)
                                  ? *reinterpret_cast<uint32_t*>(live + 28)
                                  : 0xffbf;
        const uint8_t* fp =
            reinterpret_cast<const uint8_t*>(frame.user.fpstate);

        if (*reinterpret_cast<const uint32_t*>(fp + 24) & ~mask) {
            return false;
        }

        // 只保留用户允许修改的标志，并强制设置中断允许位和保留位，不能经可写现场带入
        // IOPL 等特权状态。
        constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
        restored.userRflags = (restored.userRflags & allowedFlags) | 0x202;

        for (size_t i = 0; i < 512; ++i) {
            thread->fpState()[i] = fp[i];
        }

        thread->restoreFloatingPointState();
        thread->setSignalMask(frame.user.mask & ~unmaskable);
        thread->activeSignalFrame = frame.previous;
        --thread->signalDepth;
        *context = restored;
        return true;
    }

    /*
     * 把 CPU 异常转换成带故障地址和访问方式的进程信号。
     * 用户异常按信号规则处理，不能恢复的情况进入致命退出路径。
     */
    void raiseException(cpu::Context* context, uint32_t signo, int code,
                        uint64_t address, bool valid, FaultAccess access,
                        uint64_t debugStatus) {
        Thread* thread = currentThread();
        Task* task     = task::getCurrent();

        if (!thread || !task) {
            cpu::halt();
        }

        SyscallContext saved{};
        interruptToSyscall(*context, saved);
        thread->lastUserContext = saved;
        thread->hasUserContext  = true;
        FaultRecord fault{task->id(),
                          thread->getId(),
                          task->startedNanoseconds(),
                          time::getMonotonicTime(),
                          context->vector,
                          context->errorCode,
                          context->rip,
                          context->rsp,
                          address,
                          debugStatus,
                          signo,
                          code,
                          access,
                          valid};
        fault.origin      = (context->cs & 3) == 3 ? FaultOrigin::UserException
                                                   : FaultOrigin::ServiceException;
        thread->lastFault = fault;
        SignalAction action{};
        task->getSignalAction(signo, action);

        if ((thread->getSignalMask() & (1ULL << (signo - 1))) ||
            action.getHandler() <= signalIgnore) {
            fatal(signo, &fault);
        }

        if (action.getFlags() & signalFlagResetHand) {
            task->setSignalAction(signo, {}, nullptr);
        }

        if (!install(saved, signo, action, &fault)) {
            fatal(signo, &fault);
        }

        syscallToInterrupt(saved, *context);
    }

    /*
     * 在中断返回用户态之前检查调度和信号，使待处理事件在安全边界生效。
     */
    void returnToUser(cpu::Context* context) {
        if (!context || (context->cs & 3) != 3) {
            return;
        }

        Thread* thread = currentThread();

        if (!thread || thread->getType() != ThreadType::User) {
            return;
        }

        SyscallContext saved{};
        interruptToSyscall(*context, saved);
        thread->lastUserContext = saved;
        thread->hasUserContext  = true;
        const uint64_t flags    = cpu::disableInterrupts();
        cpu::enableInterrupts();
        deliverPendingSignal(&saved);
        cpu::restoreInterrupts(flags);
        syscallToInterrupt(saved, *context);
    }

    /*
     * 保存最近一次用户寄存器现场，便于之后的信号处理和核心转储读取。
     */
    void captureUserContext(const cpu::Context* context) {
        Thread* thread = currentThread();

        if (!thread || !context || (context->cs & 3) != 3) {
            return;
        }

        interruptToSyscall(*context, thread->lastUserContext);
        thread->hasUserContext = true;
    }

    /*
     * 检查信号编号和不可修改的信号，返回旧动作后安装新动作，修改在任务锁内完成。
     */
    bool Task::setSignalAction(uint32_t signal, const SignalAction& action,
                               SignalAction* oldAction) {
        if (!task::isValidSignal(signal) ||
            signal == static_cast<uint32_t>(Signal::Kill) ||
            signal == static_cast<uint32_t>(Signal::Stop)) {
            return false;
        }

        InterruptSpinLockGuard guard(lock_);

        if (oldAction != nullptr) {
            *oldAction = signalActions_[signal];
        }

        signalActions_[signal] = action;

        if (action.getHandler() == signalIgnore) {
            pendingSignals_ &= ~(1ULL << (signal - 1));
        }

        return true;
    }

    /*
     * 在任务锁内读取指定信号的处理动作。
     */
    bool Task::getSignalAction(uint32_t signal, SignalAction& action) {
        if (!task::isValidSignal(signal)) {
            return false;
        }

        InterruptSpinLockGuard guard(lock_);
        action = signalActions_[signal];

        return true;
    }

    /*
     * 合并任务级和指定线程的待处理信号位图，不在这里取走信号。
     */
    uint64_t Task::pendingSignals(Thread* thread) {
        InterruptSpinLockGuard guard(lock_);

        return pendingSignals_ |
               (thread == nullptr ? 0 : thread->getPendingSignals());
    }

    /* 只把会执行处理函数或默认致命动作的未屏蔽信号视为等待中断。 */
    bool Task::hasInterruptingSignal(Thread* thread) {
        if (!thread) return false;
        InterruptSpinLockGuard guard(lock_);
        const uint64_t pending =
            (pendingSignals_ | thread->getPendingSignals()) &
            ~thread->getSignalMask();
        for (uint32_t number = 1; number <= maximumSignal; ++number) {
            if (!(pending & (1ULL << (number - 1)))) continue;
            const uint64_t handler = signalActions_[number].getHandler();
            if (handler == signalIgnore) continue;
            if (handler != signalDefault) return true;
            if (number != static_cast<uint32_t>(Signal::Child) &&
                number != static_cast<uint32_t>(Signal::Continue) &&
                number != static_cast<uint32_t>(Signal::Urgent) &&
                number != static_cast<uint32_t>(Signal::WindowChanged))
                return true;
        }
        return false;
    }

    /*
     * 按接受集合和屏蔽规则挑选一个信号，清除相应待处理位，并取出处理动作；返回零表示没有可取信号。
     */
    uint32_t Task::takePendingSignal(Thread* thread, uint64_t accepted,
                                     SignalAction& action, bool respectMask) {
        if (thread == nullptr) {
            return 0;
        }

        InterruptSpinLockGuard guard(lock_);
        uint64_t available =
            (pendingSignals_ | thread->getPendingSignals()) & accepted;

        if (respectMask) {
            available &= ~thread->getSignalMask();
        }

        if (available == 0) {
            return 0;
        }

        // 从允许位图中选出最低编号的待处理信号，随后按来源清除任务级或线程级待处理位。
        const uint32_t signal =
            static_cast<uint32_t>(__builtin_ctzll(available)) + 1;
        const uint64_t bit = 1ULL << (signal - 1);

        if ((thread->getPendingSignals() & bit) != 0) {
            thread->setPendingSignals(thread->getPendingSignals() & (~bit));
        } else {
            pendingSignals_ &= ~bit;
        }

        action = signalActions_[signal];

        if ((action.getFlags() & signalFlagResetHand) != 0) {
            signalActions_[signal] = {};
        }

        return signal;
    }

    /*
     * 向指定线程排入信号，并打断可中断等待。
     * 必须执行进程级动作的信号交给统一投递路径。
     */
    bool Task::deliverThreadSignal(uint64_t threadId, uint32_t signal) {
        if (signal > maximumSignal || this == task::getIdle() ||
            isTerminated()) {
            return false;
        }

        {
            InterruptSpinLockGuard guard(lock_);
            Thread* target = threads_;

            while (target != nullptr && target->getId() != threadId) {
                target = target->getTaskNext();
            }

            if (target == nullptr ||
                target->getState() == ThreadState::Terminated) {
                return false;
            }

            if (signal == 0) {
                return true;
            }

            const SignalAction action = signalActions_[signal];

            if (action.getHandler() == signalIgnore) {
                return true;
            }

            if (action.getHandler() != signalDefault) {
                target->setPendingSignals(target->getPendingSignals() |
                                          (1ULL << (signal - 1)));

                if (target != currentThread() &&
                    (target->getSignalMask() & (1ULL << (signal - 1))) == 0) {
                    Endpoint::interruptThread(target);
                    interruptUserAddressWait(target);
                }

                return true;
            }
        }

        return deliverSignal(signal);
    }

    /*
     * 执行进程级信号语义。
     * 继续信号先解除作业暂停，停止信号挂起线程，致命信号终止任务；通知父任务和退出操作放在任务锁外进行。
     */
    bool Task::deliverSignal(uint32_t signal) {
        if (!task::isValidSignal(signal) || this == task::getIdle() ||
            isTerminated()) {
            return false;
        }

        struct ParentNotifier {
                Task& task;

                /* 在子任务锁外更新父任务事件并发送
                 * SIGCHLD，避免父子任务互相等锁。 */
                void operator()(int code, int status) const {
                    ::task::TaskReference parent = task.parentReference();

                    if (!parent) {
                        return;
                    }

                    {
                        InterruptSpinLockGuard guard(parent->lock_);
                        parent->childSignalInfo_        = {};
                        parent->childSignalInfo_.signo  = 17;
                        parent->childSignalInfo_.code   = code;
                        parent->childSignalInfo_.pid    = task.id_;
                        parent->childSignalInfo_.uid    = task.uid_;
                        parent->childSignalInfo_.status = status;
                    }

                    parent->notifyChildStateChange();
                    parent->deliverSignal(17);
                }
        } notifyState{*this};

        // 继续信号先解除作业暂停，即使它的处理动作被忽略或屏蔽，恢复动作也必须生效。
        if (signal == static_cast<uint32_t>(Signal::Continue)) {
            bool changed;
            {
                InterruptSpinLockGuard guard(lock_);
                changed  = stopped_;
                stopped_ = false;

                if (changed) {
                    continuedPending_ = true;
                }

                for (Thread* thread = threads_; thread;
                     thread         = thread->getTaskNext()) {
                    if (memoryFrozen() ||
                        thread->getState() != ThreadState::Stopped) {
                        continue;
                    }

                    thread->setState(thread->getStateBeforeStop());

                    if (thread->getState() == ThreadState::Ready ||
                        thread->getState() == ThreadState::Running) {
                        thread->setState(ThreadState::Ready);
                        task::enqueueThread(thread);
                    }
                }
            }

            // 即使继续信号的处理函数被屏蔽或忽略，解除作业暂停仍然必须执行。
            if (changed) {
                notifyState(6, signal);
            }
        }

        SignalAction action{};
        bool queued = false;
        {
            InterruptSpinLockGuard guard(lock_);
            action = signalActions_[signal];

            if (action.getHandler() == signalIgnore) {
                return true;
            }

            bool allBlocked = true;

            for (Thread* thread = threads_; thread != nullptr;
                 thread         = thread->getTaskNext()) {
                if (thread->getState() != ThreadState::Terminated &&
                    (thread->getSignalMask() & (1ULL << (signal - 1))) == 0) {
                    allBlocked = false;
                    break;
                }
            }

            // Kill 和 Stop
            // 不能作为普通自定义处理动作排队；其余信号是否排队还取决于动作和线程屏蔽情况。
            queued = signal != static_cast<uint32_t>(Signal::Kill) &&
                     signal != static_cast<uint32_t>(Signal::Stop) &&
                     (action.getHandler() != signalDefault || allBlocked);

            if (queued) {
                // 普通信号按位登记，重复到达会合并；真正安装用户处理现场要等某个线程返回用户态。
                pendingSignals_ |= 1ULL << (signal - 1);

                for (Thread* thread = threads_; thread;
                     thread         = thread->getTaskNext()) {
                    if (thread != currentThread() &&
                        thread->getState() != ThreadState::Terminated &&
                        (thread->getSignalMask() & (1ULL << (signal - 1))) ==
                            0) {
                        Endpoint::interruptThread(thread);
                        interruptUserAddressWait(thread);
                    }
                }
            }
        }

        // 普通信号通过待处理位合并重复投递；可接收线程已被打断，实际用户现场要等选中线程返回用户态时再安装。
        if (queued) {
            return true;
        }

        if (signal == static_cast<uint32_t>(Signal::WindowChanged) ||
            signal == static_cast<uint32_t>(Signal::Child) ||
            signal == static_cast<uint32_t>(Signal::Urgent)) {
            return true;
        }

        if (signal == static_cast<uint32_t>(Signal::Continue)) {
            return true;
        }

        if (signal == static_cast<uint32_t>(Signal::Stop) ||
            signal == static_cast<uint32_t>(Signal::TerminalStop) ||
            signal == static_cast<uint32_t>(Signal::TerminalInput) ||
            signal == static_cast<uint32_t>(Signal::TerminalOutput)) {
            {
                InterruptSpinLockGuard guard(lock_);
                stopped_          = true;
                stopSignal_       = signal;
                stopReported_     = false;
                continuedPending_ = false;

                for (Thread* thread = threads_; thread != nullptr;
                     thread         = thread->getTaskNext()) {
                    if (thread->getState() == ThreadState::Terminated ||
                        thread->getState() == ThreadState::Stopped) {
                        continue;
                    }

                    thread->setStateBeforeStop(thread->getState());
                    task::dequeueThread(thread);
                    thread->setState(ThreadState::Stopped);
                    cpu::requestRemoteReschedule(
                        static_cast<uint32_t>(thread->getCpuId()));
                }
            }

            notifyState(5, signal);

            return true;
        }

        // 先保存首次致命原因，再终止其他线程；清理和转储发布可能稍后在别的执行上下文完成。
        recordSignalExit(signal);
        terminateOtherThreads(-static_cast<int>(signal));

        if (this == task::getCurrent()) {
            exitCurrentThread(-static_cast<int>(signal));
        }

        destroy();

        return true;
    }

    /*
     * 在任务锁内读取最近一次子任务信号信息，供父任务信号处理使用。
     */
    OsSignalInfo Task::childSignalInfo() {
        InterruptSpinLockGuard guard(lock_);

        return childSignalInfo_;
    }

    /*
     * 验证信号编号是否属于当前内核支持范围。
     */
    bool isValidSignal(uint32_t signal) {
        return signal >= 1 && signal <= maximumSignal;
    }

    /*
     * 按任务编号投递信号，检查调用者身份及服务保护规则；零号信号只检查目标是否存在且允许访问。
     */
    bool sendSignalToTask(uint64_t taskId, uint32_t signal,
                          Task* authenticatedCaller) {
        ::task::TaskReference task = task::find(taskId);
        Task* caller =
            authenticatedCaller ? authenticatedCaller : task::getCurrent();

        if (task && signal && task->managedService()) {
            return false;
        }

        if (task && caller && caller->effectiveUid() != 0 &&
            caller->uid() != task->uid() &&
            caller->effectiveUid() != task->uid()) {
            return false;
        }

        return task != nullptr && (signal == 0 ? !task->isTerminated()
                                               : task->deliverSignal(signal));
    }

}  // namespace task
