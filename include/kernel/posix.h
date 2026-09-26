#pragma once

#include "kernel/syscall.h"

namespace kernel {

enum class PosixOperation : uint64_t {
    Fork,
    Exec,
    Wait,
    ThreadLifecycle,
    ThreadCancellation,
    SignalAction,
    RealtimeSignals,
    FileLinksAndMetadata,
    FileBackedMemory,
    SharedMemory,
    MessageQueue,
    NamedSemaphore,
    AsyncIo,
    Poll,
    Socket,
    Network,
    Timer,
    Scheduling,
    ResourceLimits,
    Credentials,
    Locale,
    Internationalization,
    RegularExpression,
    WordExpansion,
    DynamicLoading,
    Spawn,
    TerminalJobControl,
    Count
};

// These entry points reserve the kernel boundary for POSIX facilities whose
// backing objects or semantics do not exist yet. They must never report
// success until the corresponding subsystem is implemented.
SyscallError posixFork();
SyscallError posixExec();
SyscallError posixWait();
SyscallError posixThreadLifecycle();
SyscallError posixThreadCancellation();
SyscallError posixSignalAction();
SyscallError posixRealtimeSignals();
SyscallError posixFileLinksAndMetadata();
SyscallError posixFileBackedMemory();
SyscallError posixSharedMemory();
SyscallError posixMessageQueue();
SyscallError posixNamedSemaphore();
SyscallError posixAsyncIo();
SyscallError posixPoll();
SyscallError posixSocket();
SyscallError posixNetwork();
SyscallError posixTimer();
SyscallError posixScheduling();
SyscallError posixResourceLimits();
SyscallError posixCredentials();
SyscallError posixLocale();
SyscallError posixInternationalization();
SyscallError posixRegularExpression();
SyscallError posixWordExpansion();
SyscallError posixDynamicLoading();
SyscallError posixSpawn();
SyscallError posixTerminalJobControl();
SyscallError dispatchUnsupportedPosixOperation(PosixOperation operation);

}  // namespace kernel
