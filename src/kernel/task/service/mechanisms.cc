/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/service/mechanisms.cc
 * Caller-bound thread creation, fork, and executable-image replacement.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/device.h>

namespace kernel::task {

namespace {
constexpr uint64_t pageSize = 4096;
// Encode native negative errors in the unsigned mechanism-result field.
uint64_t error(SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}
// Keep loader-format failures distinct from memory exhaustion without exposing
// implementation-specific loader enums through the task ABI.
uint64_t elfLoadError(::kernel::elf::ElfLoadError loadError) {
    switch (loadError) {
        case ::kernel::elf::ElfLoadError::OutOfMemory:
            return error(SyscallError::OutOfMemory);
        case ::kernel::elf::ElfLoadError::UnsupportedClass:
        case ::kernel::elf::ElfLoadError::UnsupportedEndian:
        case ::kernel::elf::ElfLoadError::UnsupportedVersion:
        case ::kernel::elf::ElfLoadError::UnsupportedMachine:
        case ::kernel::elf::ElfLoadError::UnsupportedType:
        case ::kernel::elf::ElfLoadError::UnsupportedProgramHeader:
            return error(SyscallError::NotSupported);
        default:
            return error(SyscallError::InvalidArgument);
    }
}
}  // namespace
// Bind mechanisms to the actual caller; never emulate caller identity by
// changing another worker's CPU-local current task.
TaskMechanisms::TaskMechanisms(Task& caller) : caller_(caller) {}
// Prepare legitimate lazy entry/stack pages through the pager before thread
// creation. Build detached, capture its ID, then enqueue; another CPU may use
// or exit the thread immediately after runnable publication.
uint64_t TaskMechanisms::createThread(uint64_t entryPoint,
                                      uint64_t stackPointer,
                                      uint64_t argument) {
    Task* task = &caller_;
    if (task != task::getCurrent() ||
        !mm::pagerBridge().ensureUserMemory(entryPoint, 1, false) ||
        stackPointer < 8 ||
        !mm::pagerBridge().ensureUserMemory(stackPointer - 8, 8, true)) {
        return error(SyscallError::InvalidArgument);
    }
    Thread* thread = Thread::createUser(task, entryPoint, stackPointer,
                                        argument, 0, ~0ULL, false);
    if (!thread) return error(SyscallError::OutOfMemory);
    const uint64_t id = thread->getId();
    task::enqueueThread(thread);
    return id;
}
// Clone the calling user continuation, not a service-worker stack. First write
// the child's zero reply into memory that the child will inherit, then clone
// roots/mapping references and metadata. User MM clone must complete before a
// child thread is made runnable. Partial children go to the normal reclaimer;
// hardware resource owners cannot use generic fork.
uint64_t TaskMechanisms::fork() {
    SyscallContext* context = currentThread()->syscallContext();

    Task* parent    = &caller_;
    Thread* current = currentThread();
    if (context == nullptr || parent == nullptr || current == nullptr ||
        parent == task::getIdle()) {
        return error(SyscallError::InvalidArgument);
    }
    // Hardware mappings cannot escape their owner through copied page tables.
    if (device::resourceDomains().owned(*parent))
        return error(SyscallError::PermissionDenied);
    // The copied user reply must already say zero when clone takes its
    // snapshot. Doing this after cloning would leave the child with a parent
    // result or require an unsafe write through another process's COW
    // mappings.
    IpcMessage childReply{};
    childReply.length = 1;
    if (!task::copyToUser(reinterpret_cast<void*>(context->rdx), &childReply,
                          sizeof(childReply))) {
        return error(SyscallError::BadAddress);
    }
    Task* child = Task::create(parent->name(), parent);
    if (child == nullptr) return error(SyscallError::OutOfMemory);
    bool cloned = false;
    {
        PreemptionSpinLockGuard addressGuard(parent->addressSpace().lock());
        cloned =
            mm::cloneAddressSpaceRoot(
                parent->addressSpace().rootPhysicalAddress(),
                child->addressSpace().rootPhysicalAddress()) &&
            ::kernel::mm::sharedMemory().cloneTaskMappings(*parent, *child) &&
            mm::maps().cloneTask(*parent, *child);
    }
    if (!cloned || !child->inheritForFork(*parent) ||
        !mm::pagerBridge().clone(*parent, *child)) {
        child->destroy();
        return error(SyscallError::OutOfMemory);
    }
    // Sanitize inherited flags before child execution; createForkedUser sets
    // RAX zero and resumes through the complete register-return path.
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
// Prepare exec entirely in a new root while source bytes, argument strings and
// runtime state still belong to the old mapping. failureResult is the native
// error on failure. Only after all preparation succeeds may commitExec retire
// siblings and replace the root; successful exec has no ordinary reply into
// the obsolete user request/reply addresses.
bool TaskMechanisms::replace(uint64_t image, uint64_t& failureResult) {
    SyscallContext* context = currentThread()->syscallContext();

    constexpr uint32_t maximumArgumentCount    = 64;
    constexpr uint32_t maximumEnvironmentCount = 64;
    constexpr uint32_t maximumSharedImageCount = 4;
    constexpr uint32_t maximumStringLength     = 383;
    constexpr uint32_t maximumNameLength       = 95;
    constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
    constexpr uint64_t maximumRuntimeStateSize = 16 * 1024;
    Task* task                                 = &caller_;
    Thread* current                            = currentThread();
    os::wire::ExecImageInfo info{};
    if (context == nullptr || task == nullptr || current == nullptr ||
        !task::copyFromUser(&info, reinterpret_cast<const void*>(image),
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
    if (!mm::validateUserMemory(oldRoot, reinterpret_cast<uint64_t>(info.image),
                                info.imageSize, false) ||
        (info.runtimeStateSize != 0 &&
         !mm::validateUserMemory(oldRoot,
                                 reinterpret_cast<uint64_t>(info.runtimeState),
                                 info.runtimeStateSize, false))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }

    // Copy shared-image descriptors before following their user pointers.
    // Bound every image separately, so a valid descriptor array cannot
    // authorize an unbounded or invalid image range.
    os::wire::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
    ::kernel::elf::ElfSharedImage sharedImages[maximumSharedImageCount]{};
    if (info.sharedImageCount != 0 &&
        !task::copyFromUser(
            userSharedImages, info.sharedImages,
            info.sharedImageCount * sizeof(os::wire::SharedImageInfo))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }
    for (uint32_t index = 0; index < info.sharedImageCount; ++index) {
        if (userSharedImages[index].image == nullptr ||
            userSharedImages[index].imageSize == 0 ||
            userSharedImages[index].imageSize > maximumImageSize ||
            !mm::validateUserMemory(
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
        if (!task::copyFromUser(&name[byte], info.name + byte, 1)) {
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
    // Heap storage keeps the maximum argument/environment strings off the
    // kernel stack. ThreadCleanup owns it during all early returns and also if
    // the caller dies before normal C++ destruction.
    class StringStorage {
       public:
        // Allocate with interrupts available but prevent a local preemption
        // gap between successful allocation and registering its forced-exit
        // cleanup.
        explicit StringStorage(size_t size) {
            // Keep cleanup registration atomic with respect to thread removal,
            // while allowing interrupts during the potentially large
            // allocation.
            cpu::disablePreemption();
            data_ = mm::allocateHeap(size);
            if (data_)
                cleanup_.attach(data_, [](void* data) { mm::freeHeap(data); });
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
         !task::copyFromUser(argumentPointers, info.arguments,
                             info.argumentCount * sizeof(uint64_t))) ||
        (info.environmentCount != 0 &&
         !task::copyFromUser(environmentPointers, info.environment,
                             info.environmentCount * sizeof(uint64_t)))) {
        failureResult = error(SyscallError::InvalidArgument);
        return false;
    }
    for (uint32_t index = 0; index < info.argumentCount; ++index) {
        bool terminated = false;
        for (uint32_t byte = 0; byte <= maximumStringLength; ++byte) {
            if (!task::copyFromUser(&argumentStorage[index][byte],
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
            if (!task::copyFromUser(&environmentStorage[index][byte],
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

    // From here until commit, this function owns newRoot and must destroy it
    // on any error; the live process still uses oldRoot and can receive a
    // normal failure reply without losing its original image.
    const uint64_t newRoot = mm::createAddressSpaceRoot();
    if (newRoot) mm::assignPhysicalPageOwner(newRoot, task->id());
    if (newRoot == 0) {
        failureResult = error(SyscallError::OutOfMemory);
        return false;
    }
    ::kernel::elf::ElfLoadResult loadResult{};
    ::kernel::elf::ElfLoadError loadError =
        info.sharedImageCount == 0
            ? ::kernel::elf::Loader(newRoot).loadFromAddressSpace(
                  oldRoot, reinterpret_cast<uint64_t>(info.image),
                  info.imageSize, loadResult)
            : ::kernel::elf::Loader(newRoot).loadDynamicSetFromAddressSpace(
                  oldRoot, reinterpret_cast<uint64_t>(info.image),
                  info.imageSize, sharedImages, info.sharedImageCount,
                  loadResult);
    // Preserve bounded runtime data in a dedicated read-only, non-executable
    // region of the new image. Reject address collisions instead of replacing
    // ELF mappings that were already prepared there.
    uint64_t runtimeStateAddress = 0;
    if (loadError == ::kernel::elf::ElfLoadError::None &&
        info.runtimeStateSize != 0) {
        runtimeStateAddress = os::execRuntimeStateAddress;
        const uint64_t mappedSize =
            (info.runtimeStateSize + pageSize - 1) & ~(pageSize - 1);
        for (uint64_t offset = 0; offset < mappedSize; offset += pageSize) {
            if (mm::getPhysicalAddressInAddressSpace(
                    newRoot, runtimeStateAddress + offset) != 0) {
                loadError = ::kernel::elf::ElfLoadError::MappingFailed;
                break;
            }
            const ::kernel::mm::UserPageMapResult mapped =
                mm::allocateUserPageInAddressSpace(
                    newRoot, runtimeStateAddress + offset,
                    ::kernel::mm::pageUser | ::kernel::mm::pageWritable |
                        ::kernel::mm::pageNoExecute);
            if (mapped != ::kernel::mm::UserPageMapResult::Success) {
                loadError =
                    mapped == ::kernel::mm::UserPageMapResult::OutOfMemory
                        ? ::kernel::elf::ElfLoadError::OutOfMemory
                        : ::kernel::elf::ElfLoadError::MappingFailed;
                break;
            }
        }
        uint8_t buffer[pageSize];
        uint64_t copied = 0;
        while (loadError == ::kernel::elf::ElfLoadError::None &&
               copied < info.runtimeStateSize) {
            const uint64_t remaining = info.runtimeStateSize - copied;
            const uint64_t chunk = remaining < pageSize ? remaining : pageSize;
            if (!mm::copyFromAddressSpace(
                    buffer, oldRoot,
                    reinterpret_cast<uint64_t>(info.runtimeState) + copied,
                    chunk) ||
                !mm::copyToAddressSpace(newRoot, runtimeStateAddress + copied,
                                        buffer, chunk)) {
                loadError = ::kernel::elf::ElfLoadError::MappingFailed;
                break;
            }
            copied += chunk;
        }
        if (loadError == ::kernel::elf::ElfLoadError::None) {
            for (uint64_t offset = 0; offset < mappedSize; offset += pageSize) {
                mm::protectPageInAddressSpace(
                    newRoot, runtimeStateAddress + offset,
                    ::kernel::mm::pageUser | ::kernel::mm::pageNoExecute);
            }
        }
    }
    // Build argv, environment and auxiliary handles into the new user stack
    // while both address spaces remain available to the preparation path.
    uint64_t stackPointer = 0;
    if (loadError == ::kernel::elf::ElfLoadError::None) {
        loadError = buildUserStack(
            newRoot, name, info.argumentCount, arguments, info.environmentCount,
            environment, runtimeStateAddress, stackPointer, &loadResult, task);
    }
    if (loadError != ::kernel::elf::ElfLoadError::None) {
        mm::destroyAddressSpaceRoot(newRoot);
        failureResult = elfLoadError(loadError);
        return false;
    }
    // Commit rejection leaves ownership here, so destroy the prepared root.
    // Once commit succeeds, old user addresses must no longer be dereferenced.
    if (!task->commitExec(newRoot, name, current)) {
        mm::destroyAddressSpaceRoot(newRoot);
        failureResult = error(SyscallError::Busy);
        return false;
    }
    task->setCommandLine(info.argumentCount, arguments);
    // Install only the new entry/stack and clean initial flags, reset FP
    // state, and mark the frame restored. Syscall completion will skip old
    // reply copy and must not overwrite these registers with an ordinary
    // success value.
    *context            = {};
    context->userRip    = loadResult.getEntryPoint();
    context->userRsp    = stackPointer;
    context->userRflags = 0x202;
    current->setUserStack(reinterpret_cast<void*>(stackPointer));
    const uint64_t interruptFlags = cpu::disableInterrupts();
    current->resetFloatingPointState();
    current->restoreFloatingPointState();
    cpu::restoreInterrupts(interruptFlags);
    current->markContextRestored();
    return true;
}

}  // namespace kernel::task
