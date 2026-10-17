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

kernel::Endpoint* resolveEndpoint(uint64_t handle, uint32_t rights) {
    return static_cast<kernel::Endpoint*>(
        kernel::task::getCurrent()->handles().get(
            handle, kernel::HandleType::Endpoint, rights));
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
                           uint64_t, uint64_t, uint64_t, uint64_t) {
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightWrite);
    IpcMessage message{};
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (!copyFromUser(&message, reinterpret_cast<const void*>(userMessage),
                      sizeof(message))) {
        return error(SyscallError::InvalidArgument);
    }
    return endpoint->send(&message) == 0
               ? 0
               : error(SyscallError::PermissionDenied);
}

uint64_t handleReceiveMessage(uint64_t endpointHandle, uint64_t userMessage,
                              uint64_t, uint64_t, uint64_t, uint64_t) {
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightRead);
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
    if (endpoint->receive(&message) != 0) {
        return error(SyscallError::PermissionDenied);
    }
    return copyToUser(reinterpret_cast<void*>(userMessage), &message,
                      sizeof(message))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleCallService(uint64_t endpointHandle, uint64_t userRequest,
                           uint64_t userReply, uint64_t, uint64_t, uint64_t) {
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightWrite);
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
    if (endpoint->call(&request, &reply) != 0) {
        return error(SyscallError::PermissionDenied);
    }
    return copyToUser(reinterpret_cast<void*>(userReply), &reply, sizeof(reply))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleReplyMessage(uint64_t endpointHandle, uint64_t userReply,
                            uint64_t, uint64_t, uint64_t, uint64_t) {
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightRead);
    IpcMessage reply{};
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (!copyFromUser(&reply, reinterpret_cast<const void*>(userReply),
                      sizeof(reply))) {
        return error(SyscallError::InvalidArgument);
    }
    return endpoint->reply(&reply) == 0 ? 0
                                        : error(SyscallError::PermissionDenied);
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

