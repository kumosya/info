#include "kernel/task.h"
#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/syscall.h"
#include "kernel/time.h"
#include "kernel/serial.h"
#include "kernel/page.h"

namespace {
constexpr uint64_t signalFrameMagic = 0x5349474652414d46ULL;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
constexpr uint64_t unmaskable = (1ULL << 8) | (1ULL << 18);
struct alignas(16) SignalFrame {
    uint64_t magic, previous;
    OsUserContext user;
    OsSignalInfo info;
};
bool userAddress(uint64_t address) { return address >= 0x10000 && address < maximumUserAddress; }
bool onStack(const OsSignalStack& stack, uint64_t rsp) {
    const auto base = reinterpret_cast<uint64_t>(stack.pointer);
    return stack.size && rsp >= base && rsp - base < stack.size;
}
void interruptToSyscall(const kernel::interrupt::Context& in, kernel::SyscallContext& out) {
    static_assert(offsetof(kernel::interrupt::Context, vector) == offsetof(kernel::SyscallContext, userRip));
    auto* destination = reinterpret_cast<uint8_t*>(&out);
    const auto* source = reinterpret_cast<const uint8_t*>(&in);
    for (size_t i = 0; i < 15 * sizeof(uint64_t); ++i) destination[i] = source[i];
    out.userRip = in.rip; out.userRsp = in.rsp; out.userRflags = in.rflags;
}
void syscallToInterrupt(const kernel::SyscallContext& in, kernel::interrupt::Context& out) {
    auto* destination = reinterpret_cast<uint8_t*>(&out);
    const auto* source = reinterpret_cast<const uint8_t*>(&in);
    for (size_t i = 0; i < 15 * sizeof(uint64_t); ++i) destination[i] = source[i];
    out.rip = in.userRip; out.rsp = in.userRsp; out.rflags = in.userRflags;
}
bool install(kernel::SyscallContext& context, uint32_t signo,
             const kernel::SignalAction& action, const kernel::FaultRecord* fault) {
    using namespace kernel;
    Thread& thread = *Thread::getCurrent();
    if (thread.signalDepth >= 8 || !userAddress(action.getHandler()) || !userAddress(action.getRestorer())) return false;
    {
        Task* owner = task::getCurrent();
        PreemptionSpinLockGuard guard(owner->addressSpace().lock());
        const uint64_t entries[] = {action.getHandler(), action.getRestorer()};
        for (uint64_t entry : entries) {
            uint64_t flags = 0;
            if (!memory::getPhysicalAddressInAddressSpace(owner->addressSpace().rootPhysicalAddress(), entry, &flags) ||
                !(flags & pageUser) || (flags & pageNoExecute)) return false;
        }
    }
    SignalFrame frame{};
    frame.magic = signalFrameMagic; frame.previous = thread.activeSignalFrame;
    frame.user.mask = thread.getSignalMask();
    frame.user.stack = thread.signalStack;
    frame.user.stack.flags = thread.signalStack.size ? (onStack(thread.signalStack, context.userRsp) ? 1 : 0) : 2;
    signal::toMachineContext(context, frame.user.machine);
    thread.saveFloatingPointState();
    for (size_t i = 0; i < 512; ++i) reinterpret_cast<uint8_t*>(frame.user.fpstate)[i] = thread.fpState()[i];
    frame.info.signo = signo;
    if (signo == static_cast<uint32_t>(Signal::Child)) frame.info = task::getCurrent()->childSignalInfo();
    if (fault) {
        frame.info.code = fault->code;
        frame.info.address = fault->addressValid ? reinterpret_cast<void*>(fault->address) : nullptr;
        frame.user.machine.registers[OS_REG_ERR] = fault->error;
        frame.user.machine.registers[OS_REG_TRAPNO] = fault->vector;
        frame.user.machine.registers[OS_REG_CR2] = fault->addressValid ? fault->address : 0;
    }
    uint64_t top = context.userRsp;
    if ((action.getFlags() & signalFlagOnStack) && thread.signalStack.size && !onStack(thread.signalStack, top))
        top = reinterpret_cast<uint64_t>(thread.signalStack.pointer) + thread.signalStack.size;
    if (top < sizeof(frame) + 136 || top >= maximumUserAddress) return false;
    // Preserve the interrupted ABI's 128-byte red zone, including on nested delivery.
    const uint64_t address = (top - 128 - sizeof(frame)) & ~15ULL;
    const uint64_t stack = address - 8;
    if (onStack(thread.signalStack, top - 1) && stack < reinterpret_cast<uint64_t>(thread.signalStack.pointer)) return false;
    frame.user.machine.fpregs = reinterpret_cast<void*>(address + offsetof(SignalFrame, user) + offsetof(OsUserContext, fpstate));
    const uint64_t restorer = action.getRestorer();
    if (!syscall::copyToUser(reinterpret_cast<void*>(address), &frame, sizeof(frame)) ||
        !syscall::copyToUser(reinterpret_cast<void*>(stack), &restorer, sizeof(restorer))) return false;
    thread.activeSignalFrame = address; ++thread.signalDepth; ++thread.signalGeneration;
    thread.setSignalMask((frame.user.mask | action.getMask() |
        ((action.getFlags() & signalFlagNoDefer) ? 0 : (1ULL << (signo - 1)))) & ~unmaskable);
    context.userRip = action.getHandler(); context.userRsp = stack;
    context.userRflags &= ~((1ULL << 8) | (1ULL << 10) | (1ULL << 18));
    context.rdi = signo;
    context.rsi = address + offsetof(SignalFrame, info);
    context.rdx = address + offsetof(SignalFrame, user);
    // A handler must not inherit a pending unmasked FP exception.
    thread.resetFloatingPointState(); thread.restoreFloatingPointState();
    return true;
}
}

