/*
 * 服务通信数据布局。
 */

#pragma once
#include <os/device.h>
#include <os/input.h>
#include <os/storage.h>
#include <os/task.h>

namespace services::wire {

    /* 保存任务的通信字段；字段顺序和宽度属于接口约定。 */
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

    /* 保存线程的通信字段；字段顺序和宽度属于接口约定。 */
    struct ThreadInfo {
            uint64_t id, taskId, cpuNanoseconds;
            int32_t nice;
            uint32_t state;
    };

    /* 保存用户及组凭据的通信字段；字段顺序和宽度属于接口约定。 */
    struct CredentialInfo {
            uint32_t uid, effectiveUid, savedUid, gid, effectiveGid, savedGid;
            uint32_t groupCount;
            uint32_t groups[32];
    };

    /* 保存页对象的通信字段；字段顺序和宽度属于接口约定。 */
    struct PageObjectInfo {
            uint64_t owner, size;
            uint32_t references;
            uint16_t node, order;
    };

    /* 保存内存用量的通信字段；字段顺序和宽度属于接口约定。 */
    struct MemoryUsage {
            uint64_t total, free, owned, generation;
            uint32_t nodes, reserved;
    };

    /* 保存系统的通信字段；字段顺序和宽度属于接口约定。 */
    struct SystemInfo {
            uint64_t uptimeNanoseconds;
            uint64_t totalMemoryBytes;
            uint64_t freeMemoryBytes;
            uint32_t onlineCpus;
            uint32_t architecture;
    };

    /* 保存PCI 功能的通信字段；字段顺序和宽度属于接口约定。 */
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

    /* 保存DMA 缓冲区的通信字段；字段顺序和宽度属于接口约定。 */
    struct DmaBufferInfo {
            uint64_t handle;
            uint64_t virtualAddress;
            uint64_t deviceAddress;
            uint64_t size;
    };
    enum class PixelFormat : uint8_t { Unknown, Rgb, Bgr, Argb, Rgba, Bgra };

    /* 保存显示设备的通信字段；字段顺序和宽度属于接口约定。 */
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

    /* 保存块设备的通信字段；字段顺序和宽度属于接口约定。 */
    struct BlockDeviceInfo {
            uint64_t blockCount;
            uint32_t blockSize;
            uint32_t maxTransferBlocks;
            os::Storage::Device::Type type;
            bool readOnly;
    };

    /* 保存存储设备身份的通信字段；字段顺序和宽度属于接口约定。 */
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

    static_assert(sizeof(services::wire::TaskInfo) == 552);
    static_assert(sizeof(services::wire::ThreadInfo) == 32);
    static_assert(sizeof(services::wire::PciFunctionInfo) == 16);
    static_assert(sizeof(services::wire::DmaBufferInfo) == 32);
    static_assert(sizeof(services::wire::DisplayInfo) == 40);
    static_assert(sizeof(services::wire::MemoryUsage) == 40);
    static_assert(sizeof(services::wire::PageObjectInfo) == 24);
    static_assert(sizeof(services::wire::StorageDeviceIdentity) == 168);

    /* 保存程序启动映像的通信字段；字段顺序和宽度属于接口约定。 */
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

    /* 保存共享程序映像的通信字段；字段顺序和宽度属于接口约定。 */
    struct SharedImageInfo {
            const void* image;
            uint64_t imageSize;
            const char* name;
    };

    /* 保存程序替换映像的通信字段；字段顺序和宽度属于接口约定。 */
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

    /* 保存退出原因的通信字段；字段顺序和宽度属于接口约定。 */
    struct ExitInfo {
            uint32_t signal, coreState;
            uint64_t faultThread, vector, error, rip, rsp, address, started;
            uint32_t addressValid;
            int32_t signalCode;
    };

    /* 保存挂载点的通信字段；字段顺序和宽度属于接口约定。 */
    struct MountInfo {
            char device[64];
            char target[64];
            char type[16];
            bool readOnly;
    };

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

