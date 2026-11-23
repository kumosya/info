/* 内核和用户态服务的操作编号、通信常量及块请求编码。 */

#pragma once
#include "service/common/identity.h"
#include "service/common/records.h"

namespace services {

    constexpr uint32_t maximumSupplementaryGroups = 32;
    constexpr uint32_t spawnFlagNewSession = 1U, spawnFlagManagedService = 2U;
    constexpr uint64_t execRuntimeStateAddress = 0x30000;
    constexpr uint64_t taskCapabilityDisplay = 4, taskCapabilityUsers = 8;

    constexpr uint32_t maximumStorageDevices = 32,
                       maximumStorageTransferBlocks = 7,
                       storageIdentityWord = 6;
    enum class HandleOperation : uint64_t {
        Close,
        Duplicate,
        RestrictRights,
        SetFlags,
        GetState,
        ObjectIdentity,
        References,
        DestroyEndpoint,
        RequestActive
    };
    constexpr uint32_t ipcHandleRestrictRights = 1, ipcHandleMove = 2;
    constexpr uint32_t handleFlagNoInherit = 1, handleFlagCloseOnExec = 2;
    constexpr uint32_t handleFlagsMask = 3;
    constexpr uint64_t ipcInfiniteDeadline = UINT64_MAX;
    enum class PciOperation : uint32_t {
        Enumerate = 1,
        ReadConfig,
        WriteConfig
    };
    enum class IpcOperation : uint64_t {
        CreateEndpoint,
        GetCoreService,
        Handle
    };
    enum class CoreServiceKind : uint64_t {
        Pipe,
        MessageQueue,
        Memory,
        Device,
        System,
        Task,
        Pci,
        Ipc,
        Count
    };
    enum class PipeOperation : uint64_t {
        Create,
        Read,
        Write,
        CreateChannel,
        Identity,
        Nonblocking,
        Eof,
        Acknowledge,
        Readiness,
        EventPage
    };
    inline constexpr uint64_t serviceReplayMagic = 0x5356435250433031ULL;
    enum class MessageQueueOperation : uint64_t {
        Open,
        Unlink,
        Send,
        Receive,
        Attributes,
        Flags,
        Notify
    };
    enum class MemoryOperation : uint64_t {
        Open = 0,
        Unlink = 1,
        Resize = 2,
        AllocatePages = 4,
        PageInfo = 6,
        Usage = 7,
        ReclaimCaches = 8,
        BindPager = 11,
        PageState = 12,
        RangeAvailable = 14,
        Freeze,
        Thaw,
        FrozenNext,
        MappingNext,
        Map,
        Unmap,
        Protect,
        SharedInfo,
        AnonymousShared,
        UnlinkSharedHandle,
        TypedOpen,
        TypedAllocate,
        TypedInfo,
        LockRange,
        LockAll,
        LockState
    };
    enum class DeviceOperation : uint64_t {
        MapMmio,
        AllocateDma,
        BindIrq,
        AcknowledgeIrq,
        Framebuffer,
        InitializeInput,
        ReadInput,
        BootImageInfo,
        BootImageRead,
        BootImageWrite,
        ClaimDevice,
        StopDevice,
        FrameInfo,
        CreateDma,
        MapBar,
        PortIo
    };
    enum class SystemOperation : uint64_t {
        MonotonicTime,
        WallClockTime,
        ClockResolution,
        Info,
        Log,
        SetWallClockTime
    };

    enum class TaskOperation : uint64_t {
        State,
        Info,
        Terminate,
        Identity,
        Query,
        Enumerate,
        SetCredentials,
        SendSignal,
        SendThreadSignal,
        CreateSession,
        Identifiers,
        SetGroup,
        SignalAction,
        SignalMask,
        PendingSignals,
        TakeSignal,
        SpawnProgram,
        SpawnImage,
        WaitChild,
        WaitThread,
        ThreadIdentity,
        SignalReturn,
        ControlSignal,
        ControlTerminate,
        SetTerminalForeground,
        SignalStack,
        CoreLimit,
        ExitInfo,
        CoreConfigure,
        CoreAcquire,
        CoreRead,
        CoreFinish,
        ImageMapping,
        ProbeMemory,
        SignalGeneration,
        Auxiliary,
        Priority,
        ResourceLimit,
        ThreadInfo,
        Credentials,
        WaitId,
        RecoveryAcquire = 64,
        RecoveryPrepare,
        RecoveryCommit,
        RecoveryPut,
        RecoveryGet,
        RecoveryDrop,
        RecoveryList,
        Authentication,
        AuthenticateSession,
        RecoveryAppend = 80,
        RecoveryReadLog,
        RecoveryGeneration,
        ExitTask = 96,
        CreateThread,
        ExitThread,
        Yield,
        Sleep,
        Fork,
        ReplaceImage,
        StandardHandle,
        OpenTask = 112,
        CorePolicyQuery
    };

}  // namespace services

#define OS_MAP_ANONYMOUS 0U
#define OS_MAP_PAGE_OBJECT 0x100U
#define OS_MAP_SHARED_OBJECT 0x200U
#define OS_PROTECT_COPY_ON_WRITE 4U

namespace services {

    enum class ServiceOperation : uint64_t {
        List = 1,
        Status,
        Start,
        Stop,
        Restart,
        Enable,
        Disable
    };
    constexpr uint32_t serviceNameCapacity = 24;

