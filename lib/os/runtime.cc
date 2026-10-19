#include "os/task.h"
#include "os/abi.h"
#include "os/core_service.h"
#include "os/mm.h"
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

char** Runtime::initialEnvironment_ = nullptr;
char*** Runtime::environmentSlot_ = nullptr;
void (*Runtime::sharedFinalizer_)() = nullptr;
bool Runtime::prepared_ = false;

namespace {

class HeapState {
 public:
static constexpr uint64_t heapBase           = 0x0000000010000000ULL;
static constexpr uint64_t pageSize           = 4096;
static constexpr uint32_t allocationCapacity = 4096;

struct Allocation {
    uint64_t address;
    uint64_t size;
};

inline static Allocation allocations[allocationCapacity];
inline static uint64_t nextAllocationAddress = heapBase;
inline static uint8_t heapLock;


static void lockHeap() {
    while (__atomic_test_and_set(&heapLock, __ATOMIC_ACQUIRE)) {
        asm volatile("pause");
    }
}

static void unlockHeap() { __atomic_clear(&heapLock, __ATOMIC_RELEASE); }

static uint64_t alignToPage(size_t size) {
    if (size == 0 || size > ~0ULL - (pageSize - 1)) {
        return 0;
    }
    return (static_cast<uint64_t>(size) + pageSize - 1) & ~(pageSize - 1);
}

};

}  // namespace

int64_t invokeSyscall(SyscallNumber number, uint64_t arg1, uint64_t arg2,
                      uint64_t arg3, uint64_t arg4, uint64_t arg5,
                      uint64_t arg6) {
    uint16_t selector;
    asm volatile("mov %%cs, %0" : "=r"(selector));
    if ((selector & 3) != 3) return static_cast<int64_t>(ErrorCode::NotSupported);
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8 asm("r8")   = arg5;
    register uint64_t r9 asm("r9")   = arg6;
    uint64_t result;
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(static_cast<uint64_t>(number)), "D"(arg1), "S"(arg2),
                   "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return static_cast<int64_t>(result);
}


uint64_t CoreServiceClient::openHandle(CoreServiceKind kind) {
    if (kind == CoreServiceKind::Pipe || kind == CoreServiceKind::MessageQueue)
        return ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), kind == CoreServiceKind::Pipe ? "pipe" : "msgq", 1000);
    return invokeSyscall(SyscallNumber::GetBootstrapHandle, static_cast<uint64_t>(BootstrapOperation::GetCoreService), static_cast<uint64_t>(kind));
}
uint64_t CoreServiceClient::handles_[static_cast<uint64_t>(CoreServiceKind::Count)];
int CoreServiceClient::call(CoreServiceKind kind, IpcMessage& request, IpcMessage& reply, uint64_t deadline) {
    if (SystemService::isPrivileged()) return SystemService::coreCall(kind, request, reply, deadline);
    const auto index = static_cast<uint64_t>(kind);
    if (index >= static_cast<uint64_t>(CoreServiceKind::Count)) return static_cast<int>(ErrorCode::InvalidArgument);
    uint64_t handle = __atomic_load_n(&handles_[index], __ATOMIC_ACQUIRE);
    if (!handle) {
        const int64_t created = openHandle(kind);
        if (created <= 0) return static_cast<int>(ErrorCode::NotFound);
        if (Handle::setFlags(created, handleFlagCloseOnExec) != 0) {
            closeHandle(created); return static_cast<int>(ErrorCode::InvalidHandle);
        }
        uint64_t expected = 0;
        if (__atomic_compare_exchange_n(&handles_[index], &expected, created, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) handle = created;
        else { closeHandle(created); handle = expected; }
    }
    IpcMessage outgoing = request;
    const bool tagged = kind == CoreServiceKind::Pipe ? request.type != static_cast<uint64_t>(PipeOperation::Create) && request.type != static_cast<uint64_t>(PipeOperation::CreateChannel)
        : kind == CoreServiceKind::MessageQueue && request.type != static_cast<uint64_t>(MessageQueueOperation::Open) && request.type != static_cast<uint64_t>(MessageQueueOperation::Unlink);
    if (tagged) {
        if (outgoing.handleCount) return static_cast<int>(ErrorCode::InvalidArgument);
        outgoing.handleCount = 1; outgoing.handles[0] = request.data[0]; outgoing.data[0] = 0;
    }
    const int status = callService(handle, outgoing, reply, deadline);
    if (status == static_cast<int>(ErrorCode::PeerDied) || status == static_cast<int>(ErrorCode::EndpointClosed) || status == static_cast<int>(ErrorCode::InvalidHandle)) {
        uint64_t expected = handle;
        if (__atomic_compare_exchange_n(&handles_[index], &expected, 0ULL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) closeHandle(handle);
    }
    return status;
}
int64_t CoreServiceClient::result(CoreServiceKind kind, IpcMessage& request, IpcMessage& reply) {
    const int status = call(kind, request, reply);
    return status ? status : kind == CoreServiceKind::MessageQueue && request.type == static_cast<uint64_t>(MessageQueueOperation::Open) && reply.handleCount ? reply.handles[0] : static_cast<int64_t>(reply.data[0]);
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

int Process::waitThread(uint64_t threadId, int& exitCode, bool noHang) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::WaitThread); request.length = 4;
    request.data[0] = threadId; request.data[1] = noHang;
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!result) exitCode = reply.data[1];
    return result;
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

int Process::setSignalAction(uint32_t signal, const SignalAction* action, SignalAction* oldAction) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SignalAction);
    request.length = 4;
    request.data[0] = signal; request.data[1] = action != nullptr; if (action) Runtime::copyBytes(&request.data[4], action, sizeof(*action)); request.length = 8;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!status && oldAction) Runtime::copyBytes(oldAction, &reply.data[1], sizeof(*oldAction));
    return status;
}

