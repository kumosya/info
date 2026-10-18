#include "kernel/syscall.h"

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/signal.h"
#include "kernel/sync.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/services.h"
#include "kernel/core_service.h"
#include "os/pci.h"

namespace {

constexpr uint32_t ia32Efer           = 0xc0000080;
constexpr uint32_t ia32Star           = 0xc0000081;
constexpr uint32_t ia32Lstar          = 0xc0000082;
constexpr uint32_t ia32Fmask          = 0xc0000084;
constexpr uint64_t pageSize           = 4096;
constexpr uint64_t minimumUserAddress = 0x10000;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
constexpr uint64_t maximumMapSize     = 64ULL * 1024 * 1024;
constexpr uint64_t directMapBase      = 0xffff800000000000ULL;

static_assert(sizeof(kernel::SignalAction) == sizeof(os::SignalAction));
static_assert(__is_standard_layout(kernel::SignalAction));
static_assert(sizeof(kernel::MessageQueueAttributes) ==
              sizeof(os::MessageQueueAttributes));
static_assert(__is_standard_layout(kernel::MessageQueueAttributes));
static_assert(sizeof(kernel::SyscallContext) == 18 * sizeof(uint64_t));
static_assert(__builtin_offsetof(kernel::SyscallContext, rax) == 14 * 8);
static_assert(__builtin_offsetof(kernel::SyscallContext, userRip) == 15 * 8);

uint64_t error(kernel::SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}

bool validUserRange(uint64_t address, uint64_t size) {
    if (size == 0 || address < minimumUserAddress) {
        return false;
    }
    const uint64_t end = address + size;
    return end > address && end <= maximumUserAddress;
}

uint64_t mapUserMemory(uint64_t address, uint64_t size, uint64_t protection) {
    kernel::Task* task = kernel::task::getCurrent();
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    if ((protection & 3) == 3) {
        return error(kernel::SyscallError::PermissionDenied);
    }

    kernel::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());
    uint64_t flags = kernel::pageUser;
    if ((protection & 1) != 0) {
        flags |= kernel::pageWritable;
    }
    if ((protection & 2) == 0) {
        flags |= kernel::pageNoExecute;
    }

    uint64_t mappedSize = 0;
    while (mappedSize < size) {
        const uint64_t physicalAddress = kernel::memory::allocatePhysicalPage();
        if (physicalAddress == 0 ||
            !kernel::memory::mapPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(),
                address + mappedSize, physicalAddress, flags)) {
            if (physicalAddress != 0) {
                kernel::memory::freePhysicalPage(physicalAddress);
            }
            while (mappedSize != 0) {
                mappedSize -= pageSize;
                const uint64_t physical =
                    kernel::memory::getPhysicalAddressInAddressSpace(
                        task->addressSpace().rootPhysicalAddress(),
                        address + mappedSize);
                kernel::memory::unmapPageInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + mappedSize);
                kernel::memory::freePhysicalPage(physical & ~(pageSize - 1));
            }
            return error(kernel::SyscallError::OutOfMemory);
        }
        uint8_t* page =
            reinterpret_cast<uint8_t*>(directMapBase + physicalAddress);
        for (uint64_t index = 0; index < pageSize; ++index) {
            page[index] = 0;
        }
        mappedSize += pageSize;
    }
    return address;
}

uint64_t unmapUserMemory(uint64_t address, uint64_t size) {
    kernel::Task* task = kernel::task::getCurrent();
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size)) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    kernel::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags = 0;
        const uint64_t physical =
            kernel::memory::getPhysicalAddressInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                &flags);
        if (physical == 0 || (flags & kernel::pageUser) == 0) {
            return error(kernel::SyscallError::NotFound);
        }
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags = 0;
        const uint64_t physical =
            kernel::memory::getPhysicalAddressInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                &flags);
        kernel::memory::unmapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), address + offset);
        if ((flags & kernel::pageExternal) == 0) {
            kernel::memory::freePhysicalPage(physical & ~(pageSize - 1));
        }
    }
    kernel::SharedMemory::releaseMapping(*task, address, size);
    return 0;
}

