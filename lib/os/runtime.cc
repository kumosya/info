#include "os/abi.h"
#include "os/pci.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"

extern "C" {
using RuntimeFunction = void (*)();
extern RuntimeFunction __init_array_start[];
extern RuntimeFunction __init_array_end[];
extern RuntimeFunction __fini_array_start[];
extern RuntimeFunction __fini_array_end[];
}

namespace os {

namespace {

constexpr uint64_t heapBase           = 0x0000000010000000ULL;
constexpr uint64_t pageSize           = 4096;
constexpr uint32_t allocationCapacity = 4096;

struct Allocation {
    uint64_t address;
    uint64_t size;
};

Allocation allocations[allocationCapacity];
uint64_t nextAllocationAddress = heapBase;
uint8_t heapLock;
uint64_t terminalEndpoint;
uint32_t terminalId;

void lockHeap() {
    while (__atomic_test_and_set(&heapLock, __ATOMIC_ACQUIRE)) {
        asm volatile("pause");
    }
}

void unlockHeap() { __atomic_clear(&heapLock, __ATOMIC_RELEASE); }

uint64_t alignToPage(size_t size) {
    if (size == 0 || size > ~0ULL - (pageSize - 1)) {
        return 0;
    }
    return (static_cast<uint64_t>(size) + pageSize - 1) & ~(pageSize - 1);
}

}  // namespace

int64_t invokeSyscall(SyscallNumber number, uint64_t arg1, uint64_t arg2,
                      uint64_t arg3, uint64_t arg4, uint64_t arg5,
                      uint64_t arg6) {
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8 asm("r8")   = arg5;
    register uint64_t r9 asm("r9")   = arg6;
    uint64_t result;
    uint16_t selector;
    asm volatile("mov %%cs, %0" : "=r"(selector));
    if ((selector & 3) == 0) {
        asm volatile("int $0x80" : "=a"(result)
                     : "a"(static_cast<uint64_t>(number)), "D"(arg1), "S"(arg2), "d"(arg3),
                       "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
        return static_cast<int64_t>(result);
    }
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(static_cast<uint64_t>(number)), "D"(arg1), "S"(arg2),
                   "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return static_cast<int64_t>(result);
}

[[noreturn]] void exitTask(int exitCode) {
    invokeSyscall(SyscallNumber::ExitTask, static_cast<uint64_t>(exitCode));
    while (true) {
        asm volatile("ud2");
    }
}

[[noreturn]] void exitThread(int exitCode) {
    invokeSyscall(SyscallNumber::ExitThread, static_cast<uint64_t>(exitCode));
    while (true) {
        asm volatile("ud2");
    }
}

uint64_t createThread(void (*entry)(), uint64_t stackPointer) {
    return invokeSyscall(SyscallNumber::CreateThread,
                         reinterpret_cast<uint64_t>(entry), stackPointer);
}

uint64_t createThread(void (*entry)(void*), uint64_t stackPointer,
                      void* argument) {
    return invokeSyscall(SyscallNumber::CreateThread,
                         reinterpret_cast<uint64_t>(entry), stackPointer,
                         reinterpret_cast<uint64_t>(argument));
}

int waitThread(uint64_t threadId, int& exitCode, bool noHang) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::WaitThread, threadId,
        reinterpret_cast<uint64_t>(&exitCode), noHang ? 1 : 0));
}

int waitAddress(volatile uint32_t* address, uint32_t expected) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::WaitAddress, reinterpret_cast<uint64_t>(address),
        expected));
}

int wakeAddress(volatile uint32_t* address, uint32_t count) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::WakeAddress, reinterpret_cast<uint64_t>(address),
        count));
}

int setSignalAction(uint32_t signal, const SignalAction* action,
                    SignalAction* oldAction) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::SetSignalAction, signal,
        reinterpret_cast<uint64_t>(action),
        reinterpret_cast<uint64_t>(oldAction)));
}

int setSignalMask(int how, uint64_t mask, uint64_t& oldMask) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::SetSignalMask, static_cast<uint64_t>(how), mask,
        reinterpret_cast<uint64_t>(&oldMask)));
}

int getPendingSignals(uint64_t& mask) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::GetPendingSignals, reinterpret_cast<uint64_t>(&mask)));
}