int Process::setSignalMask(int how, uint64_t mask, uint64_t& oldMask) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SignalMask);
    request.length = 4;
    request.data[0] = how; request.data[1] = mask;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!status) oldMask = reply.data[1];
    return status;
}

int Process::getPendingSignals(uint64_t& mask) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::PendingSignals);
    request.length = 4;
    
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!status) mask = reply.data[1];
    return status;
}

int Process::waitSignal(uint64_t mask) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::TakeSignal);
    request.length = 4;
    request.data[0] = mask;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

void Process::signalReturn(void* frame) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SignalReturn);
    request.length = 4; request.data[0] = reinterpret_cast<uint64_t>(frame);
    CoreServiceClient::call(CoreServiceKind::Task, request, reply);
    while (true) asm volatile("ud2");
}

uint64_t MessageQueue::openMessageQueue(const char* name, int flags,
                          uint32_t maximumMessages, uint32_t messageSize) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Open);
    request.length = 5;
    request.data[0] = static_cast<uint32_t>(flags);
    request.data[1] = maximumMessages;
    request.data[2] = messageSize;
    if (!name) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[3]), name, 64);
    request.length = 3+8;
    return CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
}

int MessageQueue::unlinkMessageQueue(const char* name) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Unlink);
    request.length = 5;
    if (!name) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[3]), name, 64);
    request.length = 3+8;
    return CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
}

int MessageQueue::sendMessageQueue(uint64_t handle, const void* data, size_t size,
                     uint32_t priority) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Send);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = size;
    request.data[2] = priority;
    if ((!data && size) || size > 256) return static_cast<int>(ErrorCode::InvalidArgument);
    Runtime::copyBytes(&request.data[3], data, size);
    request.length = 3 + (size + 7) / 8;
    return CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
}

int64_t MessageQueue::receiveMessageQueue(uint64_t handle, void* data, size_t capacity,
                            uint32_t& priority) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Receive);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = capacity > 256 ? 256 : capacity;
    if (!data && capacity) return static_cast<int>(ErrorCode::InvalidArgument);
    const int64_t count = CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
    if (count >= 0 && static_cast<uint64_t>(count) <= request.data[1]) {
        Runtime::copyBytes(data, &reply.data[2], count); priority = reply.data[1];
    }
    return count;
}

int MessageQueue::getMessageQueueAttributes(uint64_t handle,
                              MessageQueueAttributes& attributes) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Attributes);
    request.length = 5;
    request.data[0] = handle;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
    if (!status) Runtime::copyBytes(&attributes, &reply.data[1], sizeof(attributes));
    return status;
}

int MessageQueue::setMessageQueueFlags(uint64_t handle, uint64_t flags,
                         uint64_t& oldFlags) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Flags);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = flags;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
    if (!status) oldFlags = reply.data[1];
    return status;
}

uint64_t Memory::allocatePages(uint32_t order, uint16_t node, uint64_t owner) {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::AllocatePages); request.length = 5;
    request.data[0] = order; request.data[1] = node; request.data[2] = owner;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}
uint64_t Memory::mapPages(uint64_t handle, uint64_t address, uint64_t size, uint64_t offset, uint64_t protection, uint64_t target) {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::MapPages); request.length = 6;
    request.data[0] = handle; request.data[1] = address; request.data[2] = size; request.data[3] = offset; request.data[4] = protection; request.data[5] = target;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}
int Memory::pageInfo(uint64_t handle, PageObjectInfo& info) {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::PageInfo); request.length = 5; request.data[0] = handle;
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}
int Memory::usage(MemoryUsage& info, uint64_t target) {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::Usage); request.length = 5; request.data[0] = target;
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}
int64_t Memory::reclaimCaches() {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::ReclaimCaches); request.length = 5;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}
int Memory::unmapTarget(uint64_t address, uint64_t size, uint64_t target) {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::UnmapTarget); request.length = 5;
    request.data[0] = address; request.data[1] = size; request.data[2] = target;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}
