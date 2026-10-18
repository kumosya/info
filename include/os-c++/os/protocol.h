#ifndef _OS_PROTOCOL_H
#define _OS_PROTOCOL_H

#include <stdint.h>

namespace os {

constexpr uint32_t serviceNameCapacity = 24;

enum class RegistryRequest : uint64_t { Register = 1, Lookup = 2, Unregister = 3 };
constexpr uint64_t serviceAccessOwnerOnly = 1U;
enum class TaskRequest : uint64_t { Spawn = 1, Wait, Execute, List, Kill };
constexpr uint64_t taskExecuteHasStdin  = 1ULL << 40;
constexpr uint64_t taskExecuteHasStdout = 1ULL << 41;
constexpr uint64_t taskExecuteHasStderr = 1ULL << 42;
enum class DeviceRequest : uint64_t { List = 1, ListNode, ReadNode };
enum class PciServiceRequest : uint64_t { Enumerate = 1, ReadConfig, WriteConfig };
enum class TerminalRequest : uint64_t {
    Write = 0x300,
    Read,
    GetActive,
    Switch,
    GetInfo,
    SetForegroundTask,
    GetEcho,
    SetEcho
};
enum class TtyRequest : uint64_t {
    CreatePty = 1,
    GetAttributes,
    SetAttributes,
    GetWindowSize,
    SetWindowSize,
    SetForegroundGroup,
    SetNonblocking
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
    WriteRange
};
constexpr uint64_t storageCompletionNotification = 0x53544f52414745ULL;
constexpr uint64_t storageDeviceShift = 56;
constexpr uint64_t storageBlockMask = (1ULL << storageDeviceShift) - 1;

class StorageProtocol {
 public:
static constexpr uint64_t encodeStorageBlock(uint32_t deviceId, uint64_t block) {
    return (static_cast<uint64_t>(deviceId) << storageDeviceShift) |
           (block & storageBlockMask);
}
};
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
    WriteAt
};
constexpr uint64_t fsCompletionNotification = 0x4653434f4d504cULL;

constexpr const char* taskServiceName     = "task";
constexpr const char* fsServiceName       = "fs";
constexpr const char* deviceServiceName   = "device";
constexpr const char* pciServiceName      = "pci";
constexpr const char* storageServiceName  = "storage";
constexpr const char* terminalServiceName = "terminal";
constexpr const char* consoleServiceName  = "console";
constexpr const char* ttyServiceName      = "tty";

enum class FileType : uint64_t { None, File, Directory };

}  // namespace os

#endif
