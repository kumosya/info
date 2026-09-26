#include "kernel/syscall.h"

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/mqueue.h"
#include "kernel/page.h"
#include "kernel/pipe.h"
#include "kernel/posix.h"
#include "kernel/process.h"
#include "kernel/pty.h"
#include "kernel/scheduler.h"
#include "kernel/serial.h"
#include "kernel/shared_memory.h"
#include "kernel/signal.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/user_wait.h"
#include "kernel/user.h"

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
    kernel::Process* process = kernel::getCurrentProcess();
    if (process == nullptr || process == kernel::getKernelProcess() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    if ((protection & 3) == 3) {
        return error(kernel::SyscallError::PermissionDenied);
    }

    kernel::PreemptionSpinLockGuard addressGuard(
        process->addressSpace().lock());
    uint64_t flags = kernel::pageUser;
    if ((protection & 1) != 0) {
        flags |= kernel::pageWritable;
    }
    if ((protection & 2) == 0) {
        flags |= kernel::pageNoExecute;
    }

    uint64_t mappedSize = 0;
    while (mappedSize < size) {
        const uint64_t physicalAddress = kernel::allocatePhysicalPage();
        if (physicalAddress == 0 ||
            !kernel::mapPageInAddressSpace(
                process->addressSpace().rootPhysicalAddress(),
                address + mappedSize, physicalAddress, flags)) {
            if (physicalAddress != 0) {
                kernel::freePhysicalPage(physicalAddress);
            }
            while (mappedSize != 0) {
                mappedSize -= pageSize;
                const uint64_t physical =
                    kernel::getPhysicalAddressInAddressSpace(
                        process->addressSpace().rootPhysicalAddress(),
                        address + mappedSize);
                kernel::unmapPageInAddressSpace(
                    process->addressSpace().rootPhysicalAddress(),
                    address + mappedSize);
                kernel::freePhysicalPage(physical & ~(pageSize - 1));
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
    kernel::Process* process = kernel::getCurrentProcess();
    if (process == nullptr || process == kernel::getKernelProcess() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size)) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    kernel::PreemptionSpinLockGuard addressGuard(
        process->addressSpace().lock());
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags          = 0;
        const uint64_t physical = kernel::getPhysicalAddressInAddressSpace(
            process->addressSpace().rootPhysicalAddress(), address + offset,
            &flags);
        if (physical == 0 || (flags & kernel::pageUser) == 0) {
            return error(kernel::SyscallError::NotFound);
        }
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags = 0;
        const uint64_t physical = kernel::getPhysicalAddressInAddressSpace(
            process->addressSpace().rootPhysicalAddress(), address + offset,
            &flags);
        kernel::unmapPageInAddressSpace(
            process->addressSpace().rootPhysicalAddress(), address + offset);
        if ((flags & kernel::pageExternal) == 0) {
            kernel::freePhysicalPage(physical & ~(pageSize - 1));
        }
    }
    kernel::releaseSharedMemoryMapping(*process, address, size);
    return 0;
}

uint64_t protectUserMemory(uint64_t address, uint64_t size,
                           uint64_t protection) {
    kernel::Process* process = kernel::getCurrentProcess();
    if (process == nullptr || process == kernel::getKernelProcess() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0 || (protection & 3) == 3) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    kernel::PreemptionSpinLockGuard addressGuard(
        process->addressSpace().lock());
    if (!kernel::sharedMemoryProtectionAllowed(*process, address, size,
                                               protection)) {
        return error(kernel::SyscallError::PermissionDenied);
    }
    uint64_t flags = kernel::pageUser;
    if ((protection & 1) != 0) flags |= kernel::pageWritable;
    if ((protection & 2) == 0) flags |= kernel::pageNoExecute;
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        if (!kernel::protectPageInAddressSpace(
                process->addressSpace().rootPhysicalAddress(),
                address + offset, flags)) {
            return error(kernel::SyscallError::InvalidArgument);
        }
    }
    return 0;
}

kernel::Endpoint* resolveEndpoint(uint64_t handle, uint32_t rights) {
    return static_cast<kernel::Endpoint*>(
        kernel::getCurrentProcess()->handles().get(
            handle, kernel::HandleType::Endpoint, rights));
}

}  // namespace

