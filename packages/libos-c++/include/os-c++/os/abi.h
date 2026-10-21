#ifndef _OS_ABI_H
#define _OS_ABI_H

#include <cstddef>
#include <cstdint>
#include <os/syscall.h>

namespace os {

constexpr uint32_t ipcDataWordCount = 515;

enum class SyscallNumber : uint64_t {
    // Message queue
    // Process
    GetStandardHandle = OS_SYS_GET_STANDARD_HANDLE,
    ExitTask = OS_SYS_EXIT_TASK,
    CreateThread = OS_SYS_CREATE_THREAD,
    ExitThread = OS_SYS_EXIT_THREAD,
    Yield = OS_SYS_YIELD,
    Sleep = OS_SYS_SLEEP,
    GetBootstrapHandle = OS_SYS_GET_BOOTSTRAP_HANDLE,
    CloseHandle = OS_SYS_CLOSE_HANDLE,
    ForkTask = OS_SYS_FORK_TASK,
    ReplaceTaskImage = OS_SYS_REPLACE_TASK_IMAGE,
    WaitAddress = OS_SYS_WAIT_ADDRESS,
    WakeAddress = OS_SYS_WAKE_ADDRESS,
    SendMessage = OS_SYS_SEND_MESSAGE,
    ReceiveMessage = OS_SYS_RECEIVE_MESSAGE,
    CallService = OS_SYS_CALL_SERVICE,
    ReplyMessage = OS_SYS_REPLY_MESSAGE,
    // Pipe and channel
    // Device
    // Memory
    MapMemory = OS_SYS_MAP_MEMORY,
    UnmapMemory = OS_SYS_UNMAP_MEMORY,
    ProtectMemory = OS_SYS_PROTECT_MEMORY,
    Count = OS_SYS_COUNT
};

struct SystemInfo {
    uint64_t uptimeNanoseconds;
    uint64_t totalMemoryBytes;
    uint64_t freeMemoryBytes;
    uint32_t onlineCpus;
    uint32_t reserved;
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
    uint32_t serviceRole;
    uint64_t taskGroupId, sessionId;
    uint64_t terminalForegroundGroup;
    uint64_t startedNanoseconds, cpuNanoseconds;
    uint32_t terminalKind, terminalIndex;
    uint32_t commandLineSize;
    uint32_t managedService;
    char commandLine[384];
};

enum class ServiceRole : uint32_t { None, Init, Task, Fs, Pci, Device, Storage, Tty, Console, Window, Users, Memory, Pipe, MessageQueue };

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
    uint32_t serviceRole;
    const char* const* arguments;
    uint32_t sharedImageCount;
    uint32_t flags;
    uint32_t terminalKind, terminalIndex;
    const struct SharedImageInfo* sharedImages;
};
constexpr uint32_t spawnFlagNewSession = 1U;
constexpr uint32_t spawnFlagManagedService = 2U;

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
    TtyServer,
    ConsoleServer,
    WindowServer,
    UsersServer,
    MemoryManager,
    PipeServer,
    MessageQueueServer,
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
    IoError          = -11,
    Cancelled        = -12,
    EndpointClosed   = -13,
    PeerDied         = -14
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
    uint64_t replyToken;
    uint32_t length;
    uint32_t handleCount;
    uint64_t data[ipcDataWordCount];
    uint64_t handles[4];
    uint32_t handleRights[4];
    uint32_t handleOptions[4];
};

constexpr uint32_t ipcHandleRestrictRights = 1U << 0;
constexpr uint32_t ipcHandleMove = 1U << 1;

constexpr uint32_t handleFlagNoInherit = 1U << 0;
constexpr uint32_t handleFlagCloseOnExec = 1U << 1;
constexpr uint32_t handleFlagsMask = handleFlagNoInherit | handleFlagCloseOnExec;
constexpr uint64_t ipcInfiniteDeadline = UINT64_MAX;

enum class HandleOperation : uint64_t { Close, Duplicate, RestrictRights, SetFlags, GetState, ObjectIdentity, References, DestroyEndpoint, RequestActive };

class Handle {
   public:
    explicit Handle(uint64_t value = 0) : value_(value) {}
    ~Handle();
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    uint64_t get() const { return value_; }
    uint64_t release() { const auto value = value_; value_ = 0; return value; }
    void reset(uint64_t value = 0);
    static int64_t duplicate(uint64_t handle, uint32_t rights, uint32_t flags = 0);
    static int restrictRights(uint64_t handle, uint32_t rights);
    static int setFlags(uint64_t handle, uint32_t flags);
    static int state(uint64_t handle);
    static uint64_t identity(uint64_t handle);
    static int64_t references(uint64_t handle);
    static int destroyEndpoint(uint64_t handle);
    static int requestActive(uint64_t endpoint, uint64_t token);
   private:
    uint64_t value_;
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

int waitAddress(volatile uint32_t* address, uint32_t expected);
int wakeAddress(volatile uint32_t* address, uint32_t count);








int64_t forkTask();

int replaceTaskImage(const ExecImageInfo& info);
void yield();
void sleep(uint64_t milliseconds);
uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection);
class Endpoint {
   public:
    static uint64_t create();
    static int cancel(uint64_t endpoint, uint64_t threadId);
};
int sendMessage(uint64_t endpoint, IpcMessage& message,
                uint64_t deadline = ipcInfiniteDeadline);
