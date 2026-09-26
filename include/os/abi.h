#pragma once

#include <stddef.h>
#include <stdint.h>

namespace os {

constexpr uint32_t ipcDataWordCount = 515;

enum class SyscallNumber : uint64_t {
    ExitProcess = 0,
    CreateThread,
    ExitThread,
    Yield,
    Sleep,
    MapMemory,
    UnmapMemory,
    CreateEndpoint,
    SendMessage,
    ReceiveMessage,
    CallService,
    ReplyMessage,
    SpawnProgram,
    GetBootstrapHandle,
    Log,
    EnumeratePci,
    ReadPciConfig,
    WritePciConfig,
    MapDeviceMmio,
    AllocateDma,
    BindIrq,
    GetProcessState,
    CloseHandle,
    GetProcessId,
    GetThreadId,
    GetMonotonicTime,
    GetWallClockTime,
    ReadIoPort,
    WriteIoPort,
    MapFramebuffer,
    SpawnImage,
    CreatePipe,
    ReadPipe,
    WritePipe,
    GetStandardHandle,
    GetProcessInfo,
    TerminateProcess,
    CreatePty,
    GetPtyAttributes,
    SetPtyAttributes,
    GetPtyWindowSize,
    SetPtyWindowSize,
    SetPtyForegroundGroup,
    SetPtyNonblocking,
    SendSignal,
    CreateSession,
    GetProcessIdentifiers,
    GetClockResolution,
    PosixUnsupported,
    WaitThread,
    WaitAddress,
    WakeAddress,
    SetSignalAction,
    SetSignalMask,
    GetPendingSignals,
    WaitSignal,
    SignalReturn,
    OpenMessageQueue,
    UnlinkMessageQueue,
    SendMessageQueue,
    ReceiveMessageQueue,
    GetMessageQueueAttributes,
    SetMessageQueueFlags,
    ProtectMemory,
    OpenSharedMemory,
    UnlinkSharedMemory,
    ResizeSharedMemory,
    MapSharedMemory,
    SendThreadSignal,
    RegisterMessageQueueNotification,
    ForkProcess,
    WaitChild,
    ReplaceProcessImage,
    Count
};

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

struct ProcessIdentifiers {
    uint64_t parentId;
    uint64_t processGroupId;
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

struct ProcessInfo {
    uint64_t id;
    uint32_t state;
    int32_t exitCode;
    char name[48];
};

struct PipeHandles {
    uint64_t readHandle;
    uint64_t writeHandle;
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
    uint64_t parentProcessId;
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

constexpr uint64_t processCapabilityDisplay = 1ULL << 2;

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
[[noreturn]] void exitProcess(int exitCode);
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
int64_t forkProcess();
int64_t waitChild(int64_t processId, int& status, int options);
int replaceProcessImage(const ExecImageInfo& info);
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
int64_t readPipe(uint64_t handle, void* buffer, size_t size);
int64_t writePipe(uint64_t handle, const void* buffer, size_t size);
uint64_t getStandardHandle(uint32_t index);
uint64_t getBootstrapHandle();
void log(const char* message);
int enumeratePci(PciFunctionInfo* functions, uint32_t capacity);
int readPciConfig(const PciFunctionInfo& function, uint16_t offset,
                  uint32_t& value);
int writePciConfig(const PciFunctionInfo& function, uint16_t offset,
                   uint32_t value);
uint64_t mapDeviceMmio(const PciFunctionInfo& function, uint8_t barIndex,
                       uint64_t address, uint64_t size);
int allocateDma(uint64_t address, DmaBufferInfo& info);
uint64_t bindIrq(uint8_t irq, uint64_t endpoint);
int getProcessState(uint64_t processHandle, int& exitCode);
int getProcessInfo(uint64_t processHandle, ProcessInfo& info);
int terminateProcess(uint64_t processHandle, int exitCode);
int sendSignal(uint64_t processId, uint32_t signal);
int64_t createSession();
int getProcessIdentifiers(ProcessIdentifiers& identifiers);
uint64_t getClockResolution();
int posixUnsupported(PosixOperation operation);
bool closeHandle(uint64_t handle);
uint64_t getProcessId();
uint64_t getThreadId();
uint64_t getMonotonicTime();
uint64_t getWallClockTime();
int readIoPort(uint16_t port, uint8_t& value);
int writeIoPort(uint16_t port, uint8_t value);
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

}  // namespace os