int waitSignal(uint64_t mask) {
    return static_cast<int>(invokeSyscall(SyscallNumber::WaitSignal, mask));
}

void signalReturn(void* frame) {
    invokeSyscall(SyscallNumber::SignalReturn,
                  reinterpret_cast<uint64_t>(frame));
    while (true) asm volatile("ud2");
}

uint64_t openMessageQueue(const char* name, int flags,
                          uint32_t maximumMessages, uint32_t messageSize) {
    return invokeSyscall(SyscallNumber::OpenMessageQueue,
                         reinterpret_cast<uint64_t>(name),
                         static_cast<uint64_t>(flags), maximumMessages,
                         messageSize);
}

int unlinkMessageQueue(const char* name) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::UnlinkMessageQueue,
        reinterpret_cast<uint64_t>(name)));
}

int sendMessageQueue(uint64_t handle, const void* data, size_t size,
                     uint32_t priority) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::SendMessageQueue, handle,
        reinterpret_cast<uint64_t>(data), size, priority));
}

int64_t receiveMessageQueue(uint64_t handle, void* data, size_t capacity,
                            uint32_t& priority) {
    return invokeSyscall(SyscallNumber::ReceiveMessageQueue, handle,
                         reinterpret_cast<uint64_t>(data), capacity,
                         reinterpret_cast<uint64_t>(&priority));
}

int getMessageQueueAttributes(uint64_t handle,
                              MessageQueueAttributes& attributes) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::GetMessageQueueAttributes, handle,
        reinterpret_cast<uint64_t>(&attributes)));
}

int setMessageQueueFlags(uint64_t handle, uint64_t flags,
                         uint64_t& oldFlags) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::SetMessageQueueFlags, handle, flags,
        reinterpret_cast<uint64_t>(&oldFlags)));
}

uint64_t openSharedMemory(const char* name, int flags) {
    return invokeSyscall(SyscallNumber::OpenSharedMemory,
                         reinterpret_cast<uint64_t>(name),
                         static_cast<uint64_t>(flags));
}

int unlinkSharedMemory(const char* name) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::UnlinkSharedMemory,
        reinterpret_cast<uint64_t>(name)));
}

int resizeSharedMemory(uint64_t handle, uint64_t size) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::ResizeSharedMemory, handle, size));
}

uint64_t mapSharedMemory(uint64_t handle, uint64_t address, uint64_t size,
                         uint64_t offset, uint64_t protection) {
    return invokeSyscall(SyscallNumber::MapSharedMemory, handle, address, size,
                         offset, protection);
}

int sendThreadSignal(uint64_t threadId, uint32_t signal) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::SendThreadSignal, threadId, signal));
}

int registerMessageQueueNotification(uint64_t handle, int type,
                                     uint32_t signal) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::RegisterMessageQueueNotification, handle,
        static_cast<uint64_t>(type), signal));
}

int64_t forkTask() {
    return invokeSyscall(SyscallNumber::ForkTask);
}

int64_t waitChild(int64_t taskId, int& status, int options) {
    return invokeSyscall(SyscallNumber::WaitChild,
                         static_cast<uint64_t>(taskId),
                         reinterpret_cast<uint64_t>(&status),
                         static_cast<uint64_t>(options));
}

int replaceTaskImage(const ExecImageInfo& info) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::ReplaceTaskImage,
        reinterpret_cast<uint64_t>(&info)));
}

void yield() { invokeSyscall(SyscallNumber::Yield); }

void sleep(uint64_t milliseconds) {
    invokeSyscall(SyscallNumber::Sleep, milliseconds);
}

uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection) {
    return invokeSyscall(SyscallNumber::MapMemory, address, size, protection);
}

uint64_t createEndpoint() {
    return invokeSyscall(SyscallNumber::CreateEndpoint);
}

int sendMessage(uint64_t endpoint, IpcMessage& message) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::SendMessage, endpoint,
                      reinterpret_cast<uint64_t>(&message)));
}

int receiveMessage(uint64_t endpoint, IpcMessage& message) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::ReceiveMessage, endpoint,
                      reinterpret_cast<uint64_t>(&message)));
}