int Memory::protectTarget(uint64_t address, uint64_t size, uint64_t protection, uint64_t target) {
    IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(MemoryOperation::ProtectTarget); request.length = 5;
    request.data[0] = address; request.data[1] = size; request.data[2] = protection; request.data[3] = target;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}

uint64_t Memory::openSharedMemory(const char* name, int flags) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MemoryOperation::Open);
    request.length = 5;
    request.data[0] = static_cast<uint32_t>(flags);
    if (!name) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[5]), name, 64);
    request.length = 5+8;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}

int Memory::unlinkSharedMemory(const char* name) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MemoryOperation::Unlink);
    request.length = 5;
    if (!name) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[5]), name, 64);
    request.length = 5+8;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}

int Memory::resizeSharedMemory(uint64_t handle, uint64_t size) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MemoryOperation::Resize);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = size;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}

uint64_t Memory::mapSharedMemory(uint64_t handle, uint64_t address, uint64_t size,
                         uint64_t offset, uint64_t protection) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MemoryOperation::Map);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = address;
    request.data[2] = size;
    request.data[3] = offset;
    request.data[4] = protection;
    return CoreServiceClient::result(CoreServiceKind::Memory, request, reply);
}

int Process::sendThreadSignal(uint64_t threadId, uint32_t signal) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SendThreadSignal);
    request.length = 4;
    request.data[0] = threadId; request.data[1] = signal;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

int MessageQueue::registerMessageQueueNotification(uint64_t handle, int type,
                                     uint32_t signal) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(MessageQueueOperation::Notify);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = static_cast<uint32_t>(type);
    request.data[2] = signal;
    return CoreServiceClient::result(CoreServiceKind::MessageQueue, request, reply);
}

int64_t forkTask() {
    return invokeSyscall(SyscallNumber::ForkTask);
}

int64_t Process::waitChild(int64_t taskId, int& status, int options) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::WaitChild); request.length = 4;
    request.data[0] = taskId; request.data[1] = options;
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (result >= 0) status = reply.data[1];
    return result;
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

uint64_t Endpoint::create() {
return invokeSyscall(SyscallNumber::GetBootstrapHandle, static_cast<uint64_t>(BootstrapOperation::CreateEndpoint));
}

int sendMessage(uint64_t endpoint, IpcMessage& message, uint64_t deadline) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::SendMessage, endpoint,
                      reinterpret_cast<uint64_t>(&message), deadline));
}

int receiveMessage(uint64_t endpoint, IpcMessage& message, uint64_t deadline) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::ReceiveMessage, endpoint,
                      reinterpret_cast<uint64_t>(&message), deadline));
}

int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply, uint64_t deadline) {
    return static_cast<int>(invokeSyscall(SyscallNumber::CallService, endpoint,
                                          reinterpret_cast<uint64_t>(&request),
                                          reinterpret_cast<uint64_t>(&reply), deadline));
}

int replyMessage(uint64_t endpoint, uint64_t replyToken, IpcMessage& reply) {
    reply.replyToken = replyToken;
    return static_cast<int>(invokeSyscall(SyscallNumber::ReplyMessage, endpoint,
                                          reinterpret_cast<uint64_t>(&reply)));
}

int Endpoint::cancel(uint64_t endpoint, uint64_t threadId) {
    return static_cast<int>(invokeSyscall(SyscallNumber::SendMessage, endpoint,
                                          0, 0, threadId));
}

uint64_t Process::spawnProgram(ProgramId program, uint64_t bootstrapHandle, uint32_t argumentCount, const char* const* arguments) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SpawnProgram); request.length = 4;
    request.data[0] = static_cast<uint64_t>(program); request.data[1] = bootstrapHandle; request.data[2] = argumentCount; request.data[3] = reinterpret_cast<uint64_t>(arguments);
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return result;
}

uint64_t Process::spawnImage(const SpawnImageInfo& info) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SpawnImage); request.length = 4;
    request.data[0] = reinterpret_cast<uint64_t>(&info);
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return result;
}

bool Pipe::createPipe(PipeHandles& handles) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(PipeOperation::Create);
    request.length = 5;
    handles = {};
    if (CoreServiceClient::result(CoreServiceKind::Pipe, request, reply) != 0 || reply.handleCount != 2) return false;
    handles.readHandle = reply.handles[0];
    handles.writeHandle = reply.handles[1];
    return true;
}

bool Pipe::createChannelPair(ChannelHandles& handles) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(PipeOperation::CreateChannel);
    request.length = 5;
    handles = {};
    if (CoreServiceClient::result(CoreServiceKind::Pipe, request, reply) != 0 || reply.handleCount != 2) return false;
    handles.firstHandle = reply.handles[0];
    handles.secondHandle = reply.handles[1];
    return true;
}