uint64_t protectUserMemory(uint64_t address, uint64_t size,
                           uint64_t protection) {
    kernel::Task* task = kernel::task::getCurrent();
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0 || (protection & 3) == 3) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    kernel::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());
    if (!kernel::SharedMemory::protectionAllowed(*task, address, size,
                                                 protection)) {
        return error(kernel::SyscallError::PermissionDenied);
    }
    uint64_t flags = kernel::pageUser;
    if ((protection & 1) != 0) flags |= kernel::pageWritable;
    if ((protection & 2) == 0) flags |= kernel::pageNoExecute;
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        if (!kernel::memory::protectPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                flags)) {
            return error(kernel::SyscallError::InvalidArgument);
        }
    }
    return 0;
}

kernel::HandleTable::Pin resolveEndpoint(uint64_t handle, uint32_t rights) {
    return kernel::HandleTable::Pin(kernel::task::getCurrent()->handles(), handle,
                                    kernel::HandleType::Endpoint, rights);
}

}  // namespace

namespace kernel::syscall {

void init() {
    constexpr uint64_t kernelCodeSelector = 0x08;
    constexpr uint64_t userSelectorBase   = 0x10;
    cpu::writeMsr(ia32Efer, cpu::readMsr(ia32Efer) | 1);
    cpu::writeMsr(ia32Star,
                  (userSelectorBase << 48) | (kernelCodeSelector << 32));
    cpu::writeMsr(ia32Lstar, reinterpret_cast<uint64_t>(syscallEntry));
    cpu::writeMsr(ia32Fmask,
                  (1ULL << 8) | (1ULL << 9) | (1ULL << 10) | (1ULL << 18));
    serial::serialWrite("[SYSCALL] SYSCALL/SYSRET initialized\n");
}

bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size) {
    Task* task             = task::getCurrent();
    const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);
    if (kernelBuffer == nullptr || task == nullptr ||
        !(validUserRange(address, size) || (task->isSystemService() && address >= directMapBase && address + size > address))) {
        return false;
    }
    PreemptionSpinLockGuard guard(task->addressSpace().lock());
    if (!memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    address, size, false)) {
        return false;
    }
    uint8_t* destination  = static_cast<uint8_t*>(kernelBuffer);
    const uint8_t* source = static_cast<const uint8_t*>(userBuffer);
    for (size_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
    return true;
}

bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size) {
    Task* task             = task::getCurrent();
    const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);
    if (kernelBuffer == nullptr || task == nullptr ||
        !(validUserRange(address, size) || (task->isSystemService() && address >= directMapBase && address + size > address))) {
        return false;
    }
    PreemptionSpinLockGuard guard(task->addressSpace().lock());
    if (!memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    address, size, true)) {
        return false;
    }
    uint8_t* destination  = static_cast<uint8_t*>(userBuffer);
    const uint8_t* source = static_cast<const uint8_t*>(kernelBuffer);
    for (size_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
    return true;
}

namespace {

using SyscallHandler = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t);

uint64_t handleExitTask(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t) {
    // serial::serialWrite("[SYSCALL] user task requested exit\n");
    task::exitCurrent(static_cast<int>(exitCode));
}

uint64_t handleCreateThread(uint64_t entryPoint, uint64_t stackPointer,
                            uint64_t argument, uint64_t, uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    if (task == nullptr ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    entryPoint, 1, false) ||
        stackPointer < 8 ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    stackPointer - 8, 8, true)) {
        return error(SyscallError::InvalidArgument);
    }
    Thread* thread =
        Thread::createUser(task, entryPoint, stackPointer, argument);
    return thread == nullptr ? error(SyscallError::OutOfMemory)
                             : thread->getId();
}

uint64_t handleExitThread(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    Thread::exitCurrent(static_cast<int>(exitCode));
}

uint64_t handleYield(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                     uint64_t) {
    schedule::yieldThread();
    return 0;
}

uint64_t handleSleep(uint64_t milliseconds, uint64_t, uint64_t, uint64_t,
                     uint64_t, uint64_t) {
    Thread::sleepCurrent(milliseconds);
    return 0;
}

