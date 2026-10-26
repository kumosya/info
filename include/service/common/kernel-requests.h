#include "service/common/records.h"
#pragma once
#include <cstdint>
#include "service/common/identity.h"

namespace os {
constexpr uint32_t maximumSupplementaryGroups = 32;
constexpr uint32_t spawnFlagNewSession = 1U, spawnFlagManagedService = 2U;
constexpr uint64_t execRuntimeStateAddress = 0x30000;
constexpr uint64_t taskCapabilityDisplay = 4, taskCapabilityUsers = 8;

constexpr uint32_t maximumStorageDevices = 32, maximumStorageTransferBlocks = 7, storageIdentityWord = 6;
enum class HandleOperation : uint64_t { Close, Duplicate, RestrictRights, SetFlags, GetState, ObjectIdentity, References, DestroyEndpoint, RequestActive };
constexpr uint32_t ipcHandleRestrictRights = 1, ipcHandleMove = 2;
constexpr uint32_t handleFlagNoInherit = 1, handleFlagCloseOnExec = 2;
constexpr uint32_t handleFlagsMask = 3;
constexpr uint64_t ipcInfiniteDeadline = UINT64_MAX;
enum class PciOperation : uint32_t { Enumerate = 1, ReadConfig, WriteConfig };
enum class IpcOperation : uint64_t { CreateEndpoint, GetCoreService, Handle };
enum class CoreServiceKind : uint64_t { Pipe, MessageQueue, Memory, Device, System, Task, Pci, Ipc, Count };
enum class PipeOperation : uint64_t { Create, Read, Write, CreateChannel, Identity, Nonblocking, Eof, Acknowledge };
inline constexpr uint64_t serviceReplayMagic = 0x5356435250433031ULL;
enum class MessageQueueOperation : uint64_t { Open, Unlink, Send, Receive, Attributes, Flags, Notify };
enum class MemoryOperation : uint64_t { Open = 0, Unlink = 1, Resize = 2, AllocatePages = 4, PageInfo = 6, Usage = 7, ReclaimCaches = 8, BindPager = 11, PageState = 12, RangeAvailable = 14, Freeze, Thaw, FrozenNext, MappingNext, Map, Unmap, Protect, SharedInfo, AnonymousShared, UnlinkSharedHandle };
enum class DeviceOperation : uint64_t { MapMmio, AllocateDma, BindIrq, AcknowledgeIrq, Framebuffer, InitializeInput, ReadInput, BootImageInfo, BootImageRead, BootImageWrite, ClaimDevice, StopDevice, FrameInfo, CreateDma, MapBar };
enum class SystemOperation : uint64_t { MonotonicTime, WallClockTime, ClockResolution, Info, Log };

enum class TaskOperation : uint64_t {
    State, Info, Terminate, Identity, Query, Enumerate, SetCredentials,
    SendSignal, SendThreadSignal, CreateSession, Identifiers, SetGroup,
    SignalAction, SignalMask, PendingSignals, TakeSignal,
    SpawnProgram, SpawnImage, WaitChild, WaitThread, ThreadIdentity, SignalReturn,
    ControlSignal, ControlTerminate, SetTerminalForeground,
    SignalStack, CoreLimit, ExitInfo, CoreConfigure, CoreAcquire, CoreRead, CoreFinish, ImageMapping, ProbeMemory, SignalGeneration, Auxiliary,
    Priority, ResourceLimit, ThreadInfo, Credentials, WaitId,
    RecoveryAcquire = 64, RecoveryPrepare, RecoveryCommit, RecoveryPut, RecoveryGet, RecoveryDrop, RecoveryList,
    Authentication, AuthenticateSession,
    RecoveryAppend = 80, RecoveryReadLog, RecoveryGeneration,
    ExitTask = 96, CreateThread, ExitThread, Yield, Sleep, Fork, ReplaceImage, StandardHandle, OpenTask = 112, CorePolicyQuery
};


}

#define OS_MAP_ANONYMOUS 0U
#define OS_MAP_PAGE_OBJECT 0x100U
#define OS_MAP_SHARED_OBJECT 0x200U
#define OS_PROTECT_COPY_ON_WRITE 4U