uint64_t Pipe::getChannelIdentity(uint64_t handle) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(PipeOperation::Identity);
    request.length = 5;
    request.data[0] = handle;
    const auto value = CoreServiceClient::result(CoreServiceKind::Pipe, request, reply);
    return value > 0 ? value : 0;
}

bool Pipe::setChannelNonblocking(uint64_t handle, bool enabled) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(PipeOperation::Nonblocking);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = enabled ? 1 : 0;
    return CoreServiceClient::result(CoreServiceKind::Pipe, request, reply) == 0;
}

bool Pipe::signalChannelEof(uint64_t handle) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(PipeOperation::Eof);
    request.length = 5;
    request.data[0] = handle;
    return CoreServiceClient::result(CoreServiceKind::Pipe, request, reply) == 0;
}

int64_t Pipe::readPipe(uint64_t handle, void* buffer, size_t size) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(PipeOperation::Read);
    request.length = 5;
    request.data[0] = handle;
    request.data[1] = size > 512 ? 512 : size;
    if (!buffer || !size) return static_cast<int>(ErrorCode::InvalidArgument);
    const int64_t count = CoreServiceClient::result(CoreServiceKind::Pipe, request, reply);
    if (count > 0 && static_cast<uint64_t>(count) <= request.data[1]) Runtime::copyBytes(buffer, &reply.data[1], count);
    return count;
}

int64_t Pipe::writePipe(uint64_t handle, const void* buffer, size_t size) {
if (!buffer && size) return static_cast<int>(ErrorCode::InvalidArgument);
    size_t written = 0;
    while (written < size) {
        IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(PipeOperation::Write);
        const size_t chunk = size - written > 512 ? 512 : size - written;
        request.data[0] = handle; request.data[1] = chunk;
        Runtime::copyBytes(&request.data[3], static_cast<const uint8_t*>(buffer) + written, chunk);
        request.length = 3 + (chunk + 7) / 8;
        const int64_t count = CoreServiceClient::result(CoreServiceKind::Pipe, request, reply);
        if (count <= 0) return written ? static_cast<int64_t>(written) : count;
        written += count;
    }
    return written;
}

uint64_t getStandardHandle(uint32_t index) {
    return invokeSyscall(SyscallNumber::GetStandardHandle, index);
}

uint64_t getBootstrapHandle() {
    return invokeSyscall(SyscallNumber::GetBootstrapHandle);
}

void System::log(const char* message) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(SystemOperation::Log);
    request.length = 5;
    if (!message) return;
    Runtime::copyString(reinterpret_cast<char*>(request.data), message, 512);
    request.length = 64;
    CoreServiceClient::result(CoreServiceKind::System, request, reply);
}

uint64_t Pci::pciServiceHandle() {
    static uint64_t handle;
    if (handle == 0)
        handle = ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), pciServiceName);
    return handle;
}

int Pci::enumeratePci(PciFunctionInfo* functions, uint32_t capacity) {
    if (functions == nullptr || capacity == 0 || capacity > 64) return -1;
    const uint64_t service = Pci::pciServiceHandle();
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
    Runtime::copyBytes(functions, &reply.data[2], reply.data[1] * sizeof(PciFunctionInfo));
    return static_cast<int>(reply.data[1]);
}

int Pci::readPciConfig(const PciFunctionInfo& function, uint16_t offset,
                  uint32_t& value) {
    const uint64_t service = Pci::pciServiceHandle();
    if (service == 0) return -1;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(PciServiceRequest::ReadConfig);
    request.data[0] = offset;
    Runtime::copyBytes(&request.data[1], &function, sizeof(function));
    request.length = 3;
    IpcMessage reply{};
    const int result = callService(service, request, reply);
    if (result == 0 && reply.data[0] == 0 && reply.length >= 2)
        value = static_cast<uint32_t>(reply.data[1]);
    else return -1;
    return result;
}

int Pci::writePciConfig(const PciFunctionInfo& function, uint16_t offset,
                   uint32_t value) {
    const uint64_t service = Pci::pciServiceHandle();
    if (service == 0) return -1;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(PciServiceRequest::WriteConfig);
    request.data[0] = offset;
    Runtime::copyBytes(&request.data[1], &function, sizeof(function));
    request.data[3] = value;
    request.length = 4;
    IpcMessage reply{};
    const int result = callService(service, request, reply);
    return result == 0 && reply.data[0] == 0 ? 0 : -1;
}

uint64_t Device::mapDeviceMmio(const PciFunctionInfo& function, uint8_t barIndex,
                       uint64_t address, uint64_t size) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(DeviceOperation::MapMmio);
    request.length = 5;
    request.data[0] = barIndex;
    request.data[1] = address;
    request.data[2] = size;
    Runtime::copyBytes(&request.data[3], &function, sizeof(function));
    return CoreServiceClient::result(CoreServiceKind::Device, request, reply);
}