uint64_t handleMapMemory(uint64_t address, uint64_t size, uint64_t protection,
                         uint64_t, uint64_t, uint64_t) {
    return mapUserMemory(address, size, protection);
}

uint64_t handleUnmapMemory(uint64_t address, uint64_t size, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
    return unmapUserMemory(address, size);
}

uint64_t handleProtectMemory(uint64_t address, uint64_t size,
                             uint64_t protection, uint64_t, uint64_t,
                             uint64_t) {
    return protectUserMemory(address, size, protection);
}

uint64_t handleCreateEndpoint(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t) {
    Endpoint* endpoint = Endpoint::create();
    if (endpoint == nullptr) {
        return error(SyscallError::OutOfMemory);
    }
    const uint64_t handle = task::getCurrent()->handles().add(
        endpoint, HandleType::Endpoint,
        handleRightRead | handleRightWrite | handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleSendMessage(uint64_t endpointHandle, uint64_t userMessage,
                           uint64_t deadline, uint64_t cancelThreadId, uint64_t, uint64_t) {
    auto endpointPin = resolveEndpoint(endpointHandle, !userMessage && cancelThreadId ? 0 : handleRightWrite);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage message{};
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (!userMessage && cancelThreadId) return endpoint->cancel(cancelThreadId);
    if (!copyFromUser(&message, reinterpret_cast<const void*>(userMessage),
                      sizeof(message))) {
        return error(SyscallError::InvalidArgument);
    }
    return static_cast<uint64_t>(endpoint->send(&message, deadline));
}

uint64_t handleReceiveMessage(uint64_t endpointHandle, uint64_t userMessage,
                              uint64_t deadline, uint64_t, uint64_t, uint64_t) {
    auto endpointPin = resolveEndpoint(endpointHandle, handleRightRead);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage message{};
    Task* task = task::getCurrent();
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (task == nullptr ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    userMessage, sizeof(message), true)) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = endpoint->receive(&message, deadline);
    if (result) return static_cast<uint64_t>(result);
    if (copyToUser(reinterpret_cast<void*>(userMessage), &message, sizeof(message))) return 0;
    for (uint32_t i = 0; i < message.handleCount; ++i) task->handles().close(message.handles[i]);
    return error(SyscallError::InvalidArgument);
}

uint64_t handleCallService(uint64_t endpointHandle, uint64_t userRequest,
                           uint64_t userReply, uint64_t deadline, uint64_t, uint64_t) {
    auto endpointPin = resolveEndpoint(endpointHandle, handleRightWrite);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage request{};
    IpcMessage reply{};
    Task* task = task::getCurrent();
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (task == nullptr ||
        !copyFromUser(&request, reinterpret_cast<const void*>(userRequest),
                      sizeof(request)) ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    userReply, sizeof(reply), true)) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = endpoint->call(&request, &reply, deadline);
    if (result) return static_cast<uint64_t>(result);
    if (copyToUser(reinterpret_cast<void*>(userReply), &reply, sizeof(reply))) return 0;
    for (uint32_t i = 0; i < reply.handleCount; ++i) task->handles().close(reply.handles[i]);
    return error(SyscallError::InvalidArgument);
}

uint64_t handleReplyMessage(uint64_t endpointHandle, uint64_t userReply,
                            uint64_t, uint64_t, uint64_t, uint64_t) {
    auto endpointPin = resolveEndpoint(endpointHandle, handleRightRead);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage reply{};
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (!copyFromUser(&reply, reinterpret_cast<const void*>(userReply),
                      sizeof(reply))) {
        return error(SyscallError::InvalidArgument);
    }
    return static_cast<uint64_t>(endpoint->reply(&reply));
}

uint64_t elfLoadError(ElfLoadError loadError) {
    switch (loadError) {
        case ElfLoadError::OutOfMemory:
            return error(SyscallError::OutOfMemory);
        case ElfLoadError::UnsupportedClass:
        case ElfLoadError::UnsupportedEndian:
        case ElfLoadError::UnsupportedVersion:
        case ElfLoadError::UnsupportedMachine:
        case ElfLoadError::UnsupportedType:
        case ElfLoadError::UnsupportedProgramHeader:
            return error(SyscallError::NotSupported);
        default:
            return error(SyscallError::InvalidArgument);
    }
}

