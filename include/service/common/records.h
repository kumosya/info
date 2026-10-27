#pragma once
#include <os/device.h>
#include <os/input.h>
#include <os/storage.h>
#include <os/task.h>
namespace os::wire {
struct TaskInfo {
    uint64_t id;
    uint32_t state;
    int32_t exitCode;
    char name[48];
    uint32_t uid, gid, effectiveUid, effectiveGid;
    uint64_t parentId;
    uint32_t trustedDeviceService;
    uint32_t serviceRole;
    uint64_t taskGroupId, sessionId;
    uint64_t terminalForegroundGroup;
    uint64_t startedNanoseconds, cpuNanoseconds;
    uint32_t terminalKind, terminalIndex;
    uint32_t commandLineSize;
    uint32_t managedService;
    char commandLine[384];
    uint64_t mainThreadId;
    int32_t nice;
    uint32_t threadCount;
};
struct ThreadInfo {
    uint64_t id, taskId, cpuNanoseconds;
    int32_t nice;
    uint32_t state;
};
struct CredentialInfo {
    uint32_t uid, effectiveUid, savedUid, gid, effectiveGid, savedGid;
    uint32_t groupCount;
    uint32_t groups[32];
};
struct PageObjectInfo {
    uint64_t owner, size;
    uint32_t references;
    uint16_t node, order;
};
struct MemoryUsage {
    uint64_t total, free, owned, generation;
    uint32_t nodes, reserved;
};
struct SystemInfo {
    uint64_t uptimeNanoseconds;
    uint64_t totalMemoryBytes;
    uint64_t freeMemoryBytes;
    uint32_t onlineCpus;
    uint32_t reserved;
};
struct PciFunctionInfo {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t programmingInterface;
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t headerType;
    uint8_t reserved[3];
};
struct DmaBufferInfo {
    uint64_t handle;
    uint64_t virtualAddress;
    uint64_t deviceAddress;
    uint64_t size;
};
enum class PixelFormat : uint8_t { Unknown, Rgb, Bgr, Argb, Rgba, Bgra };
struct DisplayInfo {
    uint64_t address;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bitsPerPixel;
    PixelFormat format;
    uint8_t redPosition;
    uint8_t greenPosition;
    uint8_t bluePosition;
};
struct BlockDeviceInfo {
    uint64_t blockCount;
    uint32_t blockSize;
    uint32_t maxTransferBlocks;
    Storage::Device::Type type;
    bool readOnly;
};

struct StorageDeviceIdentity {
    uint32_t id;
    uint32_t major;
    uint32_t minor;
    uint32_t parentId;
    uint32_t partitionNumber;
    uint32_t reserved;
    uint64_t startBlock;
    char name[32];
    char partitionUuid[40];
    char hardwarePath[64];
};
static_assert(sizeof(os::wire::TaskInfo) == 552);
static_assert(sizeof(os::wire::ThreadInfo) == 32);
static_assert(sizeof(os::wire::PciFunctionInfo) == 16);
static_assert(sizeof(os::wire::DmaBufferInfo) == 32);
static_assert(sizeof(os::wire::DisplayInfo) == 40);
static_assert(sizeof(os::wire::MemoryUsage) == 40);
static_assert(sizeof(os::wire::PageObjectInfo) == 24);
static_assert(sizeof(os::wire::StorageDeviceIdentity) == 168);

struct SpawnImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
    uint64_t bootstrapHandle;
    uint64_t standardInputHandle;
    uint64_t standardOutputHandle;
    uint64_t standardErrorHandle;
    uint64_t capabilities;
    uint64_t parentTaskId;
    uint32_t argumentCount;
    uint32_t serviceRole;
    const char* const* arguments;
    uint32_t sharedImageCount;
    uint32_t flags;
    uint32_t terminalKind, terminalIndex;
    const struct SharedImageInfo* sharedImages;
};
struct SharedImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
};
struct ExecImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
    uint32_t argumentCount;
    uint32_t environmentCount;
    const char* const* arguments;
    const char* const* environment;
    const void* runtimeState;
    uint64_t runtimeStateSize;
    uint32_t sharedImageCount;
    uint32_t sharedReserved;
    const SharedImageInfo* sharedImages;
};
struct ExitInfo {
    uint32_t signal, coreState;
    uint64_t faultThread, vector, error, rip, rsp, address, started;
    uint32_t addressValid;
    int32_t signalCode;
};
struct MountInfo {
    char device[64];
    char target[64];
    char type[16];
    bool readOnly;
};
}  // namespace os::wire

namespace os::wire {
enum class CoreState : uint32_t {
    None,
    Requested,
    Capturing,
    Ready,
    Writing,
    Complete,
    Truncated,
    Failed,
    Skipped,
    TimedOut
};

struct TaskIdentifiers {
    uint64_t parentId;
    uint64_t taskGroupId;
    uint64_t sessionId;
};
struct SignalAction {
    uint64_t handler;
    uint64_t mask;
    uint64_t flags;
    uint64_t restorer;
};
struct ChildEvent {
    uint64_t id;
    uint32_t uid, code;
    int32_t status;
};
struct CorePolicy {
    uint64_t enabled, maximumBytes, maximumConcurrent, timeoutMilliseconds;
};
struct CoreTicket {
    uint64_t id, pid, started, bytes, deadline;
    uint32_t signal, truncated;
};
struct ImageMapping {
    uint64_t begin, end, offset;
    char path[192];
};

static_assert(sizeof(TaskIdentifiers) == 24);
static_assert(sizeof(SignalAction) == 32);
static_assert(sizeof(CorePolicy) == 32);
static_assert(sizeof(CoreTicket) == 48);
}  // namespace os::wire

namespace os::wire {
struct DirectoryEntry {
    uint64_t type;
    char name[256];
    uint64_t inode, nextOffset;
};
static_assert(sizeof(DirectoryEntry) == 280);
}  // namespace os::wire

namespace os::wire {
struct KeyEvent {
    Input::Key keyCode;
    uint32_t modifiers;
    uint8_t pressed;
    uint8_t reserved[3];
};
struct MouseEvent {
    int32_t deltaX, deltaY, wheel;
    uint32_t buttons;
};
struct InputEvent {
    Input::Event::Type type;
    uint32_t reserved;
    uint64_t timestamp;
    union {
        KeyEvent key;
        MouseEvent mouse;
    };
};
struct InputState {
    uint32_t modifiers, buttons;
    uint8_t keys[128];
};
static_assert(sizeof(InputEvent) == 32);
static_assert(sizeof(InputState) == 136);
}  // namespace os::wire

namespace os::wire {
struct ServiceStatus {
    char name[24]{}, owner[24]{}, executable[128]{};
    uint64_t pid{};
    Sys::Status::State state{};
    uint32_t enabled{}, desired{}, builtin{}, restarts{};
    int32_t exitCode{};
    char blockedBy[24]{};
};
static_assert(sizeof(wire::ServiceStatus) == 232);
}  // namespace os::wire