int receiveMessage(uint64_t endpoint, IpcMessage& message,
                   uint64_t deadline = ipcInfiniteDeadline);
int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply,
                uint64_t deadline = ipcInfiniteDeadline);
int replyMessage(uint64_t endpoint, uint64_t replyToken, IpcMessage& reply);



uint64_t getStandardHandle(uint32_t index);
uint64_t getBootstrapHandle();









bool closeHandle(uint64_t handle);



bool unmapMemory(uint64_t address, uint64_t size);
bool protectMemory(uint64_t address, uint64_t size, uint64_t protection);

class Runtime {
   public:
    static const char* environmentValue(const char* name);
    static void bindEnvironment(char*** environment);
    static int printString(const char* string);
    static void copyBytes(void* destination, const void* source, size_t size);
    static void clearMemory(void* destination, size_t size);
    static void copyString(char* destination, const char* source, size_t capacity);
    static bool stringsEqual(const char* left, const char* right);
    static size_t stringLength(const char* value);
    static void runtimeFini();
    static void prepare(char** environment);
    static int runtimeInit(int argumentCount, char** arguments, char** environment);
   private:
    static char** initialEnvironment_;
    static char*** environmentSlot_;
    static void (*sharedFinalizer_)();
    static bool prepared_;
};

struct PageObjectInfo;
struct MemoryUsage;
class Memory {
   public:
    static uint64_t allocatePages(uint32_t order, uint16_t node = UINT16_MAX, uint64_t owner = 0);
    static uint64_t mapPages(uint64_t handle, uint64_t address, uint64_t size, uint64_t offset, uint64_t protection, uint64_t target = 0);
    static int pageInfo(uint64_t handle, PageObjectInfo& info);
    static int usage(MemoryUsage& info, uint64_t target = 0);
    static int64_t reclaimCaches();
    static int unmapTarget(uint64_t address, uint64_t size, uint64_t target = 0);
    static int protectTarget(uint64_t address, uint64_t size, uint64_t protection, uint64_t target = 0);
    static uint64_t mapSharedMemory(uint64_t handle, uint64_t address, uint64_t size,
                              uint64_t offset, uint64_t protection, uint64_t target = 0);
    static int resizeSharedMemory(uint64_t handle, uint64_t size);
    static int unlinkSharedMemory(const char* name);
    static uint64_t openSharedMemory(const char* name, int flags);
    static bool freeMemory(void* pointer);
    static void* allocateMemory(size_t size);
};

class Pipe {
   public:
    static int64_t writePipe(uint64_t handle, const void* buffer, size_t size);
    static int64_t readPipe(uint64_t handle, void* buffer, size_t size);
    static bool signalChannelEof(uint64_t handle);
    static bool setChannelNonblocking(uint64_t handle, bool enabled);
    static uint64_t getChannelIdentity(uint64_t handle);
    static bool createChannelPair(ChannelHandles& handles);
    static bool createPipe(PipeHandles& handles);
};

class MessageQueue {
   public:
    static int registerMessageQueueNotification(uint64_t handle, int type,
                                     uint32_t signal);
    static int setMessageQueueFlags(uint64_t handle, uint64_t flags,
                         uint64_t& oldFlags);
    static int getMessageQueueAttributes(uint64_t handle,
                              MessageQueueAttributes& attributes);
    static int64_t receiveMessageQueue(uint64_t handle, void* data, size_t capacity,
                            uint32_t& priority);
    static int sendMessageQueue(uint64_t handle, const void* data, size_t size,
                     uint32_t priority);
    static int unlinkMessageQueue(const char* name);
    static uint64_t openMessageQueue(const char* name, int flags,
                          uint32_t maximumMessages, uint32_t messageSize);
};

class Device {
   public:
    static int mapFramebuffer(uint64_t address, DisplayInfo& info);
    static int acknowledgeIrq(uint64_t binding);
    static uint64_t bindIrq(uint8_t irq, uint64_t endpoint, bool levelTriggered = false);
    static uint64_t claimDevice(const PciFunctionInfo& function);
    static int stopDevice(uint64_t device);
    static int allocateDma(uint64_t device, uint64_t address, DmaBufferInfo& info);
    static uint64_t mapDeviceMmio(const PciFunctionInfo& function, uint8_t barIndex,
                       uint64_t address, uint64_t size);
};

class Clock {
   public:
    static uint64_t getClockResolution();
    static uint64_t getWallClockTime();
    static uint64_t getMonotonicTime();
};

class System {
   public:
    static void log(const char* message);
    static bool getSystemInfo(SystemInfo& info);
};

}  // namespace os

#endif