int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply) {
    return static_cast<int>(invokeSyscall(SyscallNumber::CallService, endpoint,
                                          reinterpret_cast<uint64_t>(&request),
                                          reinterpret_cast<uint64_t>(&reply)));
}

int replyMessage(uint64_t endpoint, IpcMessage& reply) {
    return static_cast<int>(invokeSyscall(SyscallNumber::ReplyMessage, endpoint,
                                          reinterpret_cast<uint64_t>(&reply)));
}

uint64_t spawnProgram(ProgramId program, uint64_t bootstrapHandle,
                      uint32_t argumentCount, const char* const* arguments) {
    return invokeSyscall(SyscallNumber::SpawnProgram,
                         static_cast<uint64_t>(program), bootstrapHandle,
                         argumentCount, reinterpret_cast<uint64_t>(arguments));
}

uint64_t spawnImage(const SpawnImageInfo& info) {
    return invokeSyscall(SyscallNumber::SpawnImage,
                         reinterpret_cast<uint64_t>(&info));
}

bool createPipe(PipeHandles& handles) {
    handles = {};
    return invokeSyscall(SyscallNumber::CreatePipe,
                         reinterpret_cast<uint64_t>(&handles)) == 0;
}

bool createChannelPair(ChannelHandles& handles) {
    handles = {};
    return invokeSyscall(SyscallNumber::CreateChannelPair,
                         reinterpret_cast<uint64_t>(&handles)) == 0;
}

uint64_t getChannelIdentity(uint64_t handle) {
    const int64_t result = invokeSyscall(SyscallNumber::GetChannelIdentity,
                                         handle);
    return result > 0 ? static_cast<uint64_t>(result) : 0;
}

bool setChannelNonblocking(uint64_t handle, bool enabled) {
    return invokeSyscall(SyscallNumber::SetChannelNonblocking, handle,
                         enabled ? 1 : 0) == 0;
}

bool signalChannelEof(uint64_t handle) {
    return invokeSyscall(SyscallNumber::SignalChannelEof, handle) == 0;
}

int64_t readPipe(uint64_t handle, void* buffer, size_t size) {
    if (buffer == nullptr || size == 0) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    const size_t transfer = size < 512 ? size : 512;
    return invokeSyscall(SyscallNumber::ReadPipe, handle,
                         reinterpret_cast<uint64_t>(buffer), transfer);
}

int64_t writePipe(uint64_t handle, const void* buffer, size_t size) {
    if (buffer == nullptr) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    size_t written = 0;
    while (written < size) {
        const size_t transfer = size - written < 512 ? size - written : 512;
        const int64_t result =
            invokeSyscall(SyscallNumber::WritePipe, handle,
                          reinterpret_cast<uint64_t>(
                              static_cast<const uint8_t*>(buffer) + written),
                          transfer);
        if (result <= 0) {
            return written == 0 ? result : static_cast<int64_t>(written);
        }
        written += static_cast<size_t>(result);
    }
    return static_cast<int64_t>(written);
}

uint64_t getStandardHandle(uint32_t index) {
    return invokeSyscall(SyscallNumber::GetStandardHandle, index);
}

uint64_t getBootstrapHandle() {
    return invokeSyscall(SyscallNumber::GetBootstrapHandle);
}

void log(const char* message) {
    invokeSyscall(SyscallNumber::Log, reinterpret_cast<uint64_t>(message),
                  stringLength(message));
}

uint64_t pciServiceHandle() {
    static uint64_t handle;
    if (handle == 0)
        handle = lookupService(serviceRegistry(), pciServiceName);
    return handle;
}

int enumeratePci(PciFunctionInfo* functions, uint32_t capacity) {
    if (functions == nullptr || capacity == 0 || capacity > 64) return -1;
    const uint64_t service = pciServiceHandle();
    if (service == 0) return -1;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(PciServiceRequest::Enumerate);
    request.data[0] = capacity;
    request.length = 1;
    IpcMessage reply{};
    const int result = callService(service, request, reply);
    if (result != 0 || reply.data[0] != 0 || reply.data[1] > capacity ||
        reply.length < 2 + (reply.data[1] * sizeof(PciFunctionInfo) + 7) / 8)
        return -1;
    copyBytes(functions, &reply.data[2], reply.data[1] * sizeof(PciFunctionInfo));
    return static_cast<int>(reply.data[1]);
}