int Device::allocateDma(uint64_t address, DmaBufferInfo& info) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(DeviceOperation::AllocateDma);
    request.length = 5;
    request.data[0] = address;
    const auto status = CoreServiceClient::result(CoreServiceKind::Device, request, reply);
    if (!status) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return status;
}

uint64_t Device::bindIrq(uint8_t irq, uint64_t endpoint, bool levelTriggered) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(DeviceOperation::BindIrq);
    request.length = 5;
    request.data[0] = irq;
    request.data[1] = endpoint;
    request.data[2] = levelTriggered ? 1 : 0;
    return CoreServiceClient::result(CoreServiceKind::Device, request, reply);
}

int Device::acknowledgeIrq(uint64_t binding) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(DeviceOperation::AcknowledgeIrq);
    request.length = 5;
    request.data[0] = binding;
    return CoreServiceClient::result(CoreServiceKind::Device, request, reply);
}

int Process::getTaskState(uint64_t taskHandle, int& exitCode) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::State);
    request.length = 4;
    request.data[0] = taskHandle;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (status >= 0) exitCode = static_cast<int>(reply.data[1]);
    return status;
}

int Process::getTaskInfo(uint64_t taskHandle, TaskInfo& info) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::Info);
    request.length = 4;
    request.data[0] = taskHandle;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!status) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return status;
}

int Process::terminateTask(uint64_t taskHandle, int exitCode) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::Terminate);
    request.length = 4;
    request.data[0] = taskHandle; request.data[1] = static_cast<int64_t>(exitCode);
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

int Process::sendSignal(uint64_t taskId, uint32_t signal) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SendSignal);
    request.length = 4;
    request.data[0] = taskId; request.data[1] = signal;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

int Process::setTaskGroup(uint64_t taskGroupId, uint64_t taskId) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SetGroup);
    request.length = 4;
    request.data[0] = taskGroupId; request.data[1] = taskId;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

int64_t Process::createSession() {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::CreateSession);
    request.length = 4;
    
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

int Process::getTaskIdentifiers(TaskIdentifiers& identifiers, uint64_t taskId) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::Identifiers);
    request.length = 4;
    request.data[0] = taskId;
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!status) Runtime::copyBytes(&identifiers, &reply.data[1], sizeof(identifiers));
    return status;
}

uint64_t Clock::getClockResolution() {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(SystemOperation::ClockResolution);
    request.length = 5;
    return CoreServiceClient::result(CoreServiceKind::System, request, reply);
}


bool closeHandle(uint64_t handle) {
    return invokeSyscall(SyscallNumber::CloseHandle, handle) == 0;
}

Handle::~Handle() { reset(); }
void Handle::reset(uint64_t value) {
    if (value_) closeHandle(value_);
    value_ = value;
}
int64_t Handle::duplicate(uint64_t handle, uint32_t rights, uint32_t flags) {
    return invokeSyscall(SyscallNumber::CloseHandle, handle,
        static_cast<uint64_t>(HandleOperation::Duplicate), rights, flags);
}
int Handle::restrictRights(uint64_t handle, uint32_t rights) {
    return invokeSyscall(SyscallNumber::CloseHandle, handle,
        static_cast<uint64_t>(HandleOperation::RestrictRights), rights);
}
int Handle::setFlags(uint64_t handle, uint32_t flags) {
    return invokeSyscall(SyscallNumber::CloseHandle, handle,
        static_cast<uint64_t>(HandleOperation::SetFlags), 0, flags);
}
int Handle::state(uint64_t handle) {
    return invokeSyscall(SyscallNumber::CloseHandle, handle, static_cast<uint64_t>(HandleOperation::GetState));
}

uint64_t Process::getTaskId() {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::Identity);
    request.length = 4;
    
    const int64_t status = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    
    return status;
}

uint64_t Process::getThreadId() {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::ThreadIdentity); request.length = 4;
    return CoreServiceClient::result(CoreServiceKind::Task, request, reply);
}

bool System::getSystemInfo(SystemInfo& info) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(SystemOperation::Info);
    request.length = 5;
    const auto status = CoreServiceClient::result(CoreServiceKind::System, request, reply);
    if (!status) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return status == 0;
}

uint64_t Clock::getMonotonicTime() {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(SystemOperation::MonotonicTime);
    request.length = 5;
    return CoreServiceClient::result(CoreServiceKind::System, request, reply);
}

uint64_t Clock::getWallClockTime() {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(SystemOperation::WallClockTime);
    request.length = 5;
    return CoreServiceClient::result(CoreServiceKind::System, request, reply);
}

int Device::mapFramebuffer(uint64_t address, DisplayInfo& info) {
IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(DeviceOperation::Framebuffer);
    request.length = 5;
    request.data[0] = address;
    const auto status = CoreServiceClient::result(CoreServiceKind::Device, request, reply);
    if (!status) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return status;
}