uint64_t handleGetBootstrapHandle(uint64_t operation, uint64_t kind, uint64_t, uint64_t,
                                  uint64_t, uint64_t) {
    switch (static_cast<os::BootstrapOperation>(operation)) {
        case os::BootstrapOperation::CreateEndpoint:
            return handleCreateEndpoint(0, 0, 0, 0, 0, 0);
        case os::BootstrapOperation::GetCoreService:
            return CoreServices::getHandle(*task::getCurrent(), static_cast<os::CoreServiceKind>(kind));
        case os::BootstrapOperation::GetHandle: break;
        default: return error(SyscallError::InvalidArgument);
    }
    Task* task = task::getCurrent();
    return task == nullptr ? 0 : task->bootstrapHandle();
}

uint64_t handleCloseHandle(uint64_t handle, uint64_t operation, uint64_t rights,
                           uint64_t flags, uint64_t, uint64_t) {
    auto& handles = task::getCurrent()->handles();
    switch (static_cast<os::HandleOperation>(operation)) {
        case os::HandleOperation::Duplicate: {
            if (rights > UINT32_MAX || flags > UINT32_MAX) return error(SyscallError::InvalidArgument);
            const uint64_t result = handles.duplicate(handle, rights, flags);
            return result ? result : error(SyscallError::InvalidArgument);
        }
        case os::HandleOperation::RestrictRights:
            return rights <= UINT32_MAX && handles.restrictRights(handle, rights)
                ? 0 : error(SyscallError::PermissionDenied);
        case os::HandleOperation::SetFlags:
            return flags <= UINT32_MAX && handles.setFlags(handle, flags)
                ? 0 : error(SyscallError::InvalidArgument);
        case os::HandleOperation::GetState: {
            HandleTable::Pin pin(handles, handle, HandleType::Endpoint, 0);
            auto* endpoint = pin.as<Endpoint>();
            return endpoint ? (endpoint->isClosed() ? 1 : 0) : error(SyscallError::InvalidHandle);
        }
        case os::HandleOperation::Close: break;
        default: return error(SyscallError::InvalidArgument);
    }
    return task::getCurrent()->handles().close(handle)
               ? 0
               : error(SyscallError::InvalidHandle);
}

uint64_t handleGetThreadId(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t) {
    Thread* thread = Thread::getCurrent();
    return thread == nullptr ? 0 : thread->getId();
}

uint64_t handleForkTask(SyscallContext* context) {
    Task* parent    = task::getCurrent();
    Thread* current = Thread::getCurrent();
    if (context == nullptr || parent == nullptr || current == nullptr ||
        parent == task::getIdle()) {
        return error(SyscallError::InvalidArgument);
    }
    Task* child = Task::create(parent->name(), parent);
    if (child == nullptr) return error(SyscallError::OutOfMemory);
    bool cloned = false;
    {
        PreemptionSpinLockGuard addressGuard(parent->addressSpace().lock());
        cloned = memory::cloneAddressSpaceRoot(
            parent->addressSpace().rootPhysicalAddress(),
            child->addressSpace().rootPhysicalAddress());
    }
    if (!cloned || !SharedMemory::cloneTaskMappings(*parent, *child) ||
        !child->inheritForFork(*parent)) {
        child->destroy();
        return error(SyscallError::OutOfMemory);
    }
    SyscallContext childContext     = *context;
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    childContext.userRflags = (childContext.userRflags & allowedFlags) | 0x202;
    if (Thread::createForkedUser(child, childContext,
                                 current->getSignalMask()) == nullptr) {
        child->destroy();
        return error(SyscallError::OutOfMemory);
    }
    return child->id();
}