    /* 保存进程组和会话编号的通信字段；字段顺序和宽度属于接口约定。 */
    struct TaskIdentifiers {
            uint64_t parentId;
            uint64_t taskGroupId;
            uint64_t sessionId;
    };

    /* 保存信号处理入口、标志和处理期间需要屏蔽的信号集合。 */
    struct SignalAction {
            uint64_t handler;
            uint64_t mask;
            uint64_t flags;
            uint64_t restorer;
    };

    /* 保存子任务状态事件的通信字段；字段顺序和宽度属于接口约定。 */
    struct ChildEvent {
            uint64_t id;
            uint32_t uid, code;
            int32_t status;
    };

    /* 保存核心转储策略的通信字段；字段顺序和宽度属于接口约定。 */
    struct CorePolicy {
            uint64_t enabled, maximumBytes, maximumConcurrent,
                timeoutMilliseconds;
    };

    /* 保存核心转储读取票据的通信字段；字段顺序和宽度属于接口约定。 */
    struct CoreTicket {
            uint64_t id, pid, started, bytes, deadline;
            uint32_t signal, truncated;
    };

    /* 保存程序映像映射的通信字段；字段顺序和宽度属于接口约定。 */
    struct ImageMapping {
            uint64_t begin, end, offset;
            char path[192];
    };

    static_assert(sizeof(TaskIdentifiers) == 24);
    static_assert(sizeof(SignalAction) == 32);
    static_assert(sizeof(CorePolicy) == 32);
    static_assert(sizeof(CoreTicket) == 48);

    /* 保存目录遍历返回的名称、类型和对象标识。 */
    struct DirectoryEntry {
            uint64_t type;
            char name[256];
            uint64_t inode, nextOffset;
    };

    static_assert(sizeof(DirectoryEntry) == 280);

    /* 保存键盘事件的通信字段；字段顺序和宽度属于接口约定。 */
    struct KeyEvent {
            os::Input::Key keyCode;
            uint32_t modifiers;
            uint8_t pressed;
            uint8_t reserved[3];
    };

    /* 保存鼠标事件的通信字段；字段顺序和宽度属于接口约定。 */
    struct MouseEvent {
            int32_t deltaX, deltaY, wheel;
            uint32_t buttons;
    };

    /* 保存输入事件的通信字段；字段顺序和宽度属于接口约定。 */
    struct InputEvent {
            os::Input::Event::Type type;
            uint32_t reserved;
            uint64_t timestamp;

            union {
                    KeyEvent key;
                    MouseEvent mouse;
            };
    };

    /* 保存输入状态的通信字段；字段顺序和宽度属于接口约定。 */
    struct InputState {
            uint32_t modifiers, buttons;
            uint8_t keys[128];
    };

    static_assert(sizeof(InputEvent) == 32);
    static_assert(sizeof(InputState) == 136);

    /* 保存服务状态的通信字段；字段顺序和宽度属于接口约定。 */
    struct ServiceStatus {
            char name[24]{}, owner[24]{}, executable[128]{};
            uint64_t pid{};
            os::Sys::Status::State state{};
            uint32_t enabled{}, desired{}, builtin{}, restarts{};
            int32_t exitCode{};
            char blockedBy[24]{};
    };

    static_assert(sizeof(services::wire::ServiceStatus) == 232);

    // Fixed users-service message layout. Public user objects are encoded
    // explicitly.

    /* 保存用户编号、组编号、名称、主目录及登录程序等账户资料。 */
    struct UserProfile {
            uint32_t uid, gid;
            char name[32], home[96], shell[64];
    };

    /* 保存认证请求携带的账户名与凭据字段，使用时应遵守消息边界。 */
    struct UserCredentials {
            uint32_t id;
            char name[32], password[128], replacement[128];
    };

    static_assert(sizeof(UserProfile) == 200);
    static_assert(sizeof(UserCredentials) == 292);
}  // namespace services::wire