bool unmapMemory(uint64_t address, uint64_t size) {
    return invokeSyscall(SyscallNumber::UnmapMemory, address, size) == 0;
}

bool protectMemory(uint64_t address, uint64_t size, uint64_t protection) {
    return invokeSyscall(SyscallNumber::ProtectMemory, address, size,
                         protection) == 0;
}

void* Memory::allocateMemory(size_t size) {
    const uint64_t mappedSize = HeapState::alignToPage(size);
    if (mappedSize == 0) {
        return nullptr;
    }
    HeapState::lockHeap();
    for (HeapState::Allocation& allocation : HeapState::allocations) {
        if (allocation.address != 0) {
            continue;
        }
        const uint64_t address = HeapState::nextAllocationAddress;
        if (address + mappedSize < address ||
            mapMemory(address, mappedSize, 1) != address) {
            HeapState::unlockHeap();
            return nullptr;
        }
        allocation = {address, mappedSize};
        HeapState::nextAllocationAddress += mappedSize + HeapState::pageSize;
        HeapState::unlockHeap();
        return reinterpret_cast<void*>(address);
    }
    HeapState::unlockHeap();
    return nullptr;
}

bool Memory::freeMemory(void* pointer) {
    const uint64_t address = reinterpret_cast<uint64_t>(pointer);
    HeapState::lockHeap();
    for (HeapState::Allocation& allocation : HeapState::allocations) {
        if (allocation.address == address) {
            const uint64_t size = allocation.size;
            allocation          = {};
            const bool result   = unmapMemory(address, size);
            HeapState::unlockHeap();
            return result;
        }
    }
    HeapState::unlockHeap();
    return false;
}

int Runtime::printString(const char* string) {
    if (string == nullptr) {
        return static_cast<int>(ErrorCode::InvalidArgument);
    }
    const int64_t result = Terminal::write(string, Runtime::stringLength(string));
    return result < 0 ? static_cast<int>(result) : 0;
}

uint64_t Terminal::endpoint_;
uint32_t Terminal::id_;

void Terminal::initialize() { endpoint_ = 0; id_ = 0; }

uint32_t Terminal::getId() { return id_; }

void Terminal::setId(uint32_t id) {
    id_ = id <= graphicalTerminalId ? id : 0;
}

int64_t Terminal::write(const void* buffer, size_t size) {
    if (buffer == nullptr) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    if (endpoint_ == 0) {
        endpoint_ =
            ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), terminalServiceName, 1000);
    }
    if (endpoint_ == 0) {
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
        request.data[0] = id_;
        request.data[1] = chunk;
        for (size_t index = 0; index < chunk; ++index) {
            reinterpret_cast<uint8_t*>(&request.data[2])[index] =
                static_cast<const uint8_t*>(buffer)[offset + index];
        }
        request.length = 2 + (chunk + sizeof(uint64_t) - 1) / sizeof(uint64_t);
        IpcMessage reply{};
        if (callService(endpoint_, request, reply) != 0 ||
            static_cast<int64_t>(reply.data[0]) != 0) {
            return offset == 0 ? static_cast<int64_t>(ErrorCode::IoError)
                               : static_cast<int64_t>(offset);
        }
        offset += reply.data[1];
    }
    return static_cast<int64_t>(offset);
}

int64_t Terminal::read(void* buffer, size_t size) {
    if (buffer == nullptr || size == 0) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    if (endpoint_ == 0) {
        endpoint_ =
            ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), terminalServiceName, 1000);
    }
    if (endpoint_ == 0) {
        return static_cast<int64_t>(ErrorCode::NotFound);
    }
    while (true) {
        IpcMessage request{};
        request.type    = static_cast<uint64_t>(TerminalRequest::Read);
        request.data[0] = id_;
        request.data[1] =
            size > 6 * sizeof(uint64_t) ? 6 * sizeof(uint64_t) : size;
        request.length = 2;
        IpcMessage reply{};
        if (callService(endpoint_, request, reply) != 0) {
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

uint32_t Terminal::getActive() {
    if (endpoint_ == 0) {
        endpoint_ =
            ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), terminalServiceName, 1000);
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(TerminalRequest::GetActive);
    IpcMessage reply{};
    return endpoint_ != 0 &&
                   callService(endpoint_, request, reply) == 0
               ? static_cast<uint32_t>(reply.data[1])
               : 0;
}

bool Terminal::switchTo(uint32_t id) {
    if (endpoint_ == 0) {
        endpoint_ =
            ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), terminalServiceName, 1000);
    }
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(TerminalRequest::Switch);
    request.data[0] = id;
    request.length  = 1;
    IpcMessage reply{};
    return endpoint_ != 0 &&
           callService(endpoint_, request, reply) == 0 &&
           reply.data[0] == 0;
}

