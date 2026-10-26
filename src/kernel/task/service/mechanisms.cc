#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include <os/device.h>
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"

namespace kernel {
namespace {
constexpr uint64_t pageSize = 4096;
uint64_t error(SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
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
}  // namespace
TaskMechanisms::TaskMechanisms(Task& caller) : caller_(caller) {}
uint64_t TaskMechanisms::createThread(uint64_t entryPoint,
                                      uint64_t stackPointer,
                                      uint64_t argument) {
    Task* task = &caller_;
    if (task == nullptr ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    entryPoint, 1, false) ||
        stackPointer < 8 ||
        !memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    stackPointer - 8, 8, true)) {
        return error(SyscallError::InvalidArgument);
    }
    Thread* thread = Thread::createUser(task, entryPoint, stackPointer,
                                        argument, 0, ~0ULL, false);
    if (!thread) return error(SyscallError::OutOfMemory);
    const uint64_t id = thread->getId();
    schedule::enqueueThread(thread);
    return id;
}
uint64_t TaskMechanisms::fork() {
    SyscallContext* context = Thread::getCurrent()->syscallContext();

    Task* parent    = &caller_;
    Thread* current = Thread::getCurrent();
    if (context == nullptr || parent == nullptr || current == nullptr ||
        parent == task::getIdle()) {
        return error(SyscallError::InvalidArgument);
    }
    // Hardware mappings cannot escape their owner through copied page tables.
    if (device::ResourceDomain::owned(*parent))
        return error(SyscallError::PermissionDenied);
    IpcMessage childReply{};
    childReply.length = 1;
    if (!syscall::copyToUser(reinterpret_cast<void*>(context->rdx), &childReply,
                             sizeof(childReply))) {
        return error(SyscallError::BadAddress);
    }
    Task* child = Task::create(parent->name(), parent);
    if (child == nullptr) return error(SyscallError::OutOfMemory);
    bool cloned = false;
    {
        PreemptionSpinLockGuard addressGuard(parent->addressSpace().lock());
        cloned = memory::cloneAddressSpaceRoot(
                     parent->addressSpace().rootPhysicalAddress(),
                     child->addressSpace().rootPhysicalAddress()) &&
                 SharedMemory::cloneTaskMappings(*parent, *child) &&
                 memory::PageMappings::cloneTask(*parent, *child);
    }
    if (!cloned || !child->inheritForFork(*parent) ||
        !memory::PagerBridge::clone(*parent, *child)) {
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
bool TaskMechanisms::replace(uint64_t image, uint64_t& failureResult) {
    SyscallContext* context = Thread::getCurrent()->syscallContext();

    constexpr uint32_t maximumArgumentCount    = 64;
    constexpr uint32_t maximumEnvironmentCount = 64;
    constexpr uint32_t maximumSharedImageCount = 4;
    constexpr uint32_t maximumStringLength     = 383;
    constexpr uint32_t maximumNameLength       = 95;
    constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
    constexpr uint64_t maximumRuntimeStateSize = 16 * 1024;
    Task* task                                 = &caller_;
    Thread* current                            = Thread::getCurrent();
    os::wire::ExecImageInfo info{};
    if (context == nullptr || task == nullptr || current == nullptr ||
        !syscall::copyFromUser(&info, reinterpret_cast<const void*>(image),
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

    os::wire::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
    ElfSharedImage sharedImages[maximumSharedImageCount]{};
    if (info.sharedImageCount != 0 &&
        !syscall::copyFromUser(
            userSharedImages, info.sharedImages,
            info.sharedImageCount * sizeof(os::wire::SharedImageInfo))) {
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
        if (!syscall::copyFromUser(&name[byte], info.name + byte, 1)) {
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
    class StringStorage {
       public:
        explicit StringStorage(size_t size) {
            // Keep cleanup registration atomic with respect to thread removal,
            // while allowing interrupts during the potentially large
            // allocation.
            cpu::disablePreemption();
            data_ = heap::kernelAllocate(size);
            if (data_)
                cleanup_.attach(data_,
                                [](void* data) { heap::kernelFree(data); });
            cpu::enablePreemption();
        }
        void* data() const { return data_; }

       private:
        ThreadCleanup cleanup_;
        void* data_ = nullptr;
    } strings((maximumArgumentCount + maximumEnvironmentCount) *
              (maximumStringLength + 1));
    if (!strings.data()) {
        failureResult = error(SyscallError::OutOfMemory);
        return false;
    }
    auto* argumentStorage =
        static_cast<char(*)[maximumStringLength + 1]>(strings.data());
    auto* environmentStorage = argumentStorage + maximumArgumentCount;
    const char* arguments[maximumArgumentCount]{};
    const char* environment[maximumEnvironmentCount]{};
    if ((info.argumentCount != 0 &&
         !syscall::copyFromUser(argumentPointers, info.arguments,
                                info.argumentCount * sizeof(uint64_t))) ||
        (info.environmentCount != 0 &&
         !syscall::copyFromUser(environmentPointers, info.environment,
                                info.environmentCount * sizeof(uint64_t)))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }
    for (uint32_t index = 0; index < info.argumentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumStringLength; ++byte) {
            if (!syscall::copyFromUser(&argumentStorage[index][byte],
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
            if (!syscall::copyFromUser(&environmentStorage[index][byte],
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
    if (newRoot) memory::assignPhysicalPageOwner(newRoot, task->id());
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
            environment, runtimeStateAddress, stackPointer, &loadResult, task);
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
    task->setCommandLine(info.argumentCount, arguments);
    *context            = {};
    context->userRip    = loadResult.entryPoint;
    context->userRsp    = stackPointer;
    context->userRflags = 0x202;
    current->setUserStack(reinterpret_cast<void*>(stackPointer));
    const uint64_t interruptFlags = interrupt::disable();
    current->resetFloatingPointState();
    current->restoreFloatingPointState();
    interrupt::restore(interruptFlags);
    current->markContextRestored();
    return true;
}
}  // namespace kernel
