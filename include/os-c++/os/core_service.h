#pragma once

#include "os/abi.h"

namespace os {
enum class BootstrapOperation : uint64_t { GetHandle, CreateEndpoint, GetCoreService };
enum class CoreServiceKind : uint64_t { Pipe, MessageQueue, Memory, Device, System, Task, Pci, Count };
enum class PipeOperation : uint64_t { Create, Read, Write, CreateChannel, Identity, Nonblocking, Eof };
enum class MessageQueueOperation : uint64_t { Open, Unlink, Send, Receive, Attributes, Flags, Notify };
enum class MemoryOperation : uint64_t { Open, Unlink, Resize, Map, AllocatePages, MapPages, PageInfo, Usage, ReclaimCaches, UnmapTarget, ProtectTarget, BindPager, PageState, MapSharedTarget, RangeAvailable };
enum class DeviceOperation : uint64_t { MapMmio, AllocateDma, BindIrq, AcknowledgeIrq, Framebuffer, InitializeInput, ReadInput };
enum class SystemOperation : uint64_t { MonotonicTime, WallClockTime, ClockResolution, Info, Log };

enum class TaskOperation : uint64_t {
    State, Info, Terminate, Identity, Query, Enumerate, SetCredentials,
    SendSignal, SendThreadSignal, CreateSession, Identifiers, SetGroup,
    SignalAction, SignalMask, PendingSignals, TakeSignal,
    SpawnProgram, SpawnImage, WaitChild, WaitThread, ThreadIdentity, SignalReturn
};

class CoreServiceClient {
    static uint64_t handles_[static_cast<uint64_t>(CoreServiceKind::Count)];
   public:
    static uint64_t openHandle(CoreServiceKind service);
    static int call(CoreServiceKind service, IpcMessage& request, IpcMessage& reply,
                    uint64_t deadline = ipcInfiniteDeadline);
    static int64_t result(CoreServiceKind service, IpcMessage& request, IpcMessage& reply);
};
}  // namespace os