int readPciConfig(const PciFunctionInfo& function, uint16_t offset,
                  uint32_t& value) {
    const uint64_t service = pciServiceHandle();
    if (service == 0) return -1;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(PciServiceRequest::ReadConfig);
    request.data[0] = offset;
    copyBytes(&request.data[1], &function, sizeof(function));
    request.length = 3;
    IpcMessage reply{};
    const int result = callService(service, request, reply);
    if (result == 0 && reply.data[0] == 0 && reply.length >= 2)
        value = static_cast<uint32_t>(reply.data[1]);
    else return -1;
    return result;
}

int writePciConfig(const PciFunctionInfo& function, uint16_t offset,
                   uint32_t value) {
    const uint64_t service = pciServiceHandle();
    if (service == 0) return -1;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(PciServiceRequest::WriteConfig);
    request.data[0] = offset;
    copyBytes(&request.data[1], &function, sizeof(function));
    request.data[3] = value;
    request.length = 4;
    IpcMessage reply{};
    const int result = callService(service, request, reply);
    return result == 0 && reply.data[0] == 0 ? 0 : -1;
}

uint64_t mapDeviceMmio(const PciFunctionInfo& function, uint8_t barIndex,
                       uint64_t address, uint64_t size) {
    return invokeSyscall(SyscallNumber::MapDeviceMmio,
                         reinterpret_cast<uint64_t>(&function), barIndex,
                         address, size);
}

int allocateDma(uint64_t address, DmaBufferInfo& info) {
    return static_cast<int>(invokeSyscall(SyscallNumber::AllocateDma, address,
                                          reinterpret_cast<uint64_t>(&info)));
}

uint64_t bindIrq(uint8_t irq, uint64_t endpoint, bool levelTriggered) {
    return invokeSyscall(SyscallNumber::BindIrq, irq, endpoint, levelTriggered);
}

int acknowledgeIrq(uint64_t binding) {
    return static_cast<int>(invokeSyscall(SyscallNumber::AcknowledgeIrq, binding));
}

int getTaskState(uint64_t taskHandle, int& exitCode) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::GetTaskState, taskHandle,
                      reinterpret_cast<uint64_t>(&exitCode)));
}

int getTaskInfo(uint64_t taskHandle, TaskInfo& info) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::GetTaskInfo, taskHandle,
        reinterpret_cast<uint64_t>(&info)));
}

int terminateTask(uint64_t taskHandle, int exitCode) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::TerminateTask, taskHandle,
        static_cast<uint64_t>(static_cast<int64_t>(exitCode))));
}

int sendSignal(uint64_t taskId, uint32_t signal) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::SendSignal, taskId, signal));
}

int setTaskGroup(uint64_t taskGroupId) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::SetTaskGroup, taskGroupId));
}

int64_t createSession() {
    return invokeSyscall(SyscallNumber::CreateSession);
}

int getTaskIdentifiers(TaskIdentifiers& identifiers) {
    return static_cast<int>(invokeSyscall(
        SyscallNumber::GetTaskIdentifiers,
        reinterpret_cast<uint64_t>(&identifiers)));
}

uint64_t getClockResolution() {
    return invokeSyscall(SyscallNumber::GetClockResolution);
}


bool closeHandle(uint64_t handle) {
    return invokeSyscall(SyscallNumber::CloseHandle, handle) == 0;
}

uint64_t getTaskId() { return invokeSyscall(SyscallNumber::GetTaskId); }

uint64_t getThreadId() { return invokeSyscall(SyscallNumber::GetThreadId); }

bool getSystemInfo(SystemInfo& info) {
    return invokeSyscall(SyscallNumber::GetSystemInfo, reinterpret_cast<uint64_t>(&info)) == 0;
}

uint64_t getMonotonicTime() {
    return invokeSyscall(SyscallNumber::GetMonotonicTime);
}

uint64_t getWallClockTime() {
    return invokeSyscall(SyscallNumber::GetWallClockTime);
}

int mapFramebuffer(uint64_t address, DisplayInfo& info) {
    return static_cast<int>(invokeSyscall(SyscallNumber::MapFramebuffer,
                                          address,
                                          reinterpret_cast<uint64_t>(&info)));
}