bool Terminal::setForegroundTask(uint64_t taskId) {
    if (endpoint_ == 0) {
        endpoint_ =
            ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), terminalServiceName, 1000);
    }
    IpcMessage request{};
    request.type    =
        static_cast<uint64_t>(TerminalRequest::SetForegroundTask);
    request.data[0] = id_;
    request.data[1] = taskId;
    request.length  = 2;
    IpcMessage reply{};
    return endpoint_ != 0 &&
           callService(endpoint_, request, reply) == 0 &&
           reply.data[0] == 0;
}

const char* Runtime::environmentValue(const char* name) {
    if (!name || !*name) return nullptr;
    size_t length = 0;
    while (name[length]) {
        if (name[length] == '=') return nullptr;
        ++length;
    }
    char** environment = environmentSlot_ ? *environmentSlot_ : initialEnvironment_;
    if (!environment) return nullptr;
    for (size_t index = 0; environment[index]; ++index) {
        const char* entry = environment[index];
        size_t position = 0;
        while (position < length && entry[position] == name[position]) ++position;
        if (position == length && entry[position] == '=') return entry + length + 1;
    }
    return nullptr;
}

void Runtime::bindEnvironment(char*** environment) { environmentSlot_ = environment; }

void Runtime::prepare(char** environment) {
    if (prepared_) return;
    prepared_ = true;
    initialEnvironment_ = environment;
    environmentSlot_ = nullptr;
    sharedFinalizer_ = nullptr;
    if (environment) {
        char** end = environment;
        while (*end) ++end;
        const auto* auxiliary = reinterpret_cast<const uint64_t*>(end + 1);
        for (uint32_t index = 0; index < 32 && auxiliary[index * 2]; ++index)
            if (auxiliary[index * 2] == 0x60000003)
                sharedFinalizer_ = reinterpret_cast<void (*)()>(auxiliary[index * 2 + 1]);
    }
    HeapState::heapLock              = 0;
    HeapState::nextAllocationAddress = HeapState::heapBase;
    Terminal::initialize();
    Runtime::clearMemory(HeapState::allocations, sizeof(HeapState::allocations));
}

