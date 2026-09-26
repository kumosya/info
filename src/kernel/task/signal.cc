#include "kernel/signal.h"

#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"

namespace {

constexpr uint64_t signalFrameMagic = 0x5349474652414d45ULL;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;

struct SignalFrame {
    uint64_t magic;
    uint64_t oldMask;
    kernel::SyscallContext context;
};

}  // namespace

namespace kernel {

bool deliverPendingSignal(SyscallContext* context) {
    Process* process = getCurrentProcess();
    Thread* thread = getCurrentThread();
    if (context == nullptr || process == nullptr || thread == nullptr) {
        return false;
    }
    SignalAction action{};
    const uint32_t signal =
        process->takePendingSignal(thread, ~0ULL, action);
    if (signal == 0) return false;
    if (action.handler <= signalIgnore || action.restorer == 0) return false;

    SignalFrame frame{signalFrameMagic, thread->signalMask, *context};
    uint64_t frameAddress =
        (context->userRsp - sizeof(SignalFrame)) & ~0xfULL;
    const uint64_t handlerStack = frameAddress - sizeof(uint64_t);
    if (frameAddress >= maximumUserAddress || handlerStack >= maximumUserAddress ||
        !copyToUser(reinterpret_cast<void*>(frameAddress), &frame,
                    sizeof(frame)) ||
        !copyToUser(reinterpret_cast<void*>(handlerStack), &action.restorer,
                    sizeof(action.restorer))) {
        return false;
    }
    thread->signalMask |= action.mask;
    if ((action.flags & signalFlagNoDefer) == 0) {
        thread->signalMask |= 1ULL << (signal - 1);
    }
    thread->signalMask &= ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
                            (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
    context->userRip = action.handler;
    context->userRsp = handlerStack;
    context->rdi = signal;
    context->rsi = 0;
    context->rdx = 0;
    return true;
}

bool restoreSignalFrame(SyscallContext* context, uint64_t userFrame) {
    Thread* thread = getCurrentThread();
    SignalFrame frame{};
    if (context == nullptr || thread == nullptr ||
        !copyFromUser(&frame, reinterpret_cast<const void*>(userFrame),
                      sizeof(frame)) || frame.magic != signalFrameMagic ||
        frame.context.userRip >= maximumUserAddress ||
        frame.context.userRsp >= maximumUserAddress) {
        return false;
    }
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    frame.context.userRflags =
        (frame.context.userRflags & allowedFlags) | 0x202;
    thread->signalMask = frame.oldMask &
        ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
          (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
    *context = frame.context;
    return true;
}

}  // namespace kernel
