#ifndef _OS_ABI_H
#define _OS_ABI_H

#include <stddef.h>
#include <stdint.h>
#include <os/syscall.h>

namespace os {

constexpr uint32_t ipcDataWordCount = 515;

enum class SyscallNumber : uint64_t {
    // Message queue
    OpenMessageQueue = OS_SYS_OPEN_MESSAGE_QUEUE,
    UnlinkMessageQueue = OS_SYS_UNLINK_MESSAGE_QUEUE,
    SendMessageQueue = OS_SYS_SEND_MESSAGE_QUEUE,
    ReceiveMessageQueue = OS_SYS_RECEIVE_MESSAGE_QUEUE,
    GetMessageQueueAttributes = OS_SYS_GET_MESSAGE_QUEUE_ATTRIBUTES,
    SetMessageQueueFlags = OS_SYS_SET_MESSAGE_QUEUE_FLAGS,
    RegisterMessageQueueNotification = OS_SYS_REGISTER_MESSAGE_QUEUE_NOTIFICATION,
    // Process
    GetStandardHandle = OS_SYS_GET_STANDARD_HANDLE,
    ExitTask = OS_SYS_EXIT_TASK,
    CreateThread = OS_SYS_CREATE_THREAD,
    ExitThread = OS_SYS_EXIT_THREAD,
    Yield = OS_SYS_YIELD,
    Sleep = OS_SYS_SLEEP,
    SpawnProgram = OS_SYS_SPAWN_PROGRAM,
    SpawnImage = OS_SYS_SPAWN_IMAGE,
    GetBootstrapHandle = OS_SYS_GET_BOOTSTRAP_HANDLE,
    GetTaskState = OS_SYS_GET_TASK_STATE,
    CloseHandle = OS_SYS_CLOSE_HANDLE,
    GetTaskId = OS_SYS_GET_TASK_ID,
    GetThreadId = OS_SYS_GET_THREAD_ID,
    GetTaskInfo = OS_SYS_GET_TASK_INFO,
    TerminateTask = OS_SYS_TERMINATE_TASK,
    SendSignal = OS_SYS_SEND_SIGNAL,
    SendThreadSignal = OS_SYS_SEND_THREAD_SIGNAL,
    CreateSession = OS_SYS_CREATE_SESSION,
    GetTaskIdentifiers = OS_SYS_GET_TASK_IDENTIFIERS,
    SetTaskGroup = OS_SYS_SET_TASK_GROUP,
    SetCredentials = OS_SYS_SET_CREDENTIALS,
    QueryTask = OS_SYS_QUERY_TASK,
    EnumerateTask = OS_SYS_ENUMERATE_TASK,
    ForkTask = OS_SYS_FORK_TASK,
    WaitChild = OS_SYS_WAIT_CHILD,
    ReplaceTaskImage = OS_SYS_REPLACE_TASK_IMAGE,
    WaitThread = OS_SYS_WAIT_THREAD,
    WaitAddress = OS_SYS_WAIT_ADDRESS,
    WakeAddress = OS_SYS_WAKE_ADDRESS,
    SetSignalAction = OS_SYS_SET_SIGNAL_ACTION,
    SetSignalMask = OS_SYS_SET_SIGNAL_MASK,
    GetPendingSignals = OS_SYS_GET_PENDING_SIGNALS,
    WaitSignal = OS_SYS_WAIT_SIGNAL,
    SignalReturn = OS_SYS_SIGNAL_RETURN,
    TerminalId = OS_SYS_TERMINAL_ID,
    CreateEndpoint = OS_SYS_CREATE_ENDPOINT,
    SendMessage = OS_SYS_SEND_MESSAGE,
    ReceiveMessage = OS_SYS_RECEIVE_MESSAGE,
    CallService = OS_SYS_CALL_SERVICE,
    ReplyMessage = OS_SYS_REPLY_MESSAGE,
    // Pipe and channel
    CreatePipe = OS_SYS_CREATE_PIPE,
    ReadPipe = OS_SYS_READ_PIPE,
    WritePipe = OS_SYS_WRITE_PIPE,
    CreateChannelPair = OS_SYS_CREATE_CHANNEL_PAIR,
    GetChannelIdentity = OS_SYS_GET_CHANNEL_IDENTITY,
    SetChannelNonblocking = OS_SYS_SET_CHANNEL_NONBLOCKING,
    SignalChannelEof = OS_SYS_SIGNAL_CHANNEL_EOF,
    // Device
    CallPciService = OS_SYS_CALL_PCI_SERVICE,
    MapDeviceMmio = OS_SYS_MAP_DEVICE_MMIO,
    AllocateDma = OS_SYS_ALLOCATE_DMA,
    BindIrq = OS_SYS_BIND_IRQ,
    AcknowledgeIrq = OS_SYS_ACKNOWLEDGE_IRQ,
    MapFramebuffer = OS_SYS_MAP_FRAMEBUFFER,
    GetMonotonicTime = OS_SYS_GET_MONOTONIC_TIME,
    GetWallClockTime = OS_SYS_GET_WALL_CLOCK_TIME,
    GetClockResolution = OS_SYS_GET_CLOCK_RESOLUTION,
    GetSystemInfo = OS_SYS_GET_SYSTEM_INFO,
    Log = OS_SYS_LOG,
    // Memory
    MapMemory = OS_SYS_MAP_MEMORY,
    UnmapMemory = OS_SYS_UNMAP_MEMORY,
    ProtectMemory = OS_SYS_PROTECT_MEMORY,
    OpenSharedMemory = OS_SYS_OPEN_SHARED_MEMORY,
    UnlinkSharedMemory = OS_SYS_UNLINK_SHARED_MEMORY,
    ResizeSharedMemory = OS_SYS_RESIZE_SHARED_MEMORY,
    MapSharedMemory = OS_SYS_MAP_SHARED_MEMORY,
    Count = OS_SYS_COUNT
};

struct SystemInfo {
    uint64_t uptimeNanoseconds;
    uint64_t totalMemoryBytes;
    uint64_t freeMemoryBytes;
    uint32_t onlineCpus;
    uint32_t reserved;
};
bool getSystemInfo(SystemInfo& info);


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

struct MessageQueueAttributes {
    uint64_t flags;
    uint64_t maximumMessages;
    uint64_t messageSize;
    uint64_t currentMessages;
};

struct TaskInfo {
    uint64_t id;
    uint32_t state;
    int32_t exitCode;
    char name[48];
    uint32_t uid, gid, effectiveUid, effectiveGid;
    uint64_t parentId;
    uint32_t trustedDeviceService;
    uint32_t reserved;
};

struct PipeHandles {
    uint64_t readHandle;
    uint64_t writeHandle;
};

struct ChannelHandles {
    uint64_t firstHandle;
    uint64_t secondHandle;
};

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
    uint32_t reserved;
    const char* const* arguments;
    uint32_t sharedImageCount;
    uint32_t sharedReserved;
    const struct SharedImageInfo* sharedImages;
};