bool unmapMemory(uint64_t address, uint64_t size) {
    return invokeSyscall(SyscallNumber::UnmapMemory, address, size) == 0;
}

bool protectMemory(uint64_t address, uint64_t size, uint64_t protection) {
    return invokeSyscall(SyscallNumber::ProtectMemory, address, size,
                         protection) == 0;
}

void* allocateMemory(size_t size) {
    const uint64_t mappedSize = alignToPage(size);
    if (mappedSize == 0) {
        return nullptr;
    }
    lockHeap();
    for (Allocation& allocation : allocations) {
        if (allocation.address != 0) {
            continue;
        }
        const uint64_t address = nextAllocationAddress;
        if (address + mappedSize < address ||
            mapMemory(address, mappedSize, 1) != address) {
            unlockHeap();
            return nullptr;
        }
        allocation = {address, mappedSize};
        nextAllocationAddress += mappedSize + pageSize;
        unlockHeap();
        return reinterpret_cast<void*>(address);
    }
    unlockHeap();
    return nullptr;
}

bool freeMemory(void* pointer) {
    const uint64_t address = reinterpret_cast<uint64_t>(pointer);
    lockHeap();
    for (Allocation& allocation : allocations) {
        if (allocation.address == address) {
            const uint64_t size = allocation.size;
            allocation          = {};
            const bool result   = unmapMemory(address, size);
            unlockHeap();
            return result;
        }
    }
    unlockHeap();
    return false;
}

int printString(const char* string) {
    if (string == nullptr) {
        return static_cast<int>(ErrorCode::InvalidArgument);
    }
    const int64_t result = writeTerminal(string, stringLength(string));
    return result < 0 ? static_cast<int>(result) : 0;
}

uint32_t getTerminalId() { return terminalId; }

void setTerminalId(uint32_t id) {
    terminalId = id < textTerminalCount ? id : 0;
    invokeSyscall(SyscallNumber::TerminalId, terminalId);
}

int64_t writeTerminal(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    if (terminalEndpoint == 0) {
        terminalEndpoint =
            lookupService(serviceRegistry(), terminalServiceName, 1000);
    }
    if (terminalEndpoint == 0) {
        return static_cast<int64_t>(ErrorCode::NotFound);
    }
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = size - offset;
        if (chunk > 512) {
            chunk = 512;
        }
        IpcMessage request{};
        request.type    = static_cast<uint64_t>(TerminalRequest::Write);
        request.data[0] = terminalId;
        request.data[1] = chunk;
        for (size_t index = 0; index < chunk; ++index) {
            reinterpret_cast<uint8_t*>(&request.data[2])[index] =
                static_cast<const uint8_t*>(buffer)[offset + index];
        }
        request.length = 2 + (chunk + sizeof(uint64_t) - 1) / sizeof(uint64_t);
        IpcMessage reply{};
        if (callService(terminalEndpoint, request, reply) != 0 ||
            static_cast<int64_t>(reply.data[0]) != 0) {
            return offset == 0 ? static_cast<int64_t>(ErrorCode::IoError)
                               : static_cast<int64_t>(offset);
        }
        offset += reply.data[1];
    }
    return static_cast<int64_t>(offset);
}

int64_t readTerminal(void* buffer, size_t size) {
    if (buffer == nullptr || size == 0) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    if (terminalEndpoint == 0) {
        terminalEndpoint =
            lookupService(serviceRegistry(), terminalServiceName, 1000);
    }
    if (terminalEndpoint == 0) {
        return static_cast<int64_t>(ErrorCode::NotFound);
    }
    while (true) {
        IpcMessage request{};
        request.type    = static_cast<uint64_t>(TerminalRequest::Read);
        request.data[0] = terminalId;
        request.data[1] =
            size > 6 * sizeof(uint64_t) ? 6 * sizeof(uint64_t) : size;
        request.length = 2;
        IpcMessage reply{};
        if (callService(terminalEndpoint, request, reply) != 0) {
            return static_cast<int64_t>(ErrorCode::IoError);
        }
        const int64_t status = static_cast<int64_t>(reply.data[0]);
        if (status == static_cast<int64_t>(ErrorCode::WouldBlock)) {
            sleep(8);
            continue;
        }
        if (status != 0) {
            return status;
        }
        const size_t count = reply.data[1];
        for (size_t index = 0; index < count; ++index) {
            static_cast<uint8_t*>(buffer)[index] =
                reinterpret_cast<const uint8_t*>(&reply.data[2])[index];
        }
        return static_cast<int64_t>(count);
    }
}