int Runtime::runtimeInit(int argumentCount, char** arguments, char** environment) {
    prepare(environment);
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
        Terminal::setId(static_cast<uint32_t>(argument[position] - '1'));
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

void Runtime::runtimeFini() {
    for (RuntimeFunction* destructor = __fini_array_end;
         destructor != __fini_array_start;) {
        (*--destructor)();
    }
    if (sharedFinalizer_) {
        auto finalizer = sharedFinalizer_;
        sharedFinalizer_ = nullptr;
        finalizer();
    }
}

size_t Runtime::stringLength(const char* value) {
    size_t length = 0;
    while (value != nullptr && value[length] != '\0') {
        ++length;
    }
    return length;
}

bool Runtime::stringsEqual(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

void Runtime::copyString(char* destination, const char* source, size_t capacity) {
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

void Runtime::clearMemory(void* destination, size_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void Runtime::copyBytes(void* destination, const void* source, size_t size) {
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

extern "C" int osRuntimeInit(int argc, char** argv, char** environment) {
    return os::Runtime::runtimeInit(argc, argv, environment);
}
extern "C" void osRuntimeFini() { os::Runtime::runtimeFini(); }

namespace os {
namespace {
class VirtualMemoryClient {
    inline static uint64_t endpoint_ = 0;
    inline static uint8_t lock_ = 0;
   public:
    static int64_t call(IpcMessage& request) {
        while (__atomic_test_and_set(&lock_, __ATOMIC_ACQUIRE)) yield();
        if (!endpoint_) {
            endpoint_ = ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), "mm", 1);
            if (endpoint_) Handle::setFlags(endpoint_, handleFlagCloseOnExec);
        }
        IpcMessage reply{};
        const int status = endpoint_ ? callService(endpoint_, request, reply) : static_cast<int>(ErrorCode::NotFound);
        if (status == static_cast<int>(ErrorCode::PeerDied) || status == static_cast<int>(ErrorCode::EndpointClosed) || status == static_cast<int>(ErrorCode::InvalidHandle)) {
            if (endpoint_) closeHandle(endpoint_);
            endpoint_ = 0;
        }
        __atomic_clear(&lock_, __ATOMIC_RELEASE);
        return status ? status : reply.handleCount ? static_cast<int64_t>(reply.handles[0]) : static_cast<int64_t>(reply.data[0]);
    }
};
}
uint64_t VirtualMemory::anonymous(uint64_t size, uint32_t protection, bool lazy, uint64_t hint) {
    if (!size || size > UINT64_MAX - 4095) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type = static_cast<uint64_t>(MmOperation::MapAnonymous); request.length = 4;
    request.data[0] = (size + 4095) & ~4095ULL; request.data[1] = protection;
    request.data[2] = lazy ? 0 : mmFlagEager; request.data[3] = hint;
    return VirtualMemoryClient::call(request);
}
uint64_t VirtualMemory::shared(uint64_t handle, uint64_t size, uint32_t protection, uint64_t offset, uint64_t hint) {
    if (!handle || !size || size > UINT64_MAX - 4095) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type = static_cast<uint64_t>(MmOperation::MapShared); request.length = 5;
    request.handleCount = 1; request.handles[0] = handle;
    request.data[0] = (size + 4095) & ~4095ULL; request.data[1] = protection;
    request.data[2] = mmFlagShared; request.data[3] = offset; request.data[4] = hint;
    return VirtualMemoryClient::call(request);
}
uint64_t VirtualMemory::file(uint64_t descriptor, uint64_t size, uint32_t protection, uint64_t offset, uint64_t hint) {
    if (!descriptor || !size || size > UINT64_MAX - 4095) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    const uint64_t fs = ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), fsServiceName, 1);
    if (!fs) return static_cast<int64_t>(ErrorCode::NotFound);
    IpcMessage delegate{}, reply{};
    delegate.type = static_cast<uint64_t>(FsRequest::DelegateMapping); delegate.length = 1; delegate.data[0] = descriptor;
    const int status = callService(fs, delegate, reply);
    if (status || static_cast<int64_t>(reply.data[0])) {
        closeHandle(fs); return status ? status : reply.data[0];
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(MmOperation::MapFile); request.length = 6;
    request.data[0] = (size + 4095) & ~4095ULL; request.data[1] = protection;
    request.data[3] = offset; request.data[4] = hint; request.data[5] = reply.data[1];
    const int64_t result = VirtualMemoryClient::call(request);
    // An unclaimed delegation belongs to the issuer and must not leak on failure.
    if (result < 0) {
        IpcMessage close{}, ignored{}; close.type = static_cast<uint64_t>(FsRequest::Close);
        close.length = 1; close.data[0] = reply.data[2]; callService(fs, close, ignored);
    }
    closeHandle(fs); return result;
}
int VirtualMemory::unmap(uint64_t address, uint64_t size) {
    if (!size || size > UINT64_MAX - 4095) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type = static_cast<uint64_t>(MmOperation::Unmap); request.length = 2;
    request.data[0] = address; request.data[1] = (size + 4095) & ~4095ULL;
    return VirtualMemoryClient::call(request);
}
int VirtualMemory::protect(uint64_t address, uint64_t size, uint32_t protection) {
    if (!size || size > UINT64_MAX - 4095) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type = static_cast<uint64_t>(MmOperation::Protect); request.length = 3;
    request.data[0] = address; request.data[1] = (size + 4095) & ~4095ULL; request.data[2] = protection;
    return VirtualMemoryClient::call(request);
}
int VirtualMemory::sync(uint64_t address, uint64_t size) {
    if (!size || size > UINT64_MAX - 4095) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}; request.type = static_cast<uint64_t>(MmOperation::Sync); request.length = 2;
    request.data[0] = address; request.data[1] = (size + 4095) & ~4095ULL;
    return VirtualMemoryClient::call(request);
}
uint64_t VirtualMemory::openShared(const char* name, int flags) {
    if (!name || Runtime::stringLength(name) >= 64) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    IpcMessage request{}; request.type = static_cast<uint64_t>(MmOperation::SharedOpen); request.length = 9;
    request.data[0] = flags; Runtime::copyString(reinterpret_cast<char*>(&request.data[1]), name, 64);
    return VirtualMemoryClient::call(request);
}
int VirtualMemory::unlinkShared(const char* name) {
    if (!name || Runtime::stringLength(name) >= 64) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}; request.type = static_cast<uint64_t>(MmOperation::SharedUnlink); request.length = 9;
    Runtime::copyString(reinterpret_cast<char*>(&request.data[1]), name, 64);
    return VirtualMemoryClient::call(request);
}
}  // namespace os

namespace os {
uint64_t Handle::identity(uint64_t handle) { return invokeSyscall(SyscallNumber::CloseHandle, handle, static_cast<uint64_t>(HandleOperation::ObjectIdentity)); }
int64_t Handle::references(uint64_t handle) { return invokeSyscall(SyscallNumber::CloseHandle, handle, static_cast<uint64_t>(HandleOperation::References)); }
int Handle::destroyEndpoint(uint64_t handle) { return invokeSyscall(SyscallNumber::CloseHandle, handle, static_cast<uint64_t>(HandleOperation::DestroyEndpoint)); }
int Handle::requestActive(uint64_t endpoint, uint64_t token) { return invokeSyscall(SyscallNumber::CloseHandle, endpoint, static_cast<uint64_t>(HandleOperation::RequestActive), token); }
}