struct SharedImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
};

constexpr uint64_t execRuntimeStateAddress = 0x0000000000030000ULL;

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

enum class ProgramId : uint64_t {
    Init = 0,
    TaskServer,
    FsServer,
    PciServer,
    DeviceServer,
    StorageServer,
    Count
};

enum class ErrorCode : int64_t {
    Success          = 0,
    InvalidArgument  = -1,
    InvalidHandle    = -2,
    PermissionDenied = -3,
    NotFound         = -4,
    OutOfMemory      = -5,
    WouldBlock       = -6,
    NotSupported     = -7,
    Busy             = -8,
    Timeout          = -9,
    Interrupted      = -10,
    IoError          = -11
};

constexpr uint64_t taskCapabilityDisplay = 1ULL << 2;
constexpr uint64_t taskCapabilityUsers = 1ULL << 3;

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

struct IpcMessage {
    uint64_t type;
    uint64_t senderPid;
    uint64_t senderTid;
    uint32_t length;
    uint32_t handleCount;
    uint64_t data[ipcDataWordCount];
    uint64_t handles[4];
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

int64_t invokeSyscall(SyscallNumber number, uint64_t arg1 = 0,
                      uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0,
                      uint64_t arg5 = 0, uint64_t arg6 = 0);
[[noreturn]] void exitTask(int exitCode);
[[noreturn]] void exitThread(int exitCode);
uint64_t createThread(void (*entry)(), uint64_t stackPointer);
uint64_t createThread(void (*entry)(void*), uint64_t stackPointer,
                      void* argument);
int waitThread(uint64_t threadId, int& exitCode, bool noHang = false);
int waitAddress(volatile uint32_t* address, uint32_t expected);
int wakeAddress(volatile uint32_t* address, uint32_t count);
int setSignalAction(uint32_t signal, const SignalAction* action,
                    SignalAction* oldAction);
int setSignalMask(int how, uint64_t mask, uint64_t& oldMask);
int getPendingSignals(uint64_t& mask);
int waitSignal(uint64_t mask);
[[noreturn]] void signalReturn(void* frame);
uint64_t openMessageQueue(const char* name, int flags,
                          uint32_t maximumMessages, uint32_t messageSize);
int unlinkMessageQueue(const char* name);
int sendMessageQueue(uint64_t handle, const void* data, size_t size,
                     uint32_t priority);
int64_t receiveMessageQueue(uint64_t handle, void* data, size_t capacity,
                            uint32_t& priority);
int getMessageQueueAttributes(uint64_t handle,
                              MessageQueueAttributes& attributes);
int setMessageQueueFlags(uint64_t handle, uint64_t flags,
                         uint64_t& oldFlags);
uint64_t openSharedMemory(const char* name, int flags);
int unlinkSharedMemory(const char* name);
int resizeSharedMemory(uint64_t handle, uint64_t size);
uint64_t mapSharedMemory(uint64_t handle, uint64_t address, uint64_t size,
                         uint64_t offset, uint64_t protection);
int sendThreadSignal(uint64_t threadId, uint32_t signal);
int registerMessageQueueNotification(uint64_t handle, int type,
                                     uint32_t signal);
int64_t forkTask();
int64_t waitChild(int64_t taskId, int& status, int options);
int replaceTaskImage(const ExecImageInfo& info);
void yield();
void sleep(uint64_t milliseconds);
uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection);
uint64_t createEndpoint();
int sendMessage(uint64_t endpoint, IpcMessage& message);
int receiveMessage(uint64_t endpoint, IpcMessage& message);
int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply);
int replyMessage(uint64_t endpoint, IpcMessage& reply);
uint64_t spawnProgram(ProgramId program, uint64_t bootstrapHandle,
                      uint32_t argumentCount       = 0,
                      const char* const* arguments = nullptr);