namespace kernel::signal {
void toMachineContext(const SyscallContext& s, OsMachineContext& target) {
    auto* r = target.registers;
    r[0]=s.r8; r[1]=s.r9; r[2]=s.r10; r[3]=s.r11;
    r[4]=s.r12; r[5]=s.r13; r[6]=s.r14; r[7]=s.r15;
    r[8]=s.rdi; r[9]=s.rsi; r[10]=s.rbp; r[11]=s.rbx;
    r[12]=s.rdx; r[13]=s.rax; r[14]=s.rcx;
    r[15]=s.userRsp; r[16]=s.userRip; r[17]=s.userRflags; r[18]=0x23;
}
void fromMachineContext(const OsMachineContext& source, SyscallContext& s) {
    const auto* r = source.registers;
    s.r8=r[0]; s.r9=r[1]; s.r10=r[2]; s.r11=r[3];
    s.r12=r[4]; s.r13=r[5]; s.r14=r[6]; s.r15=r[7];
    s.rdi=r[8]; s.rsi=r[9]; s.rbp=r[10]; s.rbx=r[11];
    s.rdx=r[12]; s.rax=r[13]; s.rcx=r[14];
    s.userRsp=r[15]; s.userRip=r[16]; s.userRflags=r[17];
}
bool hasCoreDefault(uint32_t s) {
    return s == 3 || s == 4 || s == 5 || s == 6 || s == 7 || s == 8 || s == 11 || s == 24 || s == 25 || s == 31;
}
[[noreturn]] void fatal(uint32_t signo, const FaultRecord* fault) {
    Task* task = task::getCurrent();
    if (!task || !Thread::getCurrent()) cpu::halt();
    serial::serialWrite("[SIGNAL] fatal pid="); serial::serialWriteDecimal(task->id());
    serial::serialWrite(" signal="); serial::serialWriteDecimal(signo);
    if (fault) {
        serial::serialWrite(" vector="); serial::serialWriteDecimal(fault->vector);
        serial::serialWrite(" rip="); serial::serialWriteHex(fault->rip);
        serial::serialWrite(" rsp="); serial::serialWriteHex(fault->rsp);
    }
    serial::serialWrite("\n");
    task->recordSignalExit(signo, fault);
    task::exitCurrent(-static_cast<int>(signo));
}
bool deliverPendingSignal(SyscallContext* context) {
    Task* task = task::getCurrent(); Thread* thread = Thread::getCurrent();
    if (!context || !task || !thread) return false;
    for (uint32_t n = 0; n < maximumSignal; ++n) {
        SignalAction action{};
        const uint32_t signo = task->takePendingSignal(thread, ~0ULL, action);
        if (!signo) return false;
        if (action.getHandler() == signalIgnore) continue;
        if (action.getHandler() == signalDefault) { task->deliverSignal(signo); continue; }
        if (!install(*context, signo, action, nullptr)) fatal(static_cast<uint32_t>(Signal::SegmentationViolation));
        return true;
    }
    return false;
}
bool restoreSignalFrame(SyscallContext* context, uint64_t address) {
    Thread* thread = Thread::getCurrent(); SignalFrame frame{};
    if (!context || !thread || !thread->signalDepth || address != thread->activeSignalFrame ||
        !syscall::copyFromUser(&frame, reinterpret_cast<void*>(address), sizeof(frame)) || frame.magic != signalFrameMagic)
        return false;
    SyscallContext restored{}; fromMachineContext(frame.user.machine, restored);
    if (!userAddress(restored.userRip) || !userAddress(restored.userRsp) ||
        frame.user.machine.registers[OS_REG_CSGSFS] != 0x23 ||
        frame.user.machine.fpregs != reinterpret_cast<void*>(address + offsetof(SignalFrame, user) + offsetof(OsUserContext, fpstate))) return false;
    alignas(16) uint8_t live[512];
    asm volatile("fxsave64 %0" : "=m"(live) : : "memory");
    const uint32_t mask = *reinterpret_cast<uint32_t*>(live + 28) ? *reinterpret_cast<uint32_t*>(live + 28) : 0xffbf;
    const auto* fp = reinterpret_cast<const uint8_t*>(frame.user.fpstate);
    if (*reinterpret_cast<const uint32_t*>(fp + 24) & ~mask) return false;
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    restored.userRflags = (restored.userRflags & allowedFlags) | 0x202;
    for (size_t i = 0; i < 512; ++i) thread->fpState()[i] = fp[i];
    thread->restoreFloatingPointState();
    thread->setSignalMask(frame.user.mask & ~unmaskable);
    thread->activeSignalFrame = frame.previous; --thread->signalDepth;
    *context = restored;
    return true;
}
void raiseException(interrupt::Context* context, uint32_t signo, int code,
                    uint64_t address, bool valid, FaultAccess access, uint64_t debugStatus) {
    Thread* thread = Thread::getCurrent(); Task* task = task::getCurrent();
    if (!thread || !task) cpu::halt();
    SyscallContext saved{}; interruptToSyscall(*context, saved);
    thread->lastUserContext = saved; thread->hasUserContext = true;
    FaultRecord fault{task->id(), thread->getId(), task->startedNanoseconds(), time::getMonotonicTime(),
        context->vector, context->errorCode, context->rip, context->rsp, address, debugStatus, signo, code, access, valid};
    fault.origin = (context->cs & 3) == 3 ? FaultOrigin::UserException : FaultOrigin::ServiceException;
    thread->lastFault = fault;
    SignalAction action{}; task->getSignalAction(signo, action);
    if ((thread->getSignalMask() & (1ULL << (signo - 1))) || action.getHandler() <= signalIgnore)
        fatal(signo, &fault);
    if (action.getFlags() & signalFlagResetHand) task->setSignalAction(signo, {}, nullptr);
    if (!install(saved, signo, action, &fault)) fatal(signo, &fault);
    syscallToInterrupt(saved, *context);
}
void returnToUser(interrupt::Context* context) {
    if (!context || (context->cs & 3) != 3) return;
    Thread* thread = Thread::getCurrent();
    if (!thread || thread->getType() != ThreadType::User) return;
    SyscallContext saved{}; interruptToSyscall(*context, saved);
    thread->lastUserContext = saved; thread->hasUserContext = true;
    const auto flags = interrupt::disable(); interrupt::enable();
    deliverPendingSignal(&saved);
    interrupt::restore(flags);
    syscallToInterrupt(saved, *context);
}
void captureUserContext(const interrupt::Context* context) {
    Thread* thread = Thread::getCurrent();
    if (!thread || !context || (context->cs & 3) != 3) return;
    interruptToSyscall(*context, thread->lastUserContext);
    thread->hasUserContext = true;
}
}
