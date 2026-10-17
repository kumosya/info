#include "kernel/signal.h"

#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace {

constexpr uint64_t signalFrameMagic   = 0x5349474652414d45ULL;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;

struct SignalFrame {
    uint64_t magic;
    uint64_t oldMask;
    kernel::SyscallContext context;
};

}  // namespace

namespace kernel::signal {

bool deliverPendingSignal(SyscallContext* context) {
    Task* task     = task::getCurrent();
    Thread* thread = Thread::getCurrent();
    if (context == nullptr || task == nullptr || thread == nullptr) {
        return false;
    }
    SignalAction action{};
    const uint32_t signal = task->takePendingSignal(thread, ~0ULL, action);
    if (signal == 0) return false;
    if (action.getHandler() <= signalIgnore || action.getRestorer() == 0)
        return false;

    SignalFrame frame{signalFrameMagic, thread->getSignalMask(), *context};
    uint64_t frameAddress = (context->userRsp - sizeof(SignalFrame)) & ~0xfULL;
    const uint64_t handlerStack = frameAddress - sizeof(uint64_t);
    if (frameAddress >= maximumUserAddress ||
        handlerStack >= maximumUserAddress ||
        !syscall::copyToUser(reinterpret_cast<void*>(frameAddress), &frame,
                             sizeof(frame)) ||
        !syscall::copyToUser(reinterpret_cast<void*>(handlerStack),
                             &action.accessRestorer(),
                             sizeof(action.getRestorer()))) {
        return false;
    }
    thread->setSignalMask(thread->getSignalMask() | (action.getMask()));
    if ((action.getFlags() & signalFlagNoDefer) == 0) {
        thread->setSignalMask(thread->getSignalMask() | (1ULL << (signal - 1)));
    }
    thread->setSignalMask(
        thread->getSignalMask() &
        (~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
           (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)))));
    context->userRip = action.getHandler();
    context->userRsp = handlerStack;
    context->rdi     = signal;
    context->rsi     = 0;
    context->rdx     = 0;
    return true;
}

bool restoreSignalFrame(SyscallContext* context, uint64_t userFrame) {
    Thread* thread = Thread::getCurrent();
    SignalFrame frame{};
    if (context == nullptr || thread == nullptr ||
        !syscall::copyFromUser(&frame, reinterpret_cast<const void*>(userFrame),
                               sizeof(frame)) ||
        frame.magic != signalFrameMagic ||
        frame.context.userRip >= maximumUserAddress ||
        frame.context.userRsp >= maximumUserAddress) {
        return false;
    }
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    frame.context.userRflags =
        (frame.context.userRflags & allowedFlags) | 0x202;
    thread->setSignalMask(
        frame.oldMask & ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
                          (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1))));
    *context = frame.context;
    return true;
}

}  // namespace kernel::signal