uint64_t spawnImage(const SpawnImageInfo& info);
bool createPipe(PipeHandles& handles);
bool createChannelPair(ChannelHandles& handles);
uint64_t getChannelIdentity(uint64_t handle);
bool setChannelNonblocking(uint64_t handle, bool enabled);
bool signalChannelEof(uint64_t handle);
int64_t readPipe(uint64_t handle, void* buffer, size_t size);
int64_t writePipe(uint64_t handle, const void* buffer, size_t size);
uint64_t getStandardHandle(uint32_t index);
uint64_t getBootstrapHandle();
void log(const char* message);
uint64_t mapDeviceMmio(const PciFunctionInfo& function, uint8_t barIndex,
                       uint64_t address, uint64_t size);
int allocateDma(uint64_t address, DmaBufferInfo& info);
uint64_t bindIrq(uint8_t irq, uint64_t endpoint, bool levelTriggered = false);
int acknowledgeIrq(uint64_t binding);
int getTaskState(uint64_t taskHandle, int& exitCode);
int getTaskInfo(uint64_t taskHandle, TaskInfo& info);
int terminateTask(uint64_t taskHandle, int exitCode);
int sendSignal(uint64_t taskId, uint32_t signal);
int setTaskGroup(uint64_t taskGroupId);
int64_t createSession();
int getTaskIdentifiers(TaskIdentifiers& identifiers);
uint64_t getClockResolution();
bool closeHandle(uint64_t handle);
uint64_t getTaskId();
uint64_t getThreadId();
uint64_t getMonotonicTime();
uint64_t getWallClockTime();
int mapFramebuffer(uint64_t address, DisplayInfo& info);
bool unmapMemory(uint64_t address, uint64_t size);
bool protectMemory(uint64_t address, uint64_t size, uint64_t protection);
void* allocateMemory(size_t size);
bool freeMemory(void* pointer);
int printString(const char* string);

int runtimeInit(int argumentCount, char** arguments, char** environment);
void runtimeFini();

size_t stringLength(const char* value);
bool stringsEqual(const char* left, const char* right);
void copyString(char* destination, const char* source, size_t capacity);
void clearMemory(void* destination, size_t size);
void copyBytes(void* destination, const void* source, size_t size);

}  // namespace os

#endif