uint32_t getActiveTerminal() {
    if (terminalEndpoint == 0) {
        terminalEndpoint =
            lookupService(serviceRegistry(), terminalServiceName, 1000);
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(TerminalRequest::GetActive);
    IpcMessage reply{};
    return terminalEndpoint != 0 &&
                   callService(terminalEndpoint, request, reply) == 0
               ? static_cast<uint32_t>(reply.data[1])
               : 0;
}

bool switchTerminal(uint32_t id) {
    if (terminalEndpoint == 0) {
        terminalEndpoint =
            lookupService(serviceRegistry(), terminalServiceName, 1000);
    }
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(TerminalRequest::Switch);
    request.data[0] = id;
    request.length  = 1;
    IpcMessage reply{};
    return terminalEndpoint != 0 &&
           callService(terminalEndpoint, request, reply) == 0 &&
           reply.data[0] == 0;
}

bool setTerminalForegroundTask(uint64_t taskId) {
    if (terminalEndpoint == 0) {
        terminalEndpoint =
            lookupService(serviceRegistry(), terminalServiceName, 1000);
    }
    IpcMessage request{};
    request.type    =
        static_cast<uint64_t>(TerminalRequest::SetForegroundTask);
    request.data[0] = terminalId;
    request.data[1] = taskId;
    request.length  = 2;
    IpcMessage reply{};
    return terminalEndpoint != 0 &&
           callService(terminalEndpoint, request, reply) == 0 &&
           reply.data[0] == 0;
}

int runtimeInit(int argumentCount, char** arguments, char**) {
    heapLock              = 0;
    nextAllocationAddress = heapBase;
    terminalEndpoint      = 0;
    const uint64_t inheritedTerminal = invokeSyscall(SyscallNumber::TerminalId, UINT64_MAX);
    terminalId            = inheritedTerminal < textTerminalCount ? inheritedTerminal : 0;
    clearMemory(allocations, sizeof(allocations));
    constexpr char prefix[] = "--os-terminal=";
    for (int index = 0; index < argumentCount; ++index) {
        const char* argument = arguments[index];
        uint32_t position    = 0;
        while (argument != nullptr && prefix[position] != '\0' &&
               argument[position] == prefix[position]) {
            ++position;
        }
        if (prefix[position] != '\0' || argument[position] < '1' ||
            argument[position] > '7' || argument[position + 1] != '\0') {
            continue;
        }
        setTerminalId(static_cast<uint32_t>(argument[position] - '1'));
        for (int move = index + 1; move < argumentCount; ++move) {
            arguments[move - 1] = arguments[move];
        }
        --argumentCount;
        arguments[argumentCount] = nullptr;
        break;
    }
    for (RuntimeFunction* constructor = __init_array_start;
         constructor < __init_array_end; ++constructor) {
        (*constructor)();
    }
    return argumentCount;
}

void runtimeFini() {
    for (RuntimeFunction* destructor = __fini_array_end;
         destructor != __fini_array_start;) {
        (*--destructor)();
    }
}

size_t stringLength(const char* value) {
    size_t length = 0;
    while (value != nullptr && value[length] != '\0') {
        ++length;
    }
    return length;
}

bool stringsEqual(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

void copyString(char* destination, const char* source, size_t capacity) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    size_t index = 0;
    while (source != nullptr && source[index] != '\0' && index + 1 < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void clearMemory(void* destination, size_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void copyBytes(void* destination, const void* source, size_t size) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    const uint8_t* input = static_cast<const uint8_t*>(source);
    size_t index = 0;
    for (; index + sizeof(uint64_t) <= size; index += sizeof(uint64_t)) {
        uint64_t word;
        __builtin_memcpy(&word, input + index, sizeof(word));
        __builtin_memcpy(output + index, &word, sizeof(word));
    }
    for (; index < size; ++index) {
        output[index] = input[index];
    }
}

}  // namespace os