bool handleReplaceTaskImage(SyscallContext* context, uint64_t& failureResult) {
    constexpr uint32_t maximumArgumentCount    = 64;
    constexpr uint32_t maximumEnvironmentCount = 16;
    constexpr uint32_t maximumSharedImageCount = 4;
    constexpr uint32_t maximumStringLength     = 383;
    constexpr uint32_t maximumNameLength       = 95;
    constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
    constexpr uint64_t maximumRuntimeStateSize = 16 * 1024;
    Task* task                                 = task::getCurrent();
    Thread* current                            = Thread::getCurrent();
    os::ExecImageInfo info{};
    if (context == nullptr || task == nullptr || current == nullptr ||
        !copyFromUser(&info, reinterpret_cast<const void*>(context->rdi),
                      sizeof(info)) ||
        info.image == nullptr || info.imageSize == 0 ||
        info.imageSize > maximumImageSize || info.name == nullptr ||
        info.argumentCount > maximumArgumentCount ||
        info.environmentCount > maximumEnvironmentCount ||
        info.sharedImageCount > maximumSharedImageCount ||
        (info.argumentCount != 0 && info.arguments == nullptr) ||
        (info.environmentCount != 0 && info.environment == nullptr) ||
        (info.sharedImageCount != 0 && info.sharedImages == nullptr) ||
        info.runtimeStateSize > maximumRuntimeStateSize ||
        (info.runtimeStateSize != 0 && info.runtimeState == nullptr)) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }
    const uint64_t oldRoot = task->addressSpace().rootPhysicalAddress();
    if (!memory::validateUserMemory(oldRoot,
                                    reinterpret_cast<uint64_t>(info.image),
                                    info.imageSize, false) ||
        (info.runtimeStateSize != 0 &&
         !memory::validateUserMemory(
             oldRoot, reinterpret_cast<uint64_t>(info.runtimeState),
             info.runtimeStateSize, false))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }

    os::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
    ElfSharedImage sharedImages[maximumSharedImageCount]{};
    if (info.sharedImageCount != 0 &&
        !copyFromUser(userSharedImages, info.sharedImages,
                      info.sharedImageCount * sizeof(os::SharedImageInfo))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }
    for (uint32_t index = 0; index < info.sharedImageCount; ++index) {
        if (userSharedImages[index].image == nullptr ||
            userSharedImages[index].imageSize == 0 ||
            userSharedImages[index].imageSize > maximumImageSize ||
            !memory::validateUserMemory(
                oldRoot,
                reinterpret_cast<uint64_t>(userSharedImages[index].image),
                userSharedImages[index].imageSize, false)) {
            failureResult = error(SyscallError::InvalidArgument);
            return false;
        }
        sharedImages[index] = {
            reinterpret_cast<uint64_t>(userSharedImages[index].image),
            userSharedImages[index].imageSize, nullptr};
    }

    char name[maximumNameLength + 1]{};
    bool nameTerminated = false;
    for (uint32_t byte = 0; byte <= maximumNameLength; ++byte) {
        if (!copyFromUser(&name[byte], info.name + byte, 1)) {
            failureResult = error(SyscallError::InvalidArgument);
            return false;
        }
        if (name[byte] == '\0') {
            nameTerminated = true;
            break;
        }
    }
    if (!nameTerminated || name[0] == '\0') {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }

    uint64_t argumentPointers[maximumArgumentCount]{};
    uint64_t environmentPointers[maximumEnvironmentCount]{};
    char argumentStorage[maximumArgumentCount][maximumStringLength + 1]{};
    char environmentStorage[maximumEnvironmentCount][maximumStringLength + 1]{};
    const char* arguments[maximumArgumentCount]{};
    const char* environment[maximumEnvironmentCount]{};
    if ((info.argumentCount != 0 &&
         !copyFromUser(argumentPointers, info.arguments,
                       info.argumentCount * sizeof(uint64_t))) ||
        (info.environmentCount != 0 &&
         !copyFromUser(environmentPointers, info.environment,
                       info.environmentCount * sizeof(uint64_t)))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }
    for (uint32_t index = 0; index < info.argumentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumStringLength; ++byte) {
            if (!copyFromUser(&argumentStorage[index][byte],
                              reinterpret_cast<const void*>(
                                  argumentPointers[index] + byte),
                              1)) {
                failureResult = error(SyscallError::InvalidArgument);
                return false;
            }
            if (argumentStorage[index][byte] == '\0') {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            failureResult = error(SyscallError::InvalidArgument);
            return false;
        }
        arguments[index] = argumentStorage[index];
    }
    for (uint32_t index = 0; index < info.environmentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumStringLength; ++byte) {
            if (!copyFromUser(&environmentStorage[index][byte],
                              reinterpret_cast<const void*>(
                                  environmentPointers[index] + byte),
                              1)) {
                failureResult = error(SyscallError::InvalidArgument);
                return false;
            }
            if (environmentStorage[index][byte] == '\0') {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            failureResult = error(SyscallError::InvalidArgument);
            return false;
        }
        environment[index] = environmentStorage[index];
    }

    const uint64_t newRoot = memory::createAddressSpaceRoot();
    if (newRoot == 0) {
        failureResult = error(SyscallError::OutOfMemory);
        return false;
    }
    ElfLoadResult loadResult{};
    ElfLoadError loadError =
        info.sharedImageCount == 0
            ? ElfLoader::loadFromAddressSpace(
                  newRoot, oldRoot, reinterpret_cast<uint64_t>(info.image),
                  info.imageSize, loadResult)
            : ElfLoader::loadDynamicSetFromAddressSpace(
                  newRoot, oldRoot, reinterpret_cast<uint64_t>(info.image),
                  info.imageSize, sharedImages, info.sharedImageCount,
                  loadResult);
    uint64_t runtimeStateAddress = 0;
    if (loadError == ElfLoadError::None && info.runtimeStateSize != 0) {
        runtimeStateAddress = os::execRuntimeStateAddress;
        const uint64_t mappedSize =
            (info.runtimeStateSize + pageSize - 1) & ~(pageSize - 1);
        for (uint64_t offset = 0; offset < mappedSize; offset += pageSize) {
            if (memory::getPhysicalAddressInAddressSpace(
                    newRoot, runtimeStateAddress + offset) != 0) {
                loadError = ElfLoadError::MappingFailed;
                break;
            }
            const UserPageMapResult mapped =
                memory::allocateUserPageInAddressSpace(
                    newRoot, runtimeStateAddress + offset,
                    pageUser | pageWritable | pageNoExecute);
            if (mapped != UserPageMapResult::Success) {
                loadError = mapped == UserPageMapResult::OutOfMemory
                                ? ElfLoadError::OutOfMemory
                                : ElfLoadError::MappingFailed;
                break;
            }
        }
        uint8_t buffer[pageSize];
        uint64_t copied = 0;
        while (loadError == ElfLoadError::None &&
               copied < info.runtimeStateSize) {
            const uint64_t remaining = info.runtimeStateSize - copied;
            const uint64_t chunk = remaining < pageSize ? remaining : pageSize;
            if (!memory::copyFromAddressSpace(
                    buffer, oldRoot,
                    reinterpret_cast<uint64_t>(info.runtimeState) + copied,
                    chunk) ||
                !memory::copyToAddressSpace(
                    newRoot, runtimeStateAddress + copied, buffer, chunk)) {
                loadError = ElfLoadError::MappingFailed;
                break;
            }
            copied += chunk;
        }
        if (loadError == ElfLoadError::None) {
            for (uint64_t offset = 0; offset < mappedSize; offset += pageSize) {
                memory::protectPageInAddressSpace(newRoot,
                                                  runtimeStateAddress + offset,
                                                  pageUser | pageNoExecute);
            }
        }
    }
    uint64_t stackPointer = 0;
    if (loadError == ElfLoadError::None) {
        loadError = UserProgramLoader::buildStack(
            newRoot, name, info.argumentCount, arguments, info.environmentCount,
            environment, runtimeStateAddress, stackPointer);
    }
    if (loadError != ElfLoadError::None) {
        memory::destroyAddressSpaceRoot(newRoot);
        failureResult = elfLoadError(loadError);
        return false;
    }
    if (!task->commitExec(newRoot, name, current)) {
        memory::destroyAddressSpaceRoot(newRoot);
        failureResult = error(SyscallError::Busy);
        return false;
    }
    *context            = {};
    context->userRip    = loadResult.entryPoint;
    context->userRsp    = stackPointer;
    context->userRflags = 0x202;
    current->setUserStack(reinterpret_cast<void*>(stackPointer));
    const uint64_t interruptFlags = interrupt::disable();
    current->resetFloatingPointState();
    current->restoreFloatingPointState();
    interrupt::restore(interruptFlags);
    return true;
}