    enum class RegistryRequest : uint64_t {
        Register = 1,
        Lookup = 2,
        Unregister = 3
    };
    constexpr uint64_t serviceControlMessage = 0x535643;
    constexpr uint64_t serviceAccessOwnerOnly = 1U;
    enum class TaskRequest : uint64_t {
        Wait = 2,
        Execute = 3,
        ExecuteService = 6
    };
    constexpr uint64_t taskExecuteHasStdin = 1ULL << 40;
    constexpr uint64_t taskExecuteHasStdout = 1ULL << 41;
    constexpr uint64_t taskExecuteHasStderr = 1ULL << 42;
    enum class DeviceRequest : uint64_t { List = 1, ListNode, ReadNode };
    enum class PciServiceRequest : uint64_t {
        Enumerate = 1,
        ReadConfig,
        WriteConfig,
        Query
    };
    enum class TerminalRequest : uint64_t {
        Write = 0x300,
        Read,
        GetActive,
        Switch,
        GetInfo,
        SetForegroundGroup,
        GetEcho,
        SetEcho,
        ResetSession,
        BootStatus,
        BootComplete,
        GetBootState,
        GetState = 0x310,
        Readiness
    };
    enum class TtyRequest : uint64_t {
        CreatePty = 1,
        GetAttributes,
        SetAttributes,
        GetWindowSize,
        SetWindowSize,
        SetForegroundGroup,
        SetNonblocking,
        GetNumber,
        GetForegroundGroup,
        DeviceControl
    };
    enum class StorageRequest : uint64_t {
        GetInfo = 0x100,
        ReadBlocks,
        WriteBlocks,
        Flush,
        ReadAsync,
        WriteAsync,
        GetCount,
        ReadRange,
        WriteRange,
        GetStatistics
    };
    constexpr uint64_t storageCompletionNotification = 0x53544f52414745ULL;
    constexpr uint64_t storageDeviceShift = 56;
    constexpr uint64_t storageBlockMask = (1ULL << storageDeviceShift) - 1;

    namespace storage_protocol {

        constexpr uint32_t statisticsWords = 7;

        constexpr uint32_t rangeLimit(uint32_t bytesPerBlock,
                                      uint32_t backendLimit) {
            if (!bytesPerBlock || bytesPerBlock % sizeof(uint64_t)) {
                return 0;
            }

            const uint32_t payloadLimit =
                (os::ipcDataWordCount - 2) * sizeof(uint64_t) / bytesPerBlock;
            return backendLimit < payloadLimit ? backendLimit : payloadLimit;
        }

        constexpr uint64_t encodeStorageBlock(uint32_t deviceId,
                                              uint64_t block) {
            return (static_cast<uint64_t>(deviceId) << storageDeviceShift) |
                   (block & storageBlockMask);
        }

        constexpr uint64_t ipcDeadlineAfter(uint64_t now, uint64_t budget) {
            constexpr uint64_t lastFinite = UINT64_MAX - 1;
            return now >= lastFinite || budget > lastFinite - now
                       ? lastFinite
                       : now + budget;
        }
    }  // namespace storage_protocol

    enum class FsRequest : uint64_t {
        Open = 1,
        Close,
        Read,
        Write,
        Seek,
        Stat,
        Create,
        Remove,
        MakeDirectory,
        ReadDirectory,
        Truncate,
        Mount,
        Unmount,
        ListMounts,
        Format,
        ReadAtAsync,
        WriteAtAsync,
        ReadDirectoryBatch,
        Link,
        ChangeMode,
        ChangeOwner,
        ChangeTimes,
        CloneDescriptors,
        Rename,
        CloneForChild,
        Fstat,
        ReadAt,
        WriteAt,
        DelegateMapping,
        ClaimMapping,
        Sync,
        ReleaseMapping,
        ListMappings,
        CancelMapping,
        DirOpen,
        DirClose,
        DirRead,
        DirCreate,
        DirRemove,
        DirRename,
        Ftruncate,
        RemoveFile,
        Fchmod,
        Fchown,
        Ftimes,
        ReadBuffer,
        SyncAll
    };
    constexpr uint64_t fsCompletionNotification = 0x4653434f4d504cULL;

    constexpr const char* taskServiceName = "task";
    constexpr const char* fsServiceName = "fs";
    constexpr const char* deviceServiceName = "device";
    constexpr const char* pciServiceName = "pci";
    constexpr const char* storageServiceName = "storage";
    constexpr const char* terminalServiceName = "terminal";
    constexpr const char* consoleServiceName = "console";
    constexpr const char* ttyServiceName = "tty";

    enum class InputRequest : uint64_t {
        GetEvent = 0x200,
        GetState,
        Subscribe,
        Unsubscribe
    };
    constexpr const char* inputServiceName = "input";
    constexpr uint64_t inputEventNotification = 0x494e505554;

    enum class MmOperation : uint64_t {
        MapAnonymous = 1,
        MapFile,
        MapShared,
        Unmap,
        Protect,
        Fault,
        Usage,
        Reclaim,
        SharedOpen,
        SharedUnlink,
        Fork,
        Sync,
        UnmapOwned,
        ProtectOwned,
        SyncOwned,
        Lock,
        Unlock,
        LockAll,
        UnlockAll,
        Advise,
        MemoryOffset
    };
    constexpr uint64_t mmFlagEager = 1, mmFlagShared = 2;

    enum class UserOperation : uint64_t {
        Lookup = 1,
        Login,
        Sudo,
        Password,
        Who,
        Add,
        BeginSession,
        AutoLogin,
        List,
        GraphicalLogin
    };
}
