/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/signal.cc
 * Signal selection, user-frame delivery, and validated context restoration.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace kernel::task {

namespace {
// Frame magic distinguishes the native signal-return ABI from arbitrary user
// bytes. Frame-chain identity is checked separately; magic alone does not
// authorize restoring an old or unrelated frame.
constexpr uint64_t signalFrameMagic   = 0x5349474652414d46ULL;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
constexpr uint64_t unmaskable         = (1ULL << 8) | (1ULL << 18);
// Fixed user-stack layout: previous links nested active frames, user holds
// editable register/FP state and mask, and info carries signal provenance.
struct alignas(16) SignalFrame {
    uint64_t magic, previous;
    OsUserContext user;
    OsSignalInfo info;
};
// Native user instruction/stack addresses must be in the allowed low half.
bool userAddress(uint64_t address) {
    return address >= 0x10000 && address < maximumUserAddress;
}
// Subtraction after the lower-bound check avoids overflowing base + size.
bool onStack(const OsSignalStack& stack, uint64_t rsp) {
    const auto base = reinterpret_cast<uint64_t>(stack.pointer);
    return stack.size && rsp >= base && rsp - base < stack.size;
}
// Convert the shared GPR prefix, then copy the different interrupt tail into
// the syscall-style user tail. The offset assertion keeps assembly layouts
// synchronized; an interrupt vector is not a saved user RIP.
void interruptToSyscall(const kernel::cpu::Context& in, SyscallContext& out) {
    static_assert(offsetof(kernel::cpu::Context, vector) ==
                  offsetof(SyscallContext, userRip));
    auto* destination  = reinterpret_cast<uint8_t*>(&out);
    const auto* source = reinterpret_cast<const uint8_t*>(&in);
    for (size_t i = 0; i < 15 * sizeof(uint64_t); ++i)
        destination[i] = source[i];
    out.userRip    = in.rip;
    out.userRsp    = in.rsp;
    out.userRflags = in.rflags;
}
// Write a possibly signal-modified user continuation back into the interrupt
// frame while preserving interrupt vector/error metadata.
void syscallToInterrupt(const SyscallContext& in, kernel::cpu::Context& out) {
    auto* destination  = reinterpret_cast<uint8_t*>(&out);
    const auto* source = reinterpret_cast<const uint8_t*>(&in);
    for (size_t i = 0; i < 15 * sizeof(uint64_t); ++i)
        destination[i] = source[i];
    out.rip    = in.userRip;
    out.rsp    = in.userRsp;
    out.rflags = in.userRflags;
}
// Build one signal frame on the current or alternate user stack, then redirect
// the saved continuation to the handler. Validate executable handler/restorer
// pages first; copy the complete frame before publishing activeSignalFrame so
// partial writes cannot become an accepted return frame. Failure leaves
// delivery to the caller's fatal-signal policy.
bool install(SyscallContext& context, uint32_t signo,
             const SignalAction& action, const FaultRecord* fault) {
    Thread& thread = *currentThread();
    if (thread.signalDepth >= 8 || !userAddress(action.getHandler()) ||
        !userAddress(action.getRestorer()))
        return false;
    {
        Task* owner = task::getCurrent();
        PreemptionSpinLockGuard guard(owner->addressSpace().lock());
        const uint64_t entries[] = {action.getHandler(), action.getRestorer()};
        for (uint64_t entry : entries) {
            uint64_t flags = 0;
            if (!mm::getPhysicalAddressInAddressSpace(
                    owner->addressSpace().rootPhysicalAddress(), entry,
                    &flags) ||
                !(flags & ::kernel::mm::pageUser) ||
                (flags & ::kernel::mm::pageNoExecute))
                return false;
        }
    }
    SignalFrame frame{};
    frame.magic      = signalFrameMagic;
    frame.previous   = thread.activeSignalFrame;
    frame.user.mask  = thread.getSignalMask();
    frame.user.stack = thread.signalStack;
    frame.user.stack.flags =
        thread.signalStack.size
            ? (onStack(thread.signalStack, context.userRsp) ? 1 : 0)
            : 2;
    task::toMachineContext(context, frame.user.machine);
    thread.saveFloatingPointState();
    for (size_t i = 0; i < 512; ++i)
        reinterpret_cast<uint8_t*>(frame.user.fpstate)[i] = thread.fpState()[i];
    frame.info.signo = signo;
    if (signo == static_cast<uint32_t>(Signal::Child))
        frame.info = task::getCurrent()->childSignalInfo();
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
    if ((action.getFlags() & signalFlagOnStack) && thread.signalStack.size &&
        !onStack(thread.signalStack, top))
        top = reinterpret_cast<uint64_t>(thread.signalStack.pointer) +
              thread.signalStack.size;
    if (top < sizeof(frame) + 136 || top >= maximumUserAddress) return false;
    // Preserve the interrupted ABI's 128-byte red zone, including on nested
    // delivery.
    const uint64_t address = (top - 128 - sizeof(frame)) & ~15ULL;
    const uint64_t stack   = address - 8;
    if (onStack(thread.signalStack, top - 1) &&
        stack < reinterpret_cast<uint64_t>(thread.signalStack.pointer))
        return false;
    // The user-visible FP pointer must reference this exact frame's embedded
    // save area; restore rejects arbitrary pointers even within user memory.
    frame.user.machine.fpregs =
        reinterpret_cast<void*>(address + offsetof(SignalFrame, user) +
                                offsetof(OsUserContext, fpstate));
    const uint64_t restorer = action.getRestorer();
    if (!task::copyToUser(reinterpret_cast<void*>(address), &frame,
                          sizeof(frame)) ||
        !task::copyToUser(reinterpret_cast<void*>(stack), &restorer,
                          sizeof(restorer)))
        return false;
    // Only a fully written frame becomes active. Keep the previous frame link
    // for nested handlers and clear unmaskable bits from the new signal mask.
    thread.activeSignalFrame = address;
    ++thread.signalDepth;
    ++thread.signalGeneration;
    thread.setSignalMask(
        (frame.user.mask | action.getMask() |
         ((action.getFlags() & signalFlagNoDefer) ? 0
                                                  : (1ULL << (signo - 1)))) &
        ~unmaskable);
    context.userRip = action.getHandler();
    context.userRsp = stack;
    context.userRflags &= ~((1ULL << 8) | (1ULL << 10) | (1ULL << 18));
    context.rdi = signo;
    context.rsi = address + offsetof(SignalFrame, info);
    context.rdx = address + offsetof(SignalFrame, user);
    // A handler must not inherit a pending unmasked FP exception.
    thread.resetFloatingPointState();
    thread.restoreFloatingPointState();
    return true;
}
}  // namespace

// Map native syscall registers to the fixed POSIX-compatible user register
// indices. The user selector is supplied explicitly rather than copied from
// untrusted context fields.
void toMachineContext(const SyscallContext& s, OsMachineContext& target) {
    auto* r = target.registers;
    r[0]    = s.r8;
    r[1]    = s.r9;
    r[2]    = s.r10;
    r[3]    = s.r11;
    r[4]    = s.r12;
    r[5]    = s.r13;
    r[6]    = s.r14;
    r[7]    = s.r15;
    r[8]    = s.rdi;
    r[9]    = s.rsi;
    r[10]   = s.rbp;
    r[11]   = s.rbx;
    r[12]   = s.rdx;
    r[13]   = s.rax;
    r[14]   = s.rcx;
    r[15]   = s.userRsp;
    r[16]   = s.userRip;
    r[17]   = s.userRflags;
    r[18]   = 0x23;
}
// Read user-editable registers; restoreSignalFrame separately checks
// addresses, privilege-sensitive flags, selector and FP metadata before use.
void fromMachineContext(const OsMachineContext& source, SyscallContext& s) {
    const auto* r = source.registers;
    s.r8          = r[0];
    s.r9          = r[1];
    s.r10         = r[2];
    s.r11         = r[3];
    s.r12         = r[4];
    s.r13         = r[5];
    s.r14         = r[6];
    s.r15         = r[7];
    s.rdi         = r[8];
    s.rsi         = r[9];
    s.rbp         = r[10];
    s.rbx         = r[11];
    s.rdx         = r[12];
    s.rax         = r[13];
    s.rcx         = r[14];
    s.userRsp     = r[15];
    s.userRip     = r[16];
    s.userRflags  = r[17];
}
// Default core-producing signal set; this is policy, not a statement that
// requesting a snapshot guarantees a successfully published file.
bool hasCoreDefault(uint32_t s) {
    return s == 3 || s == 4 || s == 5 || s == 6 || s == 7 || s == 8 ||
           s == 11 || s == 24 || s == 25 || s == 31;
}
// Record the original fatal signal/fault before unified process exit.
// Exception vector numbers remain diagnostic fields, not exit-signal numbers.
[[noreturn]] void fatal(uint32_t signo, const FaultRecord* fault) {
    Task* task = task::getCurrent();
    if (!task || !currentThread()) cpu::halt();
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
// Choose unmasked pending signals before a user return. Consume ignored ones,
// apply default actions, and install at most one handler per pass. An invalid
// asynchronous handler frame becomes SIGSEGV instead of retrying the same
// malformed stack indefinitely.
bool deliverPendingSignal(SyscallContext* context) {
    Task* task     = task::getCurrent();
    Thread* thread = currentThread();
    if (!context || !task || !thread) return false;
    for (uint32_t n = 0; n < maximumSignal; ++n) {
        SignalAction action{};
        const uint32_t signo = task->takePendingSignal(thread, ~0ULL, action);
        if (!signo) return false;
        if (action.getHandler() == signalIgnore) continue;
        if (action.getHandler() == signalDefault) {
            task->deliverSignal(signo);
            continue;
        }
        if (!install(*context, signo, action, nullptr))
            fatal(static_cast<uint32_t>(Signal::SegmentationViolation));
        return true;
    }
    return false;
}
// Accept only the current innermost active frame. Validate all user-edited
// control state before restoring registers/FP, sanitize RFLAGS, restore the
// mask without Kill/Stop, then pop one nesting level. This returns false on
// invalid input; the task service applies the fatal policy.
bool restoreSignalFrame(SyscallContext* context, uint64_t address) {
    Thread* thread = currentThread();
    SignalFrame frame{};
    if (!context || !thread || !thread->signalDepth ||
        address != thread->activeSignalFrame ||
        !task::copyFromUser(&frame, reinterpret_cast<void*>(address),
                            sizeof(frame)) ||
        frame.magic != signalFrameMagic)
        return false;
    SyscallContext restored{};
    fromMachineContext(frame.user.machine, restored);
    if (!userAddress(restored.userRip) || !userAddress(restored.userRsp) ||
        frame.user.machine.registers[OS_REG_CSGSFS] != 0x23 ||
        frame.user.machine.fpregs !=
            reinterpret_cast<void*>(address + offsetof(SignalFrame, user) +
                                    offsetof(OsUserContext, fpstate)))
        return false;
    alignas(16) uint8_t live[512];
    asm volatile("fxsave64 %0" : "=m"(live) : : "memory");
    const uint32_t mask = *reinterpret_cast<uint32_t*>(live + 28)
                              ? *reinterpret_cast<uint32_t*>(live + 28)
                              : 0xffbf;
    const auto* fp      = reinterpret_cast<const uint8_t*>(frame.user.fpstate);
    if (*reinterpret_cast<const uint32_t*>(fp + 24) & ~mask) return false;
    // Keep only permitted user flags and force IF plus the reserved bit.
    // Neither IOPL nor privileged status may be smuggled through a writable
    // signal frame.
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    restored.userRflags = (restored.userRflags & allowedFlags) | 0x202;
    for (size_t i = 0; i < 512; ++i) thread->fpState()[i] = fp[i];
    thread->restoreFloatingPointState();
    thread->setSignalMask(frame.user.mask & ~unmaskable);
    thread->activeSignalFrame = frame.previous;
    --thread->signalDepth;
    *context = restored;
    return true;
}
// Deliver a synchronous CPU fault directly to this thread, preserving its
// exact hardware provenance rather than merging it into process pending bits.
// A blocked/ignored/default fault or failed frame install is fatal; returning
// to the unchanged fault instruction would otherwise repeat the exception.
void raiseException(cpu::Context* context, uint32_t signo, int code,
                    uint64_t address, bool valid, FaultAccess access,
                    uint64_t debugStatus) {
    Thread* thread = currentThread();
    Task* task     = task::getCurrent();
    if (!thread || !task) cpu::halt();
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
        action.getHandler() <= signalIgnore)
        fatal(signo, &fault);
    if (action.getFlags() & signalFlagResetHand)
        task->setSignalAction(signo, {}, nullptr);
    if (!install(saved, signo, action, &fault)) fatal(signo, &fault);
    syscallToInterrupt(saved, *context);
}
// Interrupt returns also check pending signals, so a user compute loop can
// receive signals without issuing syscalls. Convert the frame, temporarily
// allow interrupts for delivery work, then restore the entry interrupt state.
void returnToUser(cpu::Context* context) {
    if (!context || (context->cs & 3) != 3) return;
    Thread* thread = currentThread();
    if (!thread || thread->getType() != ThreadType::User) return;
    SyscallContext saved{};
    interruptToSyscall(*context, saved);
    thread->lastUserContext = saved;
    thread->hasUserContext  = true;
    const auto flags        = cpu::disableInterrupts();
    cpu::enableInterrupts();
    deliverPendingSignal(&saved);
    cpu::restoreInterrupts(flags);
    syscallToInterrupt(saved, *context);
}
// Keep the last actual user continuation for core/status even when a later
// snapshot finds this thread inside a kernel wait rather than at user RIP.
void captureUserContext(const cpu::Context* context) {
    Thread* thread = currentThread();
    if (!thread || !context || (context->cs & 3) != 3) return;
    interruptToSyscall(*context, thread->lastUserContext);
    thread->hasUserContext = true;
}

// Store a process-wide disposition under lock_; return the prior action when
// requested. Kill/Stop cannot be changed. Ignoring clears process-wide pending
// state for that signal; thread-private bits are selected separately.
bool Task::setSignalAction(uint32_t signal, const SignalAction& action,
                           SignalAction* oldAction) {
    if (!task::isValidSignal(signal) ||
        signal == static_cast<uint32_t>(Signal::Kill) ||
        signal == static_cast<uint32_t>(Signal::Stop)) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    if (oldAction != nullptr) *oldAction = signalActions_[signal];
    signalActions_[signal] = action;
    if (action.getHandler() == signalIgnore) {
        pendingSignals_ &= ~(1ULL << (signal - 1));
    }
    return true;
}

// Copy one process-wide disposition under the task lock.
bool Task::getSignalAction(uint32_t signal, SignalAction& action) {
    if (!task::isValidSignal(signal)) return false;
    InterruptSpinLockGuard guard(lock_);
    action = signalActions_[signal];
    return true;
}

// Union of task-shared and optional thread-private pending signals.
uint64_t Task::pendingSignals(Thread* thread) {
    InterruptSpinLockGuard guard(lock_);
    return pendingSignals_ |
           (thread == nullptr ? 0 : thread->getPendingSignals());
}

// Select the smallest accepted signal, optionally excluding masked bits.
// Prefer the thread-private source when both contain that signal, leaving the
// process-shared occurrence for a later delivery. ResetHand changes the
// disposition at selection, before user handler execution.
uint32_t Task::takePendingSignal(Thread* thread, uint64_t accepted,
                                 SignalAction& action, bool respectMask) {
    if (thread == nullptr) return 0;
    InterruptSpinLockGuard guard(lock_);
    uint64_t available =
        (pendingSignals_ | thread->getPendingSignals()) & accepted;
    if (respectMask) available &= ~thread->getSignalMask();
    if (available == 0) return 0;
    const uint32_t signal =
        static_cast<uint32_t>(__builtin_ctzll(available)) + 1;
    const uint64_t bit = 1ULL << (signal - 1);
    if ((thread->getPendingSignals() & bit) != 0)
        thread->setPendingSignals(thread->getPendingSignals() & (~bit));
    else
        pendingSignals_ &= ~bit;
    action = signalActions_[signal];
    if ((action.getFlags() & signalFlagResetHand) != 0) {
        signalActions_[signal] = {};
    }
    return signal;
}

// Resolve a live thread in this task. Zero probes existence; a custom action
// queues a private pending bit and interrupts eligible waits. Default actions
// still affect the process through deliverSignal, not just one thread.
bool Task::deliverThreadSignal(uint64_t threadId, uint32_t signal) {
    if (signal > maximumSignal || this == task::getIdle() || isTerminated()) {
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
        if (signal == 0) return true;
        const SignalAction action = signalActions_[signal];
        if (action.getHandler() == signalIgnore) return true;
        if (action.getHandler() != signalDefault) {
            target->setPendingSignals(target->getPendingSignals() |
                                      (1ULL << (signal - 1)));
            if (target != currentThread() &&
                (target->getSignalMask() & (1ULL << (signal - 1))) == 0)
                Endpoint::interruptThread(target);
            return true;
        }
    }
    return deliverSignal(signal);
}

// Apply process-wide semantics without holding lock_ across fatal exit or
// parent notification. SIGCONT first resumes job-control stop unconditionally;
// custom/blocked actions may still queue a handler afterward. Memory-recovery
// freeze is independent and must not be bypassed by SIGCONT.
bool Task::deliverSignal(uint32_t signal) {
    if (!task::isValidSignal(signal) || this == task::getIdle() ||
        isTerminated()) {
        return false;
    }
    auto notifyState = [&](int code, int status) {
        auto parent = parentReference();
        if (!parent) return;
        {
            InterruptSpinLockGuard guard(parent->lock_);
            parent->childSignalInfo_        = {};
            parent->childSignalInfo_.signo  = 17;
            parent->childSignalInfo_.code   = code;
            parent->childSignalInfo_.pid    = id_;
            parent->childSignalInfo_.uid    = uid_;
            parent->childSignalInfo_.status = status;
        }
        parent->notifyChildStateChange();
        parent->deliverSignal(17);
    };
    if (signal == static_cast<uint32_t>(Signal::Continue)) {
        bool changed;
        {
            InterruptSpinLockGuard guard(lock_);
            changed  = stopped_;
            stopped_ = false;
            if (changed) continuedPending_ = true;
            for (Thread* thread = threads_; thread;
                 thread         = thread->getTaskNext()) {
                if (memoryFrozen() ||
                    thread->getState() != ThreadState::Stopped)
                    continue;
                thread->setState(thread->getStateBeforeStop());
                if (thread->getState() == ThreadState::Ready ||
                    thread->getState() == ThreadState::Running) {
                    thread->setState(ThreadState::Ready);
                    task::enqueueThread(thread);
                }
            }
        }
        // Resume is unconditional even if delivery of a SIGCONT handler is
        // masked or ignored.
        if (changed) notifyState(6, signal);
    }
    SignalAction action{};
    bool queued = false;
    {
        InterruptSpinLockGuard guard(lock_);
        action = signalActions_[signal];
        if (action.getHandler() == signalIgnore) return true;
        bool allBlocked = true;
        for (Thread* thread = threads_; thread != nullptr;
             thread         = thread->getTaskNext()) {
            if (thread->getState() != ThreadState::Terminated &&
                (thread->getSignalMask() & (1ULL << (signal - 1))) == 0) {
                allBlocked = false;
                break;
            }
        }
        queued = signal != static_cast<uint32_t>(Signal::Kill) &&
                 signal != static_cast<uint32_t>(Signal::Stop) &&
                 (action.getHandler() != signalDefault || allBlocked);
        if (queued) {
            pendingSignals_ |= 1ULL << (signal - 1);
            for (Thread* thread = threads_; thread;
                 thread         = thread->getTaskNext()) {
                if (thread != currentThread() &&
                    thread->getState() != ThreadState::Terminated &&
                    (thread->getSignalMask() & (1ULL << (signal - 1))) == 0)
                    Endpoint::interruptThread(thread);
            }
        }
    }
    // Pending bits coalesce repeated ordinary signals. At this point eligible
    // threads have been interrupted; actual handler installation happens only
    // when a selected thread is returning to user mode.
    if (queued) return true;
    if (signal == static_cast<uint32_t>(Signal::WindowChanged) ||
        signal == static_cast<uint32_t>(Signal::Child) ||
        signal == static_cast<uint32_t>(Signal::Urgent))
        return true;
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
                    thread->getState() == ThreadState::Stopped)
                    continue;
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
    // Preserve the first fatal reason before terminating siblings; cleanup and
    // core publication may finish later in different execution contexts.
    recordSignalExit(signal);
    terminateOtherThreads(-static_cast<int>(signal));
    if (this == task::getCurrent()) {
        exitCurrentThread(-static_cast<int>(signal));
    }
    destroy();
    return true;
}

// Most recently recorded child event, copied under the parent task lock.
OsSignalInfo Task::childSignalInfo() {
    InterruptSpinLockGuard guard(lock_);
    return childSignalInfo_;
}

// Supported signal-number range; ordinary pending representation is 64 bits.
bool isValidSignal(uint32_t signal) {
    return signal >= 1 && signal <= maximumSignal;
}

// Resolve with a retained task reference and authorize from the actual sender
// identity. Managed services require their separate supervision control path;
// signal zero only checks that an authorized target is alive.
bool sendSignalToTask(uint64_t taskId, uint32_t signal,
                      Task* authenticatedCaller) {
    auto task = task::find(taskId);
    Task* caller =
        authenticatedCaller ? authenticatedCaller : task::getCurrent();
    if (task && signal && task->managedService()) return false;
    if (task && caller && caller->effectiveUid() != 0 &&
        caller->uid() != task->uid() && caller->effectiveUid() != task->uid())
        return false;
    return task != nullptr &&
           (signal == 0 ? !task->isTerminated() : task->deliverSignal(signal));
}

}  // namespace kernel::task
