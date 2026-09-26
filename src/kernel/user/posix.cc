#include "kernel/posix.h"

namespace kernel {

namespace {

constexpr SyscallError notImplemented() {
    return SyscallError::NotSupported;
}

}  // namespace

SyscallError posixFork() { return notImplemented(); }
SyscallError posixExec() { return notImplemented(); }
SyscallError posixWait() { return notImplemented(); }
SyscallError posixThreadLifecycle() { return notImplemented(); }
SyscallError posixThreadCancellation() { return notImplemented(); }
SyscallError posixSignalAction() { return notImplemented(); }
SyscallError posixRealtimeSignals() { return notImplemented(); }
SyscallError posixFileLinksAndMetadata() { return notImplemented(); }
SyscallError posixFileBackedMemory() { return notImplemented(); }
SyscallError posixSharedMemory() { return notImplemented(); }
SyscallError posixMessageQueue() { return notImplemented(); }
SyscallError posixNamedSemaphore() { return notImplemented(); }
SyscallError posixAsyncIo() { return notImplemented(); }
SyscallError posixPoll() { return notImplemented(); }
SyscallError posixSocket() { return notImplemented(); }
SyscallError posixNetwork() { return notImplemented(); }
SyscallError posixTimer() { return notImplemented(); }
SyscallError posixScheduling() { return notImplemented(); }
SyscallError posixResourceLimits() { return notImplemented(); }
SyscallError posixCredentials() { return notImplemented(); }
SyscallError posixLocale() { return notImplemented(); }
SyscallError posixInternationalization() { return notImplemented(); }
SyscallError posixRegularExpression() { return notImplemented(); }
SyscallError posixWordExpansion() { return notImplemented(); }
SyscallError posixDynamicLoading() { return notImplemented(); }
SyscallError posixSpawn() { return notImplemented(); }
SyscallError posixTerminalJobControl() { return notImplemented(); }

SyscallError dispatchUnsupportedPosixOperation(PosixOperation operation) {
    using Handler = SyscallError (*)();
    constexpr Handler handlers[] = {
        posixFork,
        posixExec,
        posixWait,
        posixThreadLifecycle,
        posixThreadCancellation,
        posixSignalAction,
        posixRealtimeSignals,
        posixFileLinksAndMetadata,
        posixFileBackedMemory,
        posixSharedMemory,
        posixMessageQueue,
        posixNamedSemaphore,
        posixAsyncIo,
        posixPoll,
        posixSocket,
        posixNetwork,
        posixTimer,
        posixScheduling,
        posixResourceLimits,
        posixCredentials,
        posixLocale,
        posixInternationalization,
        posixRegularExpression,
        posixWordExpansion,
        posixDynamicLoading,
        posixSpawn,
        posixTerminalJobControl,
    };
    static_assert(sizeof(handlers) / sizeof(handlers[0]) ==
                  static_cast<uint64_t>(PosixOperation::Count));
    const uint64_t index = static_cast<uint64_t>(operation);
    return index < static_cast<uint64_t>(PosixOperation::Count)
               ? handlers[index]()
               : SyscallError::InvalidArgument;
}

}  // namespace kernel