uint64_t handleSpawnProgram(uint64_t program, uint64_t bootstrapHandle,
                            uint64_t argumentCount, uint64_t userArguments,
                            uint64_t, uint64_t) {
    constexpr uint32_t maximumArgumentCount  = 64;
    constexpr uint32_t maximumArgumentLength = 383;
    Task* parent                             = task::getCurrent();
    if (parent == nullptr || !parent->hasCapability(taskCapabilitySpawn)) {
        return error(SyscallError::PermissionDenied);
    }
    if (program >= static_cast<uint64_t>(os::ProgramId::Count)) {
        return error(SyscallError::InvalidArgument);
    }
    if (argumentCount > maximumArgumentCount ||
        (argumentCount != 0 && userArguments == 0)) {
        return error(SyscallError::InvalidArgument);
    }
    uint64_t userPointers[maximumArgumentCount]{};
    char argumentStorage[maximumArgumentCount][maximumArgumentLength + 1]{};
    const char* arguments[maximumArgumentCount]{};
    if (argumentCount != 0 &&
        !copyFromUser(userPointers,
                      reinterpret_cast<const void*>(userArguments),
                      argumentCount * sizeof(uint64_t))) {
        return error(SyscallError::InvalidArgument);
    }
    for (uint32_t index = 0; index < argumentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumArgumentLength; ++byte) {
            if (!copyFromUser(
                    &argumentStorage[index][byte],
                    reinterpret_cast<const void*>(userPointers[index] + byte),
                    1)) {
                return error(SyscallError::InvalidArgument);
            }
            if (argumentStorage[index][byte] == '\0') {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            return error(SyscallError::InvalidArgument);
        }
        arguments[index] = argumentStorage[index];
    }
    ElfLoadError loadError = ElfLoadError::None;
    Task* child            = UserProgramLoader::load(
        static_cast<os::ProgramId>(program), parent, bootstrapHandle,
        static_cast<uint32_t>(argumentCount), arguments, &loadError);
    if (child == nullptr) {
        serial::serialWrite("[ELF] dynamic image load failed: ");
        serial::serialWrite(ElfLoader::errorName(loadError));
        serial::serialWrite("\n");
        return elfLoadError(loadError);
    }
    const uint64_t handle = parent->handles().add(
        child, HandleType::Task, handleRightRead | handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleSpawnImage(uint64_t userInfo, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    constexpr uint32_t maximumArgumentCount    = 64;
    constexpr uint32_t maximumArgumentLength   = 383;
    constexpr uint32_t maximumNameLength       = 63;
    constexpr uint32_t maximumSharedImageCount = 4;
    constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
    Task* parent                               = task::getCurrent();
    os::SpawnImageInfo info{};
    if (parent == nullptr || !parent->hasCapability(taskCapabilitySpawn) ||
        !copyFromUser(&info, reinterpret_cast<const void*>(userInfo),
                      sizeof(info)) ||
        info.image == nullptr || info.imageSize == 0 ||
        info.imageSize > maximumImageSize || info.name == nullptr ||
        info.argumentCount > maximumArgumentCount ||
        info.sharedImageCount > maximumSharedImageCount ||
        (info.argumentCount != 0 && info.arguments == nullptr) ||
        (info.sharedImageCount != 0 && info.sharedImages == nullptr) ||
        (info.capabilities & ~(taskCapabilityDisplay | taskCapabilityUsers)) != 0 ||
        !memory::validateUserMemory(
            parent->addressSpace().rootPhysicalAddress(),
            reinterpret_cast<uint64_t>(info.image), info.imageSize, false)) {
        return error(SyscallError::InvalidArgument);
    }

    os::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
    ElfSharedImage sharedImages[maximumSharedImageCount]{};
    if (info.sharedImageCount != 0 &&
        !copyFromUser(userSharedImages, info.sharedImages,
                      info.sharedImageCount * sizeof(os::SharedImageInfo))) {
        return error(SyscallError::InvalidArgument);
    }
    for (uint32_t index = 0; index < info.sharedImageCount; ++index) {
        if (userSharedImages[index].image == nullptr ||
            userSharedImages[index].imageSize == 0 ||
            userSharedImages[index].imageSize > maximumImageSize ||
            !memory::validateUserMemory(
                parent->addressSpace().rootPhysicalAddress(),
                reinterpret_cast<uint64_t>(userSharedImages[index].image),
                userSharedImages[index].imageSize, false)) {
            return error(SyscallError::InvalidArgument);
        }
        sharedImages[index] = {
            reinterpret_cast<uint64_t>(userSharedImages[index].image),
            userSharedImages[index].imageSize, nullptr};
    }

    Task* logicalParent = parent;
    if (info.parentTaskId != 0) {
        logicalParent = task::find(info.parentTaskId);
        if (logicalParent == nullptr || logicalParent == task::getIdle() ||
            logicalParent->isTerminated()) {
            return error(SyscallError::InvalidArgument);
        }
    }

    char name[maximumNameLength + 1]{};
    bool nameTerminated = false;
    for (uint32_t byte = 0; byte <= maximumNameLength; ++byte) {
        if (!copyFromUser(&name[byte], info.name + byte, 1)) {
            return error(SyscallError::InvalidArgument);
        }
        if (name[byte] == '\0') {
            nameTerminated = true;
            break;
        }
    }
    if (!nameTerminated || name[0] == '\0') {
        return error(SyscallError::InvalidArgument);
    }

    uint64_t userPointers[maximumArgumentCount]{};
    char argumentStorage[maximumArgumentCount][maximumArgumentLength + 1]{};
    const char* arguments[maximumArgumentCount]{};
    if (info.argumentCount != 0 &&
        !copyFromUser(userPointers, info.arguments,
                      info.argumentCount * sizeof(uint64_t))) {
        return error(SyscallError::InvalidArgument);
    }
    for (uint32_t index = 0; index < info.argumentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumArgumentLength; ++byte) {
            if (!copyFromUser(
                    &argumentStorage[index][byte],
                    reinterpret_cast<const void*>(userPointers[index] + byte),
                    1)) {
                return error(SyscallError::InvalidArgument);
            }
            if (argumentStorage[index][byte] == '\0') {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            return error(SyscallError::InvalidArgument);
        }
        arguments[index] = argumentStorage[index];
    }

    ElfLoadError loadError = ElfLoadError::None;
    Task* child            = UserProgramLoader::loadImage(
        reinterpret_cast<uint64_t>(info.image), info.imageSize, name, parent,
        logicalParent, info.bootstrapHandle, info.capabilities,
        info.standardInputHandle, info.standardOutputHandle,
        info.standardErrorHandle, info.argumentCount, arguments, sharedImages,
        info.sharedImageCount, &loadError);
    if (child == nullptr) {
        return elfLoadError(loadError);
    }
    const uint64_t handle = parent->handles().add(
        child, HandleType::Task, handleRightRead | handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleGetBootstrapHandle(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    return task == nullptr ? 0 : task->bootstrapHandle();
}

uint64_t handleLog(uint64_t userMessage, uint64_t length, uint64_t, uint64_t,
                   uint64_t, uint64_t) {
    if (length == 0 || length > 240) {
        return error(SyscallError::InvalidArgument);
    }
    char message[241];
    if (!copyFromUser(message, reinterpret_cast<const void*>(userMessage),
                      length)) {
        return error(SyscallError::InvalidArgument);
    }
    message[length] = '\0';
    serial::serialWrite(message);
    return 0;
}

bool hasDeviceCapability() {
    Task* task = task::getCurrent();
    return task != nullptr && task->isSystemService() &&
           task->hasCapability(taskCapabilityDevice);
}

uint64_t handleCallPciService(uint64_t userRequest, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t) {
    Task* caller = task::getCurrent();
    if (caller == nullptr || !caller->isSystemService() ||
        !caller->hasCapability(taskCapabilityPci)) {
        return error(SyscallError::PermissionDenied);
    }
    os::PciRequest request{};
    if (!copyFromUser(&request, reinterpret_cast<const void*>(userRequest),
                      sizeof(request))) {
        return error(SyscallError::InvalidArgument);
    }
    switch (request.operation) {
    case os::PciOperation::Enumerate: {
        os::PciFunctionInfo functions[64]{};
        uint32_t count = 0;
        if (request.capacity == 0 || request.capacity > 64) {
            return error(SyscallError::InvalidArgument);
        }
        count = device::PciAccess::enumerate(functions, request.capacity);
        if (!copyToUser(request.functions, functions,
                        sizeof(os::PciFunctionInfo) * count)) {
            return error(SyscallError::InvalidArgument);
        }
        return count;
    }
    case os::PciOperation::ReadConfig:
        if (!device::PciAccess::read(request.function, request.offset,
                                      request.value) ||
            !copyToUser(reinterpret_cast<void*>(userRequest), &request,
                        sizeof(request))) {
            return error(SyscallError::InvalidArgument);
        }
        return 0;
    case os::PciOperation::WriteConfig:
        return device::PciAccess::write(request.function, request.offset,
                                          request.value)
                   ? 0 : error(SyscallError::InvalidArgument);
    default:
        return error(SyscallError::InvalidArgument);
    }
}

uint64_t handleMapDeviceMmio(uint64_t userFunction, uint64_t barIndex,
                             uint64_t userAddress, uint64_t size, uint64_t,
                             uint64_t) {
    Task* task = task::getCurrent();
    os::PciFunctionInfo function{};
    if (!hasDeviceCapability() ||
        !copyFromUser(&function, reinterpret_cast<const void*>(userFunction),
                      sizeof(function))) {
        return error(SyscallError::PermissionDenied);
    }
    device::MmioRegion* region = device::MmioRegion::map(
        *task, function, static_cast<uint8_t>(barIndex), userAddress, size);
    if (region == nullptr) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t handle = task->handles().add(
        region, HandleType::MmioRegion, handleRightRead | handleRightWrite);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleAllocateDma(uint64_t userAddress, uint64_t userInfo, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    if (!hasDeviceCapability()) {
        return error(SyscallError::PermissionDenied);
    }
    os::DmaBufferInfo info{};
    device::DmaBuffer* buffer =
        device::DmaBuffer::create(*task, userAddress, info);
    if (buffer == nullptr) {
        return error(SyscallError::OutOfMemory);
    }
    info.handle = task->handles().add(buffer, HandleType::DmaBuffer,
                                      handleRightRead | handleRightWrite);
    if (info.handle == 0 ||
        !copyToUser(reinterpret_cast<void*>(userInfo), &info, sizeof(info))) {
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleBindIrq(uint64_t irq, uint64_t endpointHandle, uint64_t levelTriggered,
                       uint64_t, uint64_t, uint64_t) {
    Task* task         = task::getCurrent();
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightRead);
    if (!hasDeviceCapability() || endpoint == nullptr || irq > 15 || levelTriggered > 1) {
        return error(SyscallError::PermissionDenied);
    }
    device::InterruptBinding* binding = device::InterruptBinding::create(
        *task, static_cast<uint8_t>(irq), endpoint, levelTriggered != 0);
    if (binding == nullptr) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t handle = task->handles().add(
        binding, HandleType::InterruptBinding, handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleAcknowledgeIrq(uint64_t handle, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    auto* binding = task == nullptr ? nullptr : static_cast<device::InterruptBinding*>(
        task->handles().get(handle, HandleType::InterruptBinding, handleRightManage));
    return binding != nullptr && binding->acknowledge(*task)
        ? 0 : error(SyscallError::InvalidHandle);
}

uint64_t handleGetTaskState(uint64_t taskHandle, uint64_t userExitCode,
                            uint64_t, uint64_t, uint64_t, uint64_t) {
    Task* task = static_cast<Task*>(task::getCurrent()->handles().get(
        taskHandle, HandleType::Task, handleRightRead));
    if (task == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    const int exitCode = task->exitCode();
    if (!copyToUser(reinterpret_cast<void*>(userExitCode), &exitCode,
                    sizeof(exitCode))) {
        return error(SyscallError::InvalidArgument);
    }
    const TaskState state = task->state();
    if (state == TaskState::Stopped) return 2;
    if (state != TaskState::Terminated) return 0;
    task->destroy();
    return 1;
}

uint64_t handleGetTaskInfo(uint64_t taskHandle, uint64_t userInfo, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    Task* task = static_cast<Task*>(task::getCurrent()->handles().get(
        taskHandle, HandleType::Task, handleRightRead));
    if (task == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    os::TaskInfo info{};
    info.id          = task->id();
    info.state       = static_cast<uint32_t>(task->state());
    info.exitCode    = task->exitCode();
    info.uid = task->uid(); info.gid = task->gid();
    info.effectiveUid = task->effectiveUid(); info.effectiveGid = task->effectiveGid();
    info.parentId = task->parent() ? task->parent()->id() : 0;
    info.trustedDeviceService = task->isSystemService() &&
                                task->hasCapability(taskCapabilityDevice);
    const char* name = task->name();
    uint32_t index   = 0;
    while (index + 1 < sizeof(info.name) && name[index] != '\0') {
        info.name[index] = name[index];
        ++index;
    }
    return copyToUser(reinterpret_cast<void*>(userInfo), &info, sizeof(info))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleTerminateTask(uint64_t taskHandle, uint64_t exitCode, uint64_t,
                             uint64_t, uint64_t, uint64_t) {
    Task* task = static_cast<Task*>(task::getCurrent()->handles().get(
        taskHandle, HandleType::Task, handleRightManage));
    if (task == nullptr || task == task::getCurrent() ||
        task == task::getIdle()) {
        return error(SyscallError::InvalidHandle);
    }
    task->terminateOtherThreads(static_cast<int>(exitCode));
    task->destroy();
    return 0;
}

uint64_t handleCloseHandle(uint64_t handle, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
    return task::getCurrent()->handles().close(handle)
               ? 0
               : error(SyscallError::InvalidHandle);
}

uint64_t handleGetTaskId(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                         uint64_t) {
    Task* task = task::getCurrent();
    return task == nullptr ? 0 : task->id();
}

uint64_t handleGetThreadId(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t) {
    Thread* thread = Thread::getCurrent();
    return thread == nullptr ? 0 : thread->getId();
}

uint64_t handleGetMonotonicTime(uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t) {
    return time::getMonotonicTime();
}

uint64_t handleGetWallClockTime(uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t) {
    return time::getWallClockTime();
}

uint64_t handleMapFramebuffer(uint64_t userAddress, uint64_t userInfo, uint64_t,
                              uint64_t, uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    if (task == nullptr ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    userInfo, sizeof(os::DisplayInfo), true)) {
        return error(SyscallError::InvalidArgument);
    }
    os::DisplayInfo info{};
    if (!device::DisplayAccess::mapFramebuffer(*task, userAddress, info)) {
        return error(SyscallError::PermissionDenied);
    }
    return copyToUser(reinterpret_cast<void*>(userInfo), &info, sizeof(info))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleCreatePipe(uint64_t userHandles, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    os::PipeHandles handles{};
    if (task == nullptr ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    userHandles, sizeof(handles), true) ||
        !Pipe::createHandles(*task, handles.readHandle, handles.writeHandle)) {
        return error(SyscallError::OutOfMemory);
    }
    if (!copyToUser(reinterpret_cast<void*>(userHandles), &handles,
                    sizeof(handles))) {
        task->handles().close(handles.readHandle);
        task->handles().close(handles.writeHandle);
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleReadPipe(uint64_t handle, uint64_t userBuffer, uint64_t size,
                        uint64_t, uint64_t, uint64_t) {
    constexpr size_t maximumTransfer = 512;
    Task* task                       = task::getCurrent();
    if (task == nullptr || size == 0 || size > maximumTransfer ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    userBuffer, size, true)) {
        return error(SyscallError::InvalidArgument);
    }
    uint8_t buffer[maximumTransfer];
    void* object    = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!task->handles().inspect(handle, &object, &type, &rights)) {
        return error(SyscallError::InvalidHandle);
    }
    const int64_t result =
        Pipe::read(*task, handle, buffer, static_cast<size_t>(size));
    if (result <= 0) {
        return static_cast<uint64_t>(result);
    }
    return copyToUser(reinterpret_cast<void*>(userBuffer), buffer,
                      static_cast<size_t>(result))
               ? static_cast<uint64_t>(result)
               : error(SyscallError::InvalidArgument);
}

uint64_t handleWritePipe(uint64_t handle, uint64_t userBuffer, uint64_t size,
                         uint64_t, uint64_t, uint64_t) {
    constexpr size_t maximumTransfer = 512;
    Task* task                       = task::getCurrent();
    uint8_t buffer[maximumTransfer];
    if (task == nullptr || size == 0 || size > maximumTransfer ||
        !copyFromUser(buffer, reinterpret_cast<const void*>(userBuffer),
                      static_cast<size_t>(size))) {
        return error(SyscallError::InvalidArgument);
    }
    void* object    = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!task->handles().inspect(handle, &object, &type, &rights)) {
        return error(SyscallError::InvalidHandle);
    }
    return static_cast<uint64_t>(
        Pipe::write(*task, handle, buffer, static_cast<size_t>(size)));
}

uint64_t handleCreateChannelPair(uint64_t userHandles, uint64_t, uint64_t,
                                 uint64_t, uint64_t, uint64_t) {
    Task* current = task::getCurrent();
    os::ChannelHandles handles{};
    if (!current || !Pipe::createChannelPair(*current, handles.firstHandle,
                                              handles.secondHandle))
        return error(SyscallError::OutOfMemory);
    if (!copyToUser(reinterpret_cast<void*>(userHandles), &handles,
                    sizeof(handles))) {
        current->handles().close(handles.firstHandle);
        current->handles().close(handles.secondHandle);
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleGetChannelIdentity(uint64_t handle, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t) {
    Task* current = task::getCurrent();
    const uint64_t identity = current ? Pipe::channelIdentity(*current, handle) : 0;
    return identity ? identity : error(SyscallError::InvalidHandle);
}

uint64_t handleSetChannelNonblocking(uint64_t handle, uint64_t enabled,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    Task* current = task::getCurrent();
    return current && Pipe::setChannelNonblocking(*current, handle,
                                                   enabled != 0)
               ? 0 : error(SyscallError::InvalidHandle);
}

uint64_t handleSignalChannelEof(uint64_t handle, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    Task* current = task::getCurrent();
    return current && Pipe::signalChannelEof(*current, handle)
               ? 0 : error(SyscallError::InvalidHandle);
}
uint64_t handleSendSignal(uint64_t taskId, uint64_t signal, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    const int64_t target = static_cast<int64_t>(taskId);
    Task* current        = task::getCurrent();
    const bool delivered =
        target > 0 ? signal::sendSignalToTask(static_cast<uint64_t>(target),
                                              static_cast<uint32_t>(signal))
        : target == 0 && current != nullptr
            ? signal::sendSignalToTaskGroup(current->taskGroupId(),
                                            static_cast<uint32_t>(signal))
        : target < -1
            ? signal::sendSignalToTaskGroup(static_cast<uint64_t>(-target),
                                            static_cast<uint32_t>(signal))
            : false;
    return delivered ? 0 : error(SyscallError::NotFound);
}

uint64_t handleSetTaskGroup(uint64_t taskGroupId, uint64_t taskId, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
    Task* current = task::getCurrent();
    Task* target = taskId == 0 ? current : task::find(taskId);
    if (target == nullptr || current == nullptr ||
        (target != current && target->parent() != current))
        return error(SyscallError::NotFound);
    return target->setTaskGroup(taskGroupId)
               ? 0
               : error(SyscallError::PermissionDenied);
}

uint64_t handleCreateSession(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t) {
    Task* task = task::getCurrent();
    return task != nullptr && task->createSession()
               ? task->sessionId()
               : error(SyscallError::PermissionDenied);
}

struct TaskIdentifiers {
    uint64_t parentId;
    uint64_t taskGroupId;
    uint64_t sessionId;
};

uint64_t handleGetTaskIdentifiers(uint64_t userIdentifiers, uint64_t taskId, uint64_t,
                                  uint64_t, uint64_t, uint64_t) {
    Task* task = taskId == 0 ? task::getCurrent() : task::find(taskId);
    if (task == nullptr) {
        return error(SyscallError::NotFound);
    }
    TaskIdentifiers identifiers{
        task->parent() == nullptr ? 0 : task->parent()->id(),
        task->taskGroupId(), task->sessionId()};
    return copyToUser(reinterpret_cast<void*>(userIdentifiers), &identifiers,
                      sizeof(identifiers))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleGetClockResolution(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t) {
    return time::getClockResolution();
}

uint64_t handleWaitThread(uint64_t threadId, uint64_t userExitCode,
                          uint64_t noHang, uint64_t, uint64_t, uint64_t) {
    Task* task      = task::getCurrent();
    Thread* current = Thread::getCurrent();
    if (task == nullptr || current == nullptr || userExitCode == 0) {
        return error(SyscallError::InvalidArgument);
    }
    while (true) {
        int exitCode = 0;
        const int result =
            task->waitForThread(threadId, current, exitCode, noHang != 0);
        if (result < 0) {
            return error(SyscallError::NotFound);
        }
        if (result == 2) {
            return error(SyscallError::WouldBlock);
        }
        if (result == 0) {
            return copyToUser(reinterpret_cast<void*>(userExitCode), &exitCode,
                              sizeof(exitCode))
                       ? 0
                       : error(SyscallError::InvalidArgument);
        }
        Thread::blockCurrent();
    }
}

uint64_t handleWaitChild(uint64_t taskId, uint64_t userExitCode,
                         uint64_t options, uint64_t, uint64_t, uint64_t) {
    Task* task      = task::getCurrent();
    Thread* current = Thread::getCurrent();
    if (task == nullptr || current == nullptr || userExitCode == 0 ||
        (options & ~11ULL) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    while (true) {
        int status       = 0;
        uint64_t childId = 0;
        const int result =
            task->waitForChild(static_cast<int64_t>(taskId), current, status,
                               static_cast<uint32_t>(options), childId);
        if (result < 0) return error(SyscallError::NotFound);
        if (result == 2) return 0;
        if (result == 0) {
            return copyToUser(reinterpret_cast<void*>(userExitCode), &status,
                              sizeof(status))
                       ? childId
                       : error(SyscallError::InvalidArgument);
        }
        Thread::blockCurrent();
    }
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

uint64_t handleSetSignalAction(uint64_t signal, uint64_t userAction,
                               uint64_t userOldAction, uint64_t, uint64_t,
                               uint64_t) {
    Task* task = task::getCurrent();
    if (task == nullptr ||
        !signal::isValidSignal(static_cast<uint32_t>(signal))) {
        return error(SyscallError::InvalidArgument);
    }
    SignalAction action{};
    SignalAction oldAction{};
    if (userAction != 0) {
        if (!copyFromUser(&action, reinterpret_cast<const void*>(userAction),
                          sizeof(action)) ||
            (action.getFlags() & ~(signalFlagNoDefer | signalFlagResetHand |
                                   0x00000004ULL | 0x10000000ULL)) != 0) {
            return error(SyscallError::InvalidArgument);
        }
        if (action.getHandler() > signalIgnore &&
            (action.getRestorer() == 0 ||
             !memory::validateUserMemory(
                 task->addressSpace().rootPhysicalAddress(),
                 action.getHandler(), 1, false) ||
             !memory::validateUserMemory(
                 task->addressSpace().rootPhysicalAddress(),
                 action.getRestorer(), 1, false))) {
            return error(SyscallError::InvalidArgument);
        }
        if (!task->setSignalAction(static_cast<uint32_t>(signal), action,
                                   &oldAction)) {
            return error(SyscallError::InvalidArgument);
        }
    } else if (!task->getSignalAction(static_cast<uint32_t>(signal),
                                      oldAction)) {
        return error(SyscallError::InvalidArgument);
    }
    if (userOldAction != 0 &&
        !copyToUser(reinterpret_cast<void*>(userOldAction), &oldAction,
                    sizeof(oldAction))) {
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleSetSignalMask(uint64_t how, uint64_t mask, uint64_t userOldMask,
                             uint64_t, uint64_t, uint64_t) {
    Thread* thread = Thread::getCurrent();
    if (thread == nullptr || how > 2 || userOldMask == 0) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t oldMask = thread->getSignalMask();
    mask &= ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
              (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
    if (how == 0)
        thread->setSignalMask(thread->getSignalMask() | (mask));
    else if (how == 1)
        thread->setSignalMask(thread->getSignalMask() & (~mask));
    else
        thread->setSignalMask(mask);
    return copyToUser(reinterpret_cast<void*>(userOldMask), &oldMask,
                      sizeof(oldMask))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleGetPendingSignals(uint64_t userMask, uint64_t, uint64_t,
                                 uint64_t, uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    if (task == nullptr) return error(SyscallError::InvalidArgument);
    const uint64_t mask = task->pendingSignals(Thread::getCurrent());
    return copyToUser(reinterpret_cast<void*>(userMask), &mask, sizeof(mask))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleWaitSignal(uint64_t mask, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t) {
    Task* task     = task::getCurrent();
    Thread* thread = Thread::getCurrent();
    SignalAction action{};
    if (task == nullptr || thread == nullptr || mask == 0) {
        return error(SyscallError::InvalidArgument);
    }
    const uint32_t signal =
        task->takePendingSignal(thread, mask, action, false);
    return signal == 0 ? error(SyscallError::WouldBlock) : signal;
}

uint64_t handleSignalReturn(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t) {
    return error(SyscallError::InvalidArgument);
}

uint64_t handleOpenMessageQueue(uint64_t userName, uint64_t flags,
                                uint64_t maximumMessages, uint64_t messageSize,
                                uint64_t, uint64_t) {
    char name[64]{};
    bool terminated = false;
    for (uint32_t index = 0; index < sizeof(name); ++index) {
        if (!copyFromUser(&name[index],
                          reinterpret_cast<const void*>(userName + index), 1)) {
            return error(SyscallError::InvalidArgument);
        }
        if (name[index] == '\0') {
            terminated = true;
            break;
        }
    }
    if (!terminated || maximumMessages > UINT32_MAX ||
        messageSize > UINT32_MAX) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t handle =
        MessageQueue::open(*task::getCurrent(), name, static_cast<int>(flags),
                           static_cast<uint32_t>(maximumMessages),
                           static_cast<uint32_t>(messageSize));
    return handle == 0 ? error(SyscallError::InvalidArgument) : handle;
}

uint64_t handleUnlinkMessageQueue(uint64_t userName, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t) {
    char name[64]{};
    bool terminated = false;
    for (uint32_t index = 0; index < sizeof(name); ++index) {
        if (!copyFromUser(&name[index],
                          reinterpret_cast<const void*>(userName + index), 1)) {
            return error(SyscallError::InvalidArgument);
        }
        if (name[index] == '\0') {
            terminated = true;
            break;
        }
    }
    return terminated && MessageQueue::unlink(name)
               ? 0
               : error(SyscallError::NotFound);
}

uint64_t handleSendMessageQueue(uint64_t handle, uint64_t userData,
                                uint64_t size, uint64_t priority, uint64_t,
                                uint64_t) {
    uint8_t data[256];
    if (size > sizeof(data) || priority > UINT32_MAX ||
        (size != 0 &&
         !copyFromUser(data, reinterpret_cast<const void*>(userData), size))) {
        return error(SyscallError::InvalidArgument);
    }
    const int result =
        MessageQueue::send(*task::getCurrent(), handle, data, size,
                           static_cast<uint32_t>(priority));
    return result == 0 ? 0
                       : error(result == 1 ? SyscallError::WouldBlock
                                           : SyscallError::InvalidArgument);
}

uint64_t handleReceiveMessageQueue(uint64_t handle, uint64_t userData,
                                   uint64_t capacity, uint64_t userPriority,
                                   uint64_t, uint64_t) {
    uint8_t data[256];
    uint32_t priority = 0;
    if (capacity > sizeof(data)) capacity = sizeof(data);
    const int64_t result = MessageQueue::receive(*task::getCurrent(), handle,
                                                 data, capacity, priority);
    if (result == -2) return error(SyscallError::WouldBlock);
    if (result < 0 ||
        (result != 0 && !copyToUser(reinterpret_cast<void*>(userData), data,
                                    static_cast<size_t>(result))) ||
        (userPriority != 0 && !copyToUser(reinterpret_cast<void*>(userPriority),
                                          &priority, sizeof(priority)))) {
        return error(SyscallError::InvalidArgument);
    }
    return static_cast<uint64_t>(result);
}

uint64_t handleGetMessageQueueAttributes(uint64_t handle,
                                         uint64_t userAttributes, uint64_t,
                                         uint64_t, uint64_t, uint64_t) {
    MessageQueueAttributes attributes{};
    return MessageQueue::getAttributes(*task::getCurrent(), handle,
                                       attributes) &&
                   copyToUser(reinterpret_cast<void*>(userAttributes),
                              &attributes, sizeof(attributes))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleSetMessageQueueFlags(uint64_t handle, uint64_t flags,
                                    uint64_t userOldFlags, uint64_t, uint64_t,
                                    uint64_t) {
    uint64_t oldFlags = 0;
    return MessageQueue::setFlags(*task::getCurrent(), handle, flags,
                                  oldFlags) &&
                   copyToUser(reinterpret_cast<void*>(userOldFlags), &oldFlags,
                              sizeof(oldFlags))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleOpenSharedMemory(uint64_t userName, uint64_t flags, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    char name[64]{};
    bool terminated = false;
    for (uint32_t index = 0; index < sizeof(name); ++index) {
        if (!copyFromUser(&name[index],
                          reinterpret_cast<const void*>(userName + index), 1)) {
            return error(SyscallError::InvalidArgument);
        }
        if (name[index] == '\0') {
            terminated = true;
            break;
        }
    }
    if (!terminated || flags > UINT32_MAX) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t handle =
        SharedMemory::open(*task::getCurrent(), name, static_cast<int>(flags));
    return handle == 0 ? error(SyscallError::InvalidArgument) : handle;
}

uint64_t handleUnlinkSharedMemory(uint64_t userName, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t) {
    char name[64]{};
    bool terminated = false;
    for (uint32_t index = 0; index < sizeof(name); ++index) {
        if (!copyFromUser(&name[index],
                          reinterpret_cast<const void*>(userName + index), 1)) {
            return error(SyscallError::InvalidArgument);
        }
        if (name[index] == '\0') {
            terminated = true;
            break;
        }
    }
    return terminated && SharedMemory::unlink(name)
               ? 0
               : error(SyscallError::NotFound);
}

uint64_t handleResizeSharedMemory(uint64_t handle, uint64_t size, uint64_t,
                                  uint64_t, uint64_t, uint64_t) {
    return SharedMemory::resize(*task::getCurrent(), handle, size)
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleMapSharedMemory(uint64_t handle, uint64_t address, uint64_t size,
                               uint64_t offset, uint64_t protection, uint64_t) {
    const uint64_t result = SharedMemory::map(
        *task::getCurrent(), handle, address, size, offset, protection);
    return result == 0 ? error(SyscallError::InvalidArgument) : result;
}

uint64_t handleSendThreadSignal(uint64_t threadId, uint64_t signal, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    if (task == nullptr || signal > maximumSignal) {
        return error(SyscallError::InvalidArgument);
    }
    return task->deliverThreadSignal(threadId, static_cast<uint32_t>(signal))
               ? 0
               : error(SyscallError::NotFound);
}

uint64_t handleRegisterMessageQueueNotification(uint64_t handle, uint64_t type,
                                                uint64_t signal, uint64_t,
                                                uint64_t, uint64_t) {
    if (type > 2 || signal > UINT32_MAX) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = MessageQueue::registerNotification(
        *task::getCurrent(), handle, static_cast<int>(type),
        static_cast<uint32_t>(signal));
    return result == 0 ? 0
                       : error(result == 1 ? SyscallError::Busy
                                           : SyscallError::InvalidArgument);
}

uint64_t handleGetStandardHandle(uint64_t index, uint64_t, uint64_t, uint64_t,
                                 uint64_t, uint64_t) {
    Task* task = task::getCurrent();
    return task == nullptr ? 0
                           : task->standardHandle(static_cast<uint32_t>(index));
}

constexpr SyscallHandler syscallHandlers[] = {
    // Message queue
    handleOpenMessageQueue,
    handleUnlinkMessageQueue,
    handleSendMessageQueue,
    handleReceiveMessageQueue,
    handleGetMessageQueueAttributes,
    handleSetMessageQueueFlags,
    handleRegisterMessageQueueNotification,
    // Process
    handleGetStandardHandle,
    handleExitTask,
    handleCreateThread,
    handleExitThread,
    handleYield,
    handleSleep,
    handleSpawnProgram,
    handleSpawnImage,
    handleGetBootstrapHandle,
    handleGetTaskState,
    handleCloseHandle,
    handleGetTaskId,
    handleGetThreadId,
    handleGetTaskInfo,
    handleTerminateTask,
    handleSendSignal,
    handleSendThreadSignal,
    handleCreateSession,
    handleGetTaskIdentifiers,
    handleSetTaskGroup,
    [](uint64_t id, uint64_t uid, uint64_t gid, uint64_t effectiveOnly, uint64_t, uint64_t) -> uint64_t {
        Task* caller = task::getCurrent();
        Task* target = !id ? caller : task::find(id);
        if (!caller || !target || uid >= 65535 || gid >= 65535 || effectiveOnly > 1)
            return error(SyscallError::InvalidArgument);
        if (!caller->hasCapability(taskCapabilityUsers) &&
            (caller != target || (caller->effectiveUid() != 0 &&
             (uid != caller->uid() || gid != caller->gid()))))
            return error(SyscallError::PermissionDenied);
        target->setCredentials(uid, gid, effectiveOnly);
        return 0;
    },
    [](uint64_t id, uint64_t address, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t {
        Task* target = task::find(id);
        if (!target) return error(SyscallError::NotFound);
        os::TaskInfo info{};
        info.id = target->id(); info.state = static_cast<uint32_t>(target->state()); info.exitCode = target->exitCode();
        info.uid = target->uid(); info.gid = target->gid();
        info.effectiveUid = target->effectiveUid(); info.effectiveGid = target->effectiveGid();
        info.parentId = target->parent() ? target->parent()->id() : 0;
        info.trustedDeviceService = target->isSystemService() &&
                                    target->hasCapability(taskCapabilityDevice);
        const char* name = target->name();
        for (size_t i = 0; i + 1 < sizeof(info.name) && name[i]; ++i) info.name[i] = name[i];
        return copyToUser(reinterpret_cast<void*>(address), &info, sizeof(info)) ? 0 : error(SyscallError::InvalidArgument);
    },
    [](uint64_t index, uint64_t address, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t {
        Task* target = index <= UINT32_MAX ? task::at(static_cast<uint32_t>(index)) : nullptr;
        if (!target) return error(SyscallError::NotFound);
        os::TaskInfo info{};
        info.id = target->id(); info.state = static_cast<uint32_t>(target->state()); info.exitCode = target->exitCode();
        info.uid = target->uid(); info.gid = target->gid();
        info.effectiveUid = target->effectiveUid(); info.effectiveGid = target->effectiveGid();
        info.parentId = target->parent() ? target->parent()->id() : 0;
        info.trustedDeviceService = target->isSystemService() &&
                                    target->hasCapability(taskCapabilityDevice);
        const char* name = target->name();
        for (size_t i = 0; i + 1 < sizeof(info.name) && name[i]; ++i) info.name[i] = name[i];
        return copyToUser(reinterpret_cast<void*>(address), &info, sizeof(info)) ? 0 : error(SyscallError::InvalidArgument);
    },
    [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t {
        return error(SyscallError::NotSupported);
    },
    handleWaitChild,
    [](uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t {
        return error(SyscallError::NotSupported);
    },
    handleWaitThread,
    handleWaitAddress,
    handleWakeAddress,
    handleSetSignalAction,
    handleSetSignalMask,
    handleGetPendingSignals,
    handleWaitSignal,
    handleSignalReturn,
    [](uint64_t value, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t {
        Task* current = task::getCurrent();
        if (!current) return error(SyscallError::NotFound);
        if (value == UINT64_MAX) return current->terminalId();
        if (value >= 7) return error(SyscallError::InvalidArgument);
        current->setTerminalId(value); return 0;
    },
    handleCreateEndpoint,
    handleSendMessage,
    handleReceiveMessage,
    handleCallService,
    handleReplyMessage,
    // Pipe and channel
    handleCreatePipe,
    handleReadPipe,
    handleWritePipe,
    handleCreateChannelPair,
    handleGetChannelIdentity,
    handleSetChannelNonblocking,
    handleSignalChannelEof,
    // Device
    handleCallPciService,
    handleMapDeviceMmio,
    handleAllocateDma,
    handleBindIrq,
    handleAcknowledgeIrq,
    handleMapFramebuffer,
    handleGetMonotonicTime,
    handleGetWallClockTime,
    handleGetClockResolution,
    [](uint64_t address, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) -> uint64_t {
        const os::SystemInfo info{time::getMonotonicTime(), memory::getTotalMemoryBytes(),
                                  memory::getFreeMemoryBytes(), smp::getOnlineCpuCount(), 0};
        return copyToUser(reinterpret_cast<void*>(address), &info, sizeof(info)) ? 0 :
               error(SyscallError::InvalidArgument);
    },
    handleLog,
    // Memory
    handleMapMemory,
    handleUnmapMemory,
    handleProtectMemory,
    handleOpenSharedMemory,
    handleUnlinkSharedMemory,
    handleResizeSharedMemory,
    handleMapSharedMemory};

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