namespace kernel {

void initSyscall() {
    constexpr uint64_t kernelCodeSelector = 0x08;
    constexpr uint64_t userSelectorBase   = 0x10;
    writeMsr(ia32Efer, readMsr(ia32Efer) | 1);
    writeMsr(ia32Star, (userSelectorBase << 48) | (kernelCodeSelector << 32));
    writeMsr(ia32Lstar, reinterpret_cast<uint64_t>(syscallEntry));
    writeMsr(ia32Fmask,
             (1ULL << 8) | (1ULL << 9) | (1ULL << 10) | (1ULL << 18));
    serialWrite("[SYSCALL] SYSCALL/SYSRET initialized\n");
}

bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size) {
    Process* process       = getCurrentProcess();
    const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);
    if (kernelBuffer == nullptr || process == nullptr ||
        !validUserRange(address, size)) {
        return false;
    }
    PreemptionSpinLockGuard guard(process->addressSpace().lock());
    if (!validateUserMemory(process->addressSpace().rootPhysicalAddress(),
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
    Process* process       = getCurrentProcess();
    const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);
    if (kernelBuffer == nullptr || process == nullptr ||
        !validUserRange(address, size)) {
        return false;
    }
    PreemptionSpinLockGuard guard(process->addressSpace().lock());
    if (!validateUserMemory(process->addressSpace().rootPhysicalAddress(),
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

uint64_t handleExitProcess(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
    //serialWrite("[SYSCALL] user process requested exit\n");
    exitCurrentProcess(static_cast<int>(exitCode));
}

uint64_t handleCreateThread(uint64_t entryPoint, uint64_t stackPointer,
                            uint64_t argument, uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    if (process == nullptr ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            entryPoint, 1, false) ||
        stackPointer < 8 ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            stackPointer - 8, 8, true)) {
        return error(SyscallError::InvalidArgument);
    }
    Thread* thread =
        createUserThread(process, entryPoint, stackPointer, argument);
    return thread == nullptr ? error(SyscallError::OutOfMemory) : thread->id;
}

uint64_t handleExitThread(uint64_t exitCode, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    exitThread(static_cast<int>(exitCode));
}

uint64_t handleYield(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                     uint64_t) {
    yieldThread();
    return 0;
}

uint64_t handleSleep(uint64_t milliseconds, uint64_t, uint64_t, uint64_t,
                     uint64_t, uint64_t) {
    sleepThread(milliseconds);
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
    Endpoint* endpoint = createEndpoint();
    if (endpoint == nullptr) {
        return error(SyscallError::OutOfMemory);
    }
    const uint64_t handle = getCurrentProcess()->handles().add(
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
    return sendMessage(endpoint, &message) == 0
               ? 0
               : error(SyscallError::PermissionDenied);
}

uint64_t handleReceiveMessage(uint64_t endpointHandle, uint64_t userMessage,
                              uint64_t, uint64_t, uint64_t, uint64_t) {
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightRead);
    IpcMessage message{};
    Process* process = getCurrentProcess();
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (process == nullptr ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            userMessage, sizeof(message), true)) {
        return error(SyscallError::InvalidArgument);
    }
    if (receiveMessage(endpoint, &message) != 0) {
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
    Process* process = getCurrentProcess();
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (process == nullptr ||
        !copyFromUser(&request, reinterpret_cast<const void*>(userRequest),
                      sizeof(request)) ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            userReply, sizeof(reply), true)) {
        return error(SyscallError::InvalidArgument);
    }
    if (callService(endpoint, &request, &reply) != 0) {
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
    return replyMessage(endpoint, &reply) == 0
               ? 0
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
    constexpr uint32_t maximumArgumentCount = 8;
    constexpr uint32_t maximumArgumentLength = 63;
    Process* parent = getCurrentProcess();
    if (parent == nullptr || !parent->hasCapability(processCapabilitySpawn)) {
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
        !copyFromUser(userPointers, reinterpret_cast<const void*>(userArguments),
                      argumentCount * sizeof(uint64_t))) {
        return error(SyscallError::InvalidArgument);
    }
    for (uint32_t index = 0; index < argumentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumArgumentLength; ++byte) {
            if (!copyFromUser(&argumentStorage[index][byte],
                              reinterpret_cast<const void*>(
                                  userPointers[index] + byte),
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
    Process* child =
        UserProgramLoader::load(static_cast<os::ProgramId>(program), parent,
                                bootstrapHandle,
                                static_cast<uint32_t>(argumentCount),
                                arguments, &loadError);
    if (child == nullptr) {
        serialWrite("[ELF] dynamic image load failed: ");
        serialWrite(getElfLoadErrorName(loadError));
        serialWrite("\n");
        return elfLoadError(loadError);
    }
    const uint64_t handle = parent->handles().add(
        child, HandleType::Process, handleRightRead | handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleSpawnImage(uint64_t userInfo, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    constexpr uint32_t maximumArgumentCount = 8;
    constexpr uint32_t maximumArgumentLength = 63;
    constexpr uint32_t maximumNameLength = 63;
    constexpr uint32_t maximumSharedImageCount = 3;
    constexpr uint64_t maximumImageSize = 8ULL * 1024 * 1024;
    Process* parent = getCurrentProcess();
    os::SpawnImageInfo info{};
    if (parent == nullptr ||
        !parent->hasCapability(processCapabilitySpawn) ||
        !copyFromUser(&info, reinterpret_cast<const void*>(userInfo),
                      sizeof(info)) ||
        info.image == nullptr || info.imageSize == 0 ||
        info.imageSize > maximumImageSize || info.name == nullptr ||
        info.argumentCount > maximumArgumentCount ||
        info.sharedImageCount > maximumSharedImageCount ||
        (info.argumentCount != 0 && info.arguments == nullptr) ||
        (info.sharedImageCount != 0 && info.sharedImages == nullptr) ||
        (info.capabilities & ~processCapabilityDisplay) != 0 ||
        !validateUserMemory(parent->addressSpace().rootPhysicalAddress(),
                            reinterpret_cast<uint64_t>(info.image),
                            info.imageSize, false)) {
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
            !validateUserMemory(
                parent->addressSpace().rootPhysicalAddress(),
                reinterpret_cast<uint64_t>(userSharedImages[index].image),
                userSharedImages[index].imageSize, false)) {
            return error(SyscallError::InvalidArgument);
        }
        sharedImages[index] = {
            reinterpret_cast<uint64_t>(userSharedImages[index].image),
            userSharedImages[index].imageSize, nullptr};
    }

    Process* logicalParent = parent;
    if (info.parentProcessId != 0) {
        logicalParent = findProcess(info.parentProcessId);
        if (logicalParent == nullptr || logicalParent == getKernelProcess() ||
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
            if (!copyFromUser(&argumentStorage[index][byte],
                              reinterpret_cast<const void*>(
                                  userPointers[index] + byte),
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
    Process* child = UserProgramLoader::loadImage(
        reinterpret_cast<uint64_t>(info.image), info.imageSize, name, parent,
        logicalParent, info.bootstrapHandle, info.capabilities,
        info.standardInputHandle,
        info.standardOutputHandle, info.standardErrorHandle,
        info.argumentCount, arguments, sharedImages, info.sharedImageCount,
        &loadError);
    if (child == nullptr) {
        return elfLoadError(loadError);
    }
    const uint64_t handle = parent->handles().add(
        child, HandleType::Process, handleRightRead | handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleGetBootstrapHandle(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    return process == nullptr ? 0 : process->bootstrapHandle();
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
    serialWrite(message);
    return 0;
}

bool hasDeviceCapability() {
    Process* process = getCurrentProcess();
    return process != nullptr &&
           process->hasCapability(processCapabilityDevice);
}

uint64_t handleEnumeratePci(uint64_t userFunctions, uint64_t capacity, uint64_t,
                            uint64_t, uint64_t, uint64_t) {
    if (!hasDeviceCapability() || capacity == 0 || capacity > 64) {
        return error(SyscallError::PermissionDenied);
    }
    os::PciFunctionInfo functions[64]{};
    const uint32_t count =
        PciAccess::enumerate(functions, static_cast<uint32_t>(capacity));
    return copyToUser(reinterpret_cast<void*>(userFunctions), functions,
                      sizeof(os::PciFunctionInfo) * count)
               ? count
               : error(SyscallError::InvalidArgument);
}

uint64_t handleReadPciConfig(uint64_t userFunction, uint64_t offset,
                             uint64_t userValue, uint64_t, uint64_t, uint64_t) {
    os::PciFunctionInfo function{};
    uint32_t value;
    if (!hasDeviceCapability() ||
        !copyFromUser(&function, reinterpret_cast<const void*>(userFunction),
                      sizeof(function)) ||
        !PciAccess::read(function, static_cast<uint16_t>(offset), value) ||
        !copyToUser(reinterpret_cast<void*>(userValue), &value,
                    sizeof(value))) {
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleWritePciConfig(uint64_t userFunction, uint64_t offset,
                              uint64_t value, uint64_t, uint64_t, uint64_t) {
    os::PciFunctionInfo function{};
    if (!hasDeviceCapability() ||
        !copyFromUser(&function, reinterpret_cast<const void*>(userFunction),
                      sizeof(function)) ||
        !PciAccess::write(function, static_cast<uint16_t>(offset),
                          static_cast<uint32_t>(value))) {
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleMapDeviceMmio(uint64_t userFunction, uint64_t barIndex,
                             uint64_t userAddress, uint64_t size, uint64_t,
                             uint64_t) {
    Process* process = getCurrentProcess();
    os::PciFunctionInfo function{};
    if (!hasDeviceCapability() ||
        !copyFromUser(&function, reinterpret_cast<const void*>(userFunction),
                      sizeof(function))) {
        return error(SyscallError::PermissionDenied);
    }
    MmioRegion* region = MmioRegion::map(
        *process, function, static_cast<uint8_t>(barIndex), userAddress, size);
    if (region == nullptr) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t handle = process->handles().add(
        region, HandleType::MmioRegion, handleRightRead | handleRightWrite);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleAllocateDma(uint64_t userAddress, uint64_t userInfo, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    if (!hasDeviceCapability()) {
        return error(SyscallError::PermissionDenied);
    }
    os::DmaBufferInfo info{};
    DmaBuffer* buffer = DmaBuffer::create(*process, userAddress, info);
    if (buffer == nullptr) {
        return error(SyscallError::OutOfMemory);
    }
    info.handle = process->handles().add(buffer, HandleType::DmaBuffer,
                                         handleRightRead | handleRightWrite);
    if (info.handle == 0 ||
        !copyToUser(reinterpret_cast<void*>(userInfo), &info, sizeof(info))) {
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleBindIrq(uint64_t irq, uint64_t endpointHandle, uint64_t,
                       uint64_t, uint64_t, uint64_t) {
    Process* process   = getCurrentProcess();
    Endpoint* endpoint = resolveEndpoint(endpointHandle, handleRightRead);
    if (!hasDeviceCapability() || endpoint == nullptr || irq > 15) {
        return error(SyscallError::PermissionDenied);
    }
    InterruptBinding* binding =
        InterruptBinding::create(*process, static_cast<uint8_t>(irq), endpoint);
    if (binding == nullptr) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t handle = process->handles().add(
        binding, HandleType::InterruptBinding, handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
}

uint64_t handleGetProcessState(uint64_t processHandle, uint64_t userExitCode,
                               uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* process = static_cast<Process*>(getCurrentProcess()->handles().get(
        processHandle, HandleType::Process, handleRightRead));
    if (process == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    const int exitCode = process->exitCode();
    if (!copyToUser(reinterpret_cast<void*>(userExitCode), &exitCode,
                    sizeof(exitCode))) {
        return error(SyscallError::InvalidArgument);
    }
    const ProcessState state = process->state();
    if (state == ProcessState::Stopped) return 2;
    if (state != ProcessState::Terminated) return 0;
    process->destroy();
    return 1;
}

uint64_t handleGetProcessInfo(uint64_t processHandle, uint64_t userInfo,
                              uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* process = static_cast<Process*>(getCurrentProcess()->handles().get(
        processHandle, HandleType::Process, handleRightRead));
    if (process == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    os::ProcessInfo info{};
    info.id       = process->id();
    info.state    = static_cast<uint32_t>(process->state());
    info.exitCode = process->exitCode();
    const char* name = process->name();
    uint32_t index = 0;
    while (index + 1 < sizeof(info.name) && name[index] != '\0') {
        info.name[index] = name[index];
        ++index;
    }
    return copyToUser(reinterpret_cast<void*>(userInfo), &info, sizeof(info))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleTerminateProcess(uint64_t processHandle, uint64_t exitCode,
                                uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* process = static_cast<Process*>(getCurrentProcess()->handles().get(
        processHandle, HandleType::Process, handleRightManage));
    if (process == nullptr || process == getCurrentProcess() ||
        process == getKernelProcess()) {
        return error(SyscallError::InvalidHandle);
    }
    process->terminateOtherThreads(static_cast<int>(exitCode));
    process->destroy();
    return 0;
}

uint64_t handleCloseHandle(uint64_t handle, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
    return getCurrentProcess()->handles().close(handle)
               ? 0
               : error(SyscallError::InvalidHandle);
}

uint64_t handleGetProcessId(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t) {
    Process* process = getCurrentProcess();
    return process == nullptr ? 0 : process->id();
}

uint64_t handleGetThreadId(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t) {
    Thread* thread = getCurrentThread();
    return thread == nullptr ? 0 : thread->id;
}

uint64_t handleGetMonotonicTime(uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t) {
    return getMonotonicTime();
}

uint64_t handleGetWallClockTime(uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t) {
    return getWallClockTime();
}

uint64_t handleReadIoPort(uint64_t port, uint64_t userValue, uint64_t,
                          uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    uint8_t value;
    if (process == nullptr || port > 0xffff ||
        !PortAccess::read8(*process, static_cast<uint16_t>(port), value)) {
        return error(SyscallError::PermissionDenied);
    }
    return copyToUser(reinterpret_cast<void*>(userValue), &value, sizeof(value))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleWriteIoPort(uint64_t port, uint64_t value, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    return process != nullptr && port <= 0xffff && value <= 0xff &&
                   PortAccess::write8(*process, static_cast<uint16_t>(port),
                                      static_cast<uint8_t>(value))
               ? 0
               : error(SyscallError::PermissionDenied);
}

uint64_t handleMapFramebuffer(uint64_t userAddress, uint64_t userInfo,
                              uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    if (process == nullptr ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            userInfo, sizeof(os::DisplayInfo), true)) {
        return error(SyscallError::InvalidArgument);
    }
    os::DisplayInfo info{};
    if (!DisplayAccess::mapFramebuffer(*process, userAddress, info)) {
        return error(SyscallError::PermissionDenied);
    }
    return copyToUser(reinterpret_cast<void*>(userInfo), &info, sizeof(info))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleCreatePipe(uint64_t userHandles, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    os::PipeHandles handles{};
    if (process == nullptr ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            userHandles, sizeof(handles), true) ||
        !createPipeHandles(*process, handles.readHandle,
                           handles.writeHandle)) {
        return error(SyscallError::OutOfMemory);
    }
    if (!copyToUser(reinterpret_cast<void*>(userHandles), &handles,
                    sizeof(handles))) {
        process->handles().close(handles.readHandle);
        process->handles().close(handles.writeHandle);
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleReadPipe(uint64_t handle, uint64_t userBuffer, uint64_t size,
                        uint64_t, uint64_t, uint64_t) {
    constexpr size_t maximumTransfer = 512;
    Process* process = getCurrentProcess();
    if (process == nullptr || size == 0 || size > maximumTransfer ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            userBuffer, size, true)) {
        return error(SyscallError::InvalidArgument);
    }
    uint8_t buffer[maximumTransfer];
    void* object = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!process->handles().inspect(handle, &object, &type, &rights)) {
        return error(SyscallError::InvalidHandle);
    }
    const int64_t result =
        type == HandleType::Pty
            ? readPtyHandle(*process, handle, buffer, static_cast<size_t>(size))
            : readPipeHandle(*process, handle, buffer,
                             static_cast<size_t>(size));
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
    Process* process = getCurrentProcess();
    uint8_t buffer[maximumTransfer];
    if (process == nullptr || size == 0 || size > maximumTransfer ||
        !copyFromUser(buffer, reinterpret_cast<const void*>(userBuffer),
                      static_cast<size_t>(size))) {
        return error(SyscallError::InvalidArgument);
    }
    void* object = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!process->handles().inspect(handle, &object, &type, &rights)) {
        return error(SyscallError::InvalidHandle);
    }
    return static_cast<uint64_t>(
        type == HandleType::Pty
            ? writePtyHandle(*process, handle, buffer,
                             static_cast<size_t>(size))
            : writePipeHandle(*process, handle, buffer,
                              static_cast<size_t>(size)));
}

uint64_t handleCreatePty(uint64_t userHandles, uint64_t, uint64_t, uint64_t,
                         uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    os::PtyHandles handles{};
    if (process == nullptr ||
        !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                            userHandles, sizeof(handles), true) ||
        !createPtyHandles(*process, handles.masterHandle,
                          handles.slaveHandle)) {
        return error(SyscallError::OutOfMemory);
    }
    if (!copyToUser(reinterpret_cast<void*>(userHandles), &handles,
                    sizeof(handles))) {
        process->handles().close(handles.masterHandle);
        process->handles().close(handles.slaveHandle);
        return error(SyscallError::InvalidArgument);
    }
    return 0;
}

uint64_t handleGetPtyAttributes(uint64_t handle, uint64_t userAttributes,
                                uint64_t, uint64_t, uint64_t, uint64_t) {
    os::TerminalAttributes attributes{};
    Process* process = getCurrentProcess();
    return process != nullptr && getPtyAttributes(*process, handle, attributes) &&
                   copyToUser(reinterpret_cast<void*>(userAttributes),
                              &attributes, sizeof(attributes))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleSetPtyAttributes(uint64_t handle, uint64_t userAttributes,
                                uint64_t, uint64_t, uint64_t, uint64_t) {
    os::TerminalAttributes attributes{};
    Process* process = getCurrentProcess();
    return process != nullptr &&
                   copyFromUser(&attributes,
                                reinterpret_cast<const void*>(userAttributes),
                                sizeof(attributes)) &&
                   setPtyAttributes(*process, handle, attributes)
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleGetPtyWindowSize(uint64_t handle, uint64_t userSize, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    os::TerminalWindowSize size{};
    Process* process = getCurrentProcess();
    return process != nullptr && getPtyWindowSize(*process, handle, size) &&
                   copyToUser(reinterpret_cast<void*>(userSize), &size,
                              sizeof(size))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleSetPtyWindowSize(uint64_t handle, uint64_t userSize, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    os::TerminalWindowSize size{};
    Process* process = getCurrentProcess();
    return process != nullptr &&
                   copyFromUser(&size, reinterpret_cast<const void*>(userSize),
                                sizeof(size)) &&
                   setPtyWindowSize(*process, handle, size)
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleSetPtyForegroundGroup(uint64_t handle, uint64_t groupId,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    return process != nullptr &&
                   setPtyForegroundGroup(*process, handle, groupId)
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleSetPtyNonblocking(uint64_t handle, uint64_t enabled, uint64_t,
                                 uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    return process != nullptr &&
                   setPtyNonblocking(*process, handle, enabled != 0)
               ? 0
               : error(SyscallError::InvalidHandle);
}

uint64_t handleSendSignal(uint64_t processId, uint64_t signal, uint64_t,
                          uint64_t, uint64_t, uint64_t) {
    const int64_t target = static_cast<int64_t>(processId);
    Process* current = getCurrentProcess();
    const bool delivered =
        target > 0
            ? sendSignalToProcess(static_cast<uint64_t>(target),
                                  static_cast<uint32_t>(signal))
            : target == 0 && current != nullptr
                  ? sendSignalToProcessGroup(current->processGroupId(),
                                             static_cast<uint32_t>(signal))
                  : target < -1
                        ? sendSignalToProcessGroup(
                              static_cast<uint64_t>(-target),
                              static_cast<uint32_t>(signal))
                        : false;
    return delivered
               ? 0
               : error(SyscallError::NotFound);
}

uint64_t handleCreateSession(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t) {
    Process* process = getCurrentProcess();
    return process != nullptr && process->createSession()
               ? process->sessionId()
               : error(SyscallError::PermissionDenied);
}

struct ProcessIdentifiers {
    uint64_t parentId;
    uint64_t processGroupId;
    uint64_t sessionId;
};

uint64_t handleGetProcessIdentifiers(uint64_t userIdentifiers, uint64_t,
                                     uint64_t, uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    if (process == nullptr) {
        return error(SyscallError::NotFound);
    }
    ProcessIdentifiers identifiers{
        process->parent() == nullptr ? 0 : process->parent()->id(),
        process->processGroupId(), process->sessionId()};
    return copyToUser(reinterpret_cast<void*>(userIdentifiers), &identifiers,
                      sizeof(identifiers))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleGetClockResolution(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t) {
    return getClockResolution();
}

uint64_t handlePosixUnsupported(uint64_t operation, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    return error(dispatchUnsupportedPosixOperation(
        static_cast<PosixOperation>(operation)));
}

uint64_t handleWaitThread(uint64_t threadId, uint64_t userExitCode,
                          uint64_t noHang, uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    Thread* current = getCurrentThread();
    if (process == nullptr || current == nullptr || userExitCode == 0) {
        return error(SyscallError::InvalidArgument);
    }
    while (true) {
        int exitCode = 0;
        const int result =
            process->waitForThread(threadId, current, exitCode, noHang != 0);
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
        blockThread();
    }
}

uint64_t handleWaitChild(uint64_t processId, uint64_t userExitCode,
                         uint64_t options, uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    Thread* current = getCurrentThread();
    if (process == nullptr || current == nullptr || userExitCode == 0 ||
        (options & ~11ULL) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    while (true) {
        int status = 0;
        uint64_t childId = 0;
        const int result = process->waitForChild(
            static_cast<int64_t>(processId), current, status,
            static_cast<uint32_t>(options), childId);
        if (result < 0) return error(SyscallError::NotFound);
        if (result == 2) return 0;
        if (result == 0) {
            return copyToUser(reinterpret_cast<void*>(userExitCode), &status,
                              sizeof(status))
                       ? childId
                       : error(SyscallError::InvalidArgument);
        }
        blockThread();
    }
}

uint64_t handleForkProcess(SyscallContext* context) {
    Process* parent = getCurrentProcess();
    Thread* current = getCurrentThread();
    if (context == nullptr || parent == nullptr || current == nullptr ||
        parent == getKernelProcess()) {
        return error(SyscallError::InvalidArgument);
    }
    Process* child = Process::create(parent->name(), parent);
    if (child == nullptr) return error(SyscallError::OutOfMemory);
    bool cloned = false;
    {
        PreemptionSpinLockGuard addressGuard(parent->addressSpace().lock());
        cloned = cloneAddressSpaceRoot(
            parent->addressSpace().rootPhysicalAddress(),
            child->addressSpace().rootPhysicalAddress());
    }
    if (!cloned || !cloneProcessSharedMemoryMappings(*parent, *child) ||
        !child->inheritForFork(*parent)) {
        child->destroy();
        return error(SyscallError::OutOfMemory);
    }
    SyscallContext childContext = *context;
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    childContext.userRflags =
        (childContext.userRflags & allowedFlags) | 0x202;
    if (createForkedUserThread(child, childContext, current->signalMask) ==
        nullptr) {
        child->destroy();
        return error(SyscallError::OutOfMemory);
    }
    return child->id();
}

bool handleReplaceProcessImage(SyscallContext* context,
                               uint64_t& failureResult) {
    constexpr uint32_t maximumArgumentCount = 8;
    constexpr uint32_t maximumEnvironmentCount = 16;
    constexpr uint32_t maximumSharedImageCount = 3;
    constexpr uint32_t maximumStringLength = 255;
    constexpr uint32_t maximumNameLength = 95;
    constexpr uint64_t maximumImageSize = 8ULL * 1024 * 1024;
    constexpr uint64_t maximumRuntimeStateSize = 16 * 1024;
    Process* process = getCurrentProcess();
    Thread* current = getCurrentThread();
    os::ExecImageInfo info{};
    if (context == nullptr || process == nullptr || current == nullptr ||
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
    const uint64_t oldRoot = process->addressSpace().rootPhysicalAddress();
    if (!validateUserMemory(oldRoot, reinterpret_cast<uint64_t>(info.image),
                            info.imageSize, false) ||
        (info.runtimeStateSize != 0 &&
         !validateUserMemory(oldRoot,
                             reinterpret_cast<uint64_t>(info.runtimeState),
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
            !validateUserMemory(
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

    const uint64_t newRoot = createAddressSpaceRoot();
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
            if (getPhysicalAddressInAddressSpace(
                    newRoot, runtimeStateAddress + offset) != 0) {
                loadError = ElfLoadError::MappingFailed;
                break;
            }
            const UserPageMapResult mapped = allocateUserPageInAddressSpace(
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
            if (!copyFromAddressSpace(
                    buffer, oldRoot,
                    reinterpret_cast<uint64_t>(info.runtimeState) + copied,
                    chunk) ||
                !copyToAddressSpace(newRoot, runtimeStateAddress + copied,
                                    buffer, chunk)) {
                loadError = ElfLoadError::MappingFailed;
                break;
            }
            copied += chunk;
        }
        if (loadError == ElfLoadError::None) {
            for (uint64_t offset = 0; offset < mappedSize;
                 offset += pageSize) {
                protectPageInAddressSpace(
                    newRoot, runtimeStateAddress + offset,
                    pageUser | pageNoExecute);
            }
        }
    }
    uint64_t stackPointer = 0;
    if (loadError == ElfLoadError::None) {
        loadError = buildUserStack(
            newRoot, name, info.argumentCount, arguments,
            info.environmentCount, environment, runtimeStateAddress,
            stackPointer);
    }
    if (loadError != ElfLoadError::None) {
        destroyAddressSpaceRoot(newRoot);
        failureResult = elfLoadError(loadError);
        return false;
    }
    if (!process->commitExec(newRoot, name, current)) {
        destroyAddressSpaceRoot(newRoot);
        failureResult = error(SyscallError::Busy);
        return false;
    }
    *context = {};
    context->userRip = loadResult.entryPoint;
    context->userRsp = stackPointer;
    context->userRflags = 0x202;
    current->userStack = reinterpret_cast<void*>(stackPointer);
    return true;
}

uint64_t handleWaitAddress(uint64_t userAddress, uint64_t expected, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = waitOnUserAddress(
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
    return wakeUserAddress(reinterpret_cast<volatile uint32_t*>(userAddress),
                           static_cast<uint32_t>(count));
}

uint64_t handleSetSignalAction(uint64_t signal, uint64_t userAction,
                               uint64_t userOldAction, uint64_t, uint64_t,
                               uint64_t) {
    Process* process = getCurrentProcess();
    if (process == nullptr || !isValidSignal(static_cast<uint32_t>(signal))) {
        return error(SyscallError::InvalidArgument);
    }
    SignalAction action{};
    SignalAction oldAction{};
    if (userAction != 0) {
        if (!copyFromUser(&action,
                          reinterpret_cast<const void*>(userAction),
                          sizeof(action)) ||
            (action.flags & ~(signalFlagNoDefer | signalFlagResetHand |
                              0x00000004ULL | 0x10000000ULL)) != 0) {
            return error(SyscallError::InvalidArgument);
        }
        if (action.handler > signalIgnore &&
            (action.restorer == 0 ||
             !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                                 action.handler, 1, false) ||
             !validateUserMemory(process->addressSpace().rootPhysicalAddress(),
                                 action.restorer, 1, false))) {
            return error(SyscallError::InvalidArgument);
        }
        if (!process->setSignalAction(static_cast<uint32_t>(signal), action,
                                      &oldAction)) {
            return error(SyscallError::InvalidArgument);
        }
    } else if (!process->getSignalAction(static_cast<uint32_t>(signal),
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

uint64_t handleSetSignalMask(uint64_t how, uint64_t mask,
                             uint64_t userOldMask, uint64_t, uint64_t,
                             uint64_t) {
    Thread* thread = getCurrentThread();
    if (thread == nullptr || how > 2 || userOldMask == 0) {
        return error(SyscallError::InvalidArgument);
    }
    const uint64_t oldMask = thread->signalMask;
    mask &= ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
              (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
    if (how == 0) thread->signalMask |= mask;
    else if (how == 1) thread->signalMask &= ~mask;
    else thread->signalMask = mask;
    return copyToUser(reinterpret_cast<void*>(userOldMask), &oldMask,
                      sizeof(oldMask))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleGetPendingSignals(uint64_t userMask, uint64_t, uint64_t,
                                 uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    if (process == nullptr) return error(SyscallError::InvalidArgument);
    const uint64_t mask = process->pendingSignals(getCurrentThread());
    return copyToUser(reinterpret_cast<void*>(userMask), &mask, sizeof(mask))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleWaitSignal(uint64_t mask, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    Thread* thread = getCurrentThread();
    SignalAction action{};
    if (process == nullptr || thread == nullptr || mask == 0) {
        return error(SyscallError::InvalidArgument);
    }
    const uint32_t signal =
        process->takePendingSignal(thread, mask, action, false);
    return signal == 0 ? error(SyscallError::WouldBlock) : signal;
}

uint64_t handleSignalReturn(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t) {
    return error(SyscallError::InvalidArgument);
}

uint64_t handleOpenMessageQueue(uint64_t userName, uint64_t flags,
                                uint64_t maximumMessages,
                                uint64_t messageSize, uint64_t, uint64_t) {
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
    const uint64_t handle = openMessageQueue(
        *getCurrentProcess(), name, static_cast<int>(flags),
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
    return terminated && unlinkMessageQueue(name)
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
    const int result = sendMessageQueue(*getCurrentProcess(), handle, data,
                                        size, static_cast<uint32_t>(priority));
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
    const int64_t result = receiveMessageQueue(
        *getCurrentProcess(), handle, data, capacity, priority);
    if (result == -2) return error(SyscallError::WouldBlock);
    if (result < 0 ||
        (result != 0 &&
         !copyToUser(reinterpret_cast<void*>(userData), data,
                     static_cast<size_t>(result))) ||
        (userPriority != 0 &&
         !copyToUser(reinterpret_cast<void*>(userPriority), &priority,
                     sizeof(priority)))) {
        return error(SyscallError::InvalidArgument);
    }
    return static_cast<uint64_t>(result);
}

uint64_t handleGetMessageQueueAttributes(uint64_t handle,
                                         uint64_t userAttributes, uint64_t,
                                         uint64_t, uint64_t, uint64_t) {
    MessageQueueAttributes attributes{};
    return getMessageQueueAttributes(*getCurrentProcess(), handle, attributes) &&
                   copyToUser(reinterpret_cast<void*>(userAttributes),
                              &attributes, sizeof(attributes))
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleSetMessageQueueFlags(uint64_t handle, uint64_t flags,
                                    uint64_t userOldFlags, uint64_t, uint64_t,
                                    uint64_t) {
    uint64_t oldFlags = 0;
    return setMessageQueueFlags(*getCurrentProcess(), handle, flags, oldFlags) &&
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
    const uint64_t handle = openSharedMemory(
        *getCurrentProcess(), name, static_cast<int>(flags));
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
    return terminated && unlinkSharedMemory(name)
               ? 0
               : error(SyscallError::NotFound);
}

uint64_t handleResizeSharedMemory(uint64_t handle, uint64_t size, uint64_t,
                                  uint64_t, uint64_t, uint64_t) {
    return resizeSharedMemory(*getCurrentProcess(), handle, size)
               ? 0
               : error(SyscallError::InvalidArgument);
}

uint64_t handleMapSharedMemory(uint64_t handle, uint64_t address,
                               uint64_t size, uint64_t offset,
                               uint64_t protection, uint64_t) {
    const uint64_t result = mapSharedMemory(*getCurrentProcess(), handle,
                                            address, size, offset, protection);
    return result == 0 ? error(SyscallError::InvalidArgument) : result;
}

uint64_t handleSendThreadSignal(uint64_t threadId, uint64_t signal, uint64_t,
                                uint64_t, uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    if (process == nullptr || signal > maximumSignal) {
        return error(SyscallError::InvalidArgument);
    }
    return process->deliverThreadSignal(threadId, static_cast<uint32_t>(signal))
               ? 0
               : error(SyscallError::NotFound);
}

uint64_t handleRegisterMessageQueueNotification(uint64_t handle,
                                                uint64_t type,
                                                uint64_t signal, uint64_t,
                                                uint64_t, uint64_t) {
    if (type > 2 || signal > UINT32_MAX) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = registerMessageQueueNotification(
        *getCurrentProcess(), handle, static_cast<int>(type),
        static_cast<uint32_t>(signal));
    return result == 0
               ? 0
               : error(result == 1 ? SyscallError::Busy
                                   : SyscallError::InvalidArgument);
}

uint64_t handleGetStandardHandle(uint64_t index, uint64_t, uint64_t, uint64_t,
                                 uint64_t, uint64_t) {
    Process* process = getCurrentProcess();
    return process == nullptr ? 0
                              : process->standardHandle(
                                    static_cast<uint32_t>(index));
}

constexpr SyscallHandler syscallHandlers[] = {
    handleExitProcess,   handleCreateThread,
    handleExitThread,    handleYield,
    handleSleep,         handleMapMemory,
    handleUnmapMemory,   handleCreateEndpoint,
    handleSendMessage,   handleReceiveMessage,
    handleCallService,   handleReplyMessage,
    handleSpawnProgram,  handleGetBootstrapHandle,
    handleLog,           handleEnumeratePci,
    handleReadPciConfig, handleWritePciConfig,
    handleMapDeviceMmio, handleAllocateDma,
    handleBindIrq,       handleGetProcessState,
    handleCloseHandle,   handleGetProcessId,
    handleGetThreadId,   handleGetMonotonicTime,
    handleGetWallClockTime, handleReadIoPort,
    handleWriteIoPort,   handleMapFramebuffer,
    handleSpawnImage,    handleCreatePipe,
    handleReadPipe,      handleWritePipe,
    handleGetStandardHandle, handleGetProcessInfo,
    handleTerminateProcess, handleCreatePty,
    handleGetPtyAttributes, handleSetPtyAttributes,
    handleGetPtyWindowSize, handleSetPtyWindowSize,
    handleSetPtyForegroundGroup, handleSetPtyNonblocking,
    handleSendSignal, handleCreateSession,
    handleGetProcessIdentifiers, handleGetClockResolution,
    handlePosixUnsupported, handleWaitThread,
    handleWaitAddress, handleWakeAddress,
    handleSetSignalAction, handleSetSignalMask,
    handleGetPendingSignals, handleWaitSignal,
    handleSignalReturn, handleOpenMessageQueue,
    handleUnlinkMessageQueue, handleSendMessageQueue,
    handleReceiveMessageQueue, handleGetMessageQueueAttributes,
    handleSetMessageQueueFlags, handleProtectMemory,
    handleOpenSharedMemory, handleUnlinkSharedMemory,
    handleResizeSharedMemory, handleMapSharedMemory,
    handleSendThreadSignal, handleRegisterMessageQueueNotification,
    handlePosixUnsupported, handleWaitChild, handlePosixUnsupported};

static_assert(sizeof(syscallHandlers) / sizeof(syscallHandlers[0]) ==
              static_cast<uint64_t>(SyscallNumber::Count));

}  // namespace

uint64_t dispatchSyscall(uint64_t syscallNumber, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6) {
    if (syscallNumber >= static_cast<uint64_t>(SyscallNumber::Count)) {
        return error(SyscallError::NotSupported);
    }
    return syscallHandlers[syscallNumber](arg1, arg2, arg3, arg4, arg5, arg6);
}

extern "C" void syscallDispatchEntry(SyscallContext* context) {
    Thread* thread = getCurrentThread();
    if (context == nullptr || thread == nullptr ||
        thread->type != ThreadType::User ||
        context->userRip >= maximumUserAddress ||
        context->userRsp >= maximumUserAddress) {
        terminateCurrentThreadFromException(-1);
    }
    if (context->rax == static_cast<uint64_t>(SyscallNumber::SignalReturn)) {
        if (!restoreSignalFrame(context, context->rdi)) {
            terminateCurrentThreadFromException(-1);
        }
        return;
    }
    if (context->rax == static_cast<uint64_t>(SyscallNumber::ForkProcess)) {
        context->rax = handleForkProcess(context);
    } else if (context->rax ==
               static_cast<uint64_t>(SyscallNumber::ReplaceProcessImage)) {
        uint64_t failureResult = 0;
        if (handleReplaceProcessImage(context, failureResult)) return;
        context->rax = failureResult;
    } else {
        context->rax = dispatchSyscall(
            context->rax, context->rdi, context->rsi, context->rdx,
            context->r10, context->r8, context->r9);
    }
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    context->userRflags = (context->userRflags & allowedFlags) | 0x202;
    deliverPendingSignal(context);
}

}  // namespace kernel