uint64_t handleWaitAddress(uint64_t userAddress, uint64_t expected, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = sync::waitOnUserAddress(
        reinterpret_cast<volatile uint32_t*>(userAddress),
        static_cast<uint32_t>(expected));
    if (result < 0) return error(SyscallError::InvalidArgument);
    if (result > 0) return error(SyscallError::WouldBlock);
    return 0;
}

uint64_t handleWakeAddress(uint64_t userAddress, uint64_t count, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    return sync::wakeUserAddress(
        reinterpret_cast<volatile uint32_t*>(userAddress),
        static_cast<uint32_t>(count));
}

uint64_t handleSignalReturn(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t) {
    return error(SyscallError::InvalidArgument);
}

uint64_t handleGetStandardHandle(uint64_t index, uint64_t, uint64_t, uint64_t,
                                 uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    return task == nullptr ? 0
                           : task->standardHandle(static_cast<uint32_t>(index));
}

constexpr SyscallHandler syscallHandlers[] = {
    handleSendMessage,
    handleReceiveMessage,
    handleCallService,
    handleReplyMessage,
    handleGetStandardHandle,
    handleGetBootstrapHandle,
    handleCloseHandle,
    handleMapMemory,
    handleUnmapMemory,
    handleProtectMemory,
    handleExitTask,
    handleCreateThread,
    handleExitThread,
    handleYield,
    handleSleep,
    handleGetThreadId,
    [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t { return error(SyscallError::NotSupported); },
    [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t { return error(SyscallError::NotSupported); },
    handleWaitAddress,
    handleWakeAddress,
    handleSignalReturn};

static_assert(sizeof(syscallHandlers) / sizeof(syscallHandlers[0]) ==
              static_cast<uint64_t>(SyscallNumber::Count) - OS_SYS_BASE);

}  // namespace


uint64_t dispatchSyscall(uint64_t syscallNumber, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6) {
    if (syscallNumber < OS_SYS_BASE ||
        syscallNumber >= static_cast<uint64_t>(SyscallNumber::Count)) {
        return error(SyscallError::NotSupported);
    }
    return syscallHandlers[syscallNumber - OS_SYS_BASE](arg1, arg2, arg3, arg4, arg5, arg6);
}

extern "C" void syscallDispatchEntry(SyscallContext* context) {
    Thread* thread = Thread::getCurrent();
    if (context == nullptr || thread == nullptr ||
        thread->getType() != ThreadType::User ||
        context->userRip >= maximumUserAddress ||
        context->userRsp >= maximumUserAddress) {
        Thread::terminateCurrentFromException(-1);
    }
    if (context->rax == static_cast<uint64_t>(SyscallNumber::SignalReturn)) {
        if (!signal::restoreSignalFrame(context, context->rdi)) {
            Thread::terminateCurrentFromException(-1);
        }
        return;
    }
    if (context->rax == static_cast<uint64_t>(SyscallNumber::ForkTask)) {
        context->rax = handleForkTask(context);
    } else if (context->rax ==
               static_cast<uint64_t>(SyscallNumber::ReplaceTaskImage)) {
        uint64_t failureResult = 0;
        if (handleReplaceTaskImage(context, failureResult)) return;
        context->rax = failureResult;
    } else {
        context->rax = dispatchSyscall(context->rax, context->rdi, context->rsi,
                                       context->rdx, context->r10, context->r8,
                                       context->r9);
    }
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    context->userRflags = (context->userRflags & allowedFlags) | 0x202;
    signal::deliverPendingSignal(context);
}

}  // namespace kernel::syscall

uint64_t kernel::Services::mapMemory(uint64_t address, uint64_t size, uint64_t protection) {
    return mapUserMemory(address, size, protection);
}

bool kernel::Services::unmapMemory(uint64_t address, uint64_t size) { return unmapUserMemory(address, size) == 0; }
bool kernel::Services::protectMemory(uint64_t address, uint64_t size, uint64_t protection) { return protectUserMemory(address, size, protection) == 0; }
