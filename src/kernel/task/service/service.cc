/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/service/service.cc
 * Authenticated task requests, image launch, waits, and process control.
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include "service/users/identity.h"
#include <os/device.h>

namespace kernel::task {

namespace {
// Keep source-memory access bound to the real caller. The source root can
// differ from the logical parent or child, so pointers must not be followed
// using either of those other address spaces.
class TaskImages {
    Task& caller_;
    // Encode a native negative error in the unsigned reply payload.
    static uint64_t error(SyscallError value) {
        return static_cast<uint64_t>(static_cast<int64_t>(value));
    }
    // Validate existing caller mappings under its address-space lock. This
    // loader helper does not fault in arbitrary image ranges or grant access
    // merely because an address was supplied in a launch descriptor.
    bool copyFromUser(void* destination, const void* source, size_t size) {
        const auto address = reinterpret_cast<uint64_t>(source);
        if (!destination || !size || address < 0x10000 ||
            address + size < address ||
            (address + size > 0x0000800000000000ULL &&
             !(caller_.isSystemService() && address >= 0xffff800000000000ULL)))
            return false;
        PreemptionSpinLockGuard guard(caller_.addressSpace().lock());
        const auto root = caller_.addressSpace().rootPhysicalAddress();
        if (!mm::validateUserMemory(root, address, size, false)) return false;
        if (address >= 0xffff800000000000ULL && caller_.isSystemService()) {
            copyMessageData(destination, source, size);
            return true;
        }
        return mm::copyFromAddressSpace(destination, root, address, size);
    }
    // Separate unsupported image formats from out-of-memory failures.
    static uint64_t elfLoadError(::kernel::elf::ElfLoadError loadError) {
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

   public:
    // Source task for memory reads, source handles and launch authorization.
    explicit TaskImages(Task& caller) : caller_(caller) {}
    // Authorize a fixed ProgramId, copy bounded arguments, then load and
    // install a child Task handle. A successful result is a handle, not the
    // child PID.
    uint64_t spawnProgram(uint64_t program, uint64_t bootstrapHandle,
                          uint64_t argumentCount, uint64_t userArguments) {
        constexpr uint32_t maximumArgumentCount  = 64;
        constexpr uint32_t maximumArgumentLength = 383;
        Task* parent                             = &caller_;
        const auto requested = static_cast<os::ProgramId>(program);
        // Init can launch boot programs. Task/Device can launch only their
        // listed subordinate service kinds, not every image compiled into the
        // system.
        const bool permitted =
            parent && (parent->serviceRole() == os::ServiceRole::Init ||
                       (parent->serviceRole() == os::ServiceRole::Task &&
                        (requested == os::ProgramId::PipeServer ||
                         requested == os::ProgramId::MessageQueueServer)) ||
                       (parent->serviceRole() == os::ServiceRole::Device &&
                        (requested == os::ProgramId::PciServer ||
                         requested == os::ProgramId::StorageServer)));
        if (!permitted) {
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
        ::kernel::elf::ElfLoadError loadError =
            ::kernel::elf::ElfLoadError::None;
        auto child = loadProgram(
            static_cast<os::ProgramId>(program), parent, bootstrapHandle,
            static_cast<uint32_t>(argumentCount), arguments, &loadError);
        if (child == nullptr) {
            device::serialWrite("[ELF] dynamic image load failed: ");
            device::serialWrite(::kernel::elf::elfErrorName(loadError));
            device::serialWrite("\n");
            return elfLoadError(loadError);
        }
        const uint64_t handle = parent->handles().add(
            child, HandleType::Task, handleRightRead | handleRightManage);
        return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
    }
    // Validate images and strings before child creation. Caller-supplied
    // capabilities must be zero; fixed-role capabilities come only from the
    // trusted service table. Loading failure leaves cleanup to the loader
    // path.
    uint64_t spawnImage(uint64_t userInfo) {
        constexpr uint32_t maximumArgumentCount    = 64;
        constexpr uint32_t maximumArgumentLength   = 383;
        constexpr uint32_t maximumNameLength       = 63;
        constexpr uint32_t maximumSharedImageCount = 4;
        constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
        Task* parent                               = &caller_;
        os::wire::SpawnImageInfo info{};
        if (parent == nullptr || !parent->hasCapability(taskCapabilitySpawn) ||
            !copyFromUser(&info, reinterpret_cast<const void*>(userInfo),
                          sizeof(info)) ||
            info.image == nullptr || info.imageSize == 0 ||
            info.imageSize > maximumImageSize || info.name == nullptr ||
            info.argumentCount > maximumArgumentCount ||
            info.sharedImageCount > maximumSharedImageCount ||
            (info.argumentCount != 0 && info.arguments == nullptr) ||
            (info.sharedImageCount != 0 && info.sharedImages == nullptr) ||
            info.capabilities != 0 || info.terminalKind > 2 ||
            (info.flags &
             ~(os::spawnFlagNewSession | os::spawnFlagManagedService)) ||
            !mm::validateUserMemory(
                parent->addressSpace().rootPhysicalAddress(),
                reinterpret_cast<uint64_t>(info.image), info.imageSize,
                false)) {
            return error(SyscallError::InvalidArgument);
        }

        os::wire::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
        ::kernel::elf::ElfSharedImage sharedImages[maximumSharedImageCount]{};
        if (info.sharedImageCount != 0 &&
            !copyFromUser(
                userSharedImages, info.sharedImages,
                info.sharedImageCount * sizeof(os::wire::SharedImageInfo))) {
            return error(SyscallError::InvalidArgument);
        }
        for (uint32_t index = 0; index < info.sharedImageCount; ++index) {
            if (userSharedImages[index].image == nullptr ||
                userSharedImages[index].imageSize == 0 ||
                userSharedImages[index].imageSize > maximumImageSize ||
                !mm::validateUserMemory(
                    parent->addressSpace().rootPhysicalAddress(),
                    reinterpret_cast<uint64_t>(userSharedImages[index].image),
                    userSharedImages[index].imageSize, false)) {
                return error(SyscallError::InvalidArgument);
            }
            sharedImages[index] = {
                reinterpret_cast<uint64_t>(userSharedImages[index].image),
                userSharedImages[index].imageSize, nullptr};
        }

        // Hold the logical parent alive across loading, while bytes and
        // handles still come from caller_. Parentage does not authorize a
        // managed role.
        auto logicalParentReference =
            task::find(info.parentTaskId ? info.parentTaskId : parent->id());
        Task* logicalParent = logicalParentReference;
        if (info.parentTaskId != 0) {
            if (logicalParent == nullptr || logicalParent == task::getIdle() ||
                logicalParent->isTerminated()) {
                return error(SyscallError::InvalidArgument);
            }
        }
        uint64_t serviceCapabilities = 0;
        const bool managed           = info.flags & os::spawnFlagManagedService;
        // Only Task acting for Init may create a supervised instance.
        // Otherwise an application could self-assign roles with recovery or
        // identity privileges.
        if ((managed || info.serviceRole) &&
            (parent->serviceRole() != os::ServiceRole::Task || !logicalParent ||
             logicalParent->serviceRole() != os::ServiceRole::Init || !managed))
            return error(SyscallError::PermissionDenied);
        if (info.serviceRole &&
            !task::serviceCapabilities(
                static_cast<os::ServiceRole>(info.serviceRole),
                serviceCapabilities))
            return error(SyscallError::PermissionDenied);

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

        ::kernel::elf::ElfLoadError loadError =
            ::kernel::elf::ElfLoadError::None;
        auto child = loadProgramImage(
            reinterpret_cast<uint64_t>(info.image), info.imageSize, name,
            parent, logicalParent, info.bootstrapHandle, serviceCapabilities,
            info.standardInputHandle, info.standardOutputHandle,
            info.standardErrorHandle, info.argumentCount, arguments,
            sharedImages, info.sharedImageCount, &loadError,
            static_cast<os::ServiceRole>(info.serviceRole),
            (info.flags & os::spawnFlagNewSession) != 0, managed,
            info.terminalKind, info.terminalIndex);
        if (child == nullptr) {
            device::serialWrite("[ELF] spawn image failed: ");
            device::serialWrite(::kernel::elf::elfErrorName(loadError));
            device::serialWrite("\n");
            return elfLoadError(loadError);
        }
        const uint64_t handle = parent->handles().add(
            child, HandleType::Task, handleRightRead | handleRightManage);
        return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
    }
};

namespace TaskService {
// Native dispatch errors are negative; POSIX operation status can instead be a
// positive errno stored separately in reply.data[1].
static int failure(os::ErrorCode code) { return static_cast<int>(code); }
// Copy a bounded task record with caller-based command-line disclosure.
// Counters are sampled independently; main nice is not every thread's nice.
static void describe(Task& target, Task& caller, IpcMessage& reply) {
    os::wire::TaskInfo info{};
    os::wire::CredentialInfo credentials{};
    target.credentials(credentials);
    info.id           = target.id();
    info.state        = static_cast<uint32_t>(target.state());
    info.exitCode     = target.exitCode();
    info.uid          = credentials.uid;
    info.gid          = credentials.gid;
    info.effectiveUid = credentials.effectiveUid;
    info.effectiveGid = credentials.effectiveGid;
    info.parentId     = target.parentId();
    info.serviceRole  = static_cast<uint32_t>(target.serviceRole());
    info.taskGroupId  = target.taskGroupId();
    info.sessionId    = target.sessionId();
    info.terminalForegroundGroup =
        task::terminalForeground(target.terminalKind(), target.terminalIndex());
    info.startedNanoseconds = target.startedNanoseconds();
    info.cpuNanoseconds     = target.cpuNanoseconds();
    info.terminalKind       = target.terminalKind();
    info.terminalIndex      = target.terminalIndex();
    info.commandLineSize =
        caller.effectiveUid() == 0 ||
                caller.effectiveUid() == target.effectiveUid()
            ? target.commandLineSize()
            : 0;
    info.managedService = target.managedService();
    info.mainThreadId   = target.mainThreadId();
    info.nice           = target.mainNice();
    info.threadCount    = target.threadCount();
    for (size_t i = 0; i < info.commandLineSize; ++i)
        info.commandLine[i] = target.commandLine()[i];
    info.trustedDeviceService =
        target.hasCapability(taskCapabilityDevice) &&
        (target.serviceRole() == os::ServiceRole::Device ||
         target.serviceRole() == os::ServiceRole::Storage);
    for (size_t i = 0; i + 1 < sizeof(info.name) && target.name()[i]; ++i)
        info.name[i] = target.name()[i];
    copyMessageData(&reply.data[1], &info, sizeof(info));
}
// Run as the real calling thread, never as an impersonating service worker.
// Some operations modify this thread's saved context. Check each variable
// payload before reading it, even after the common minimum-length check.
int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
    // This endpoint runs inline in the authenticated calling thread, never in
    // a server worker impersonating the sender by changing CPU current.
    Thread* thread = currentThread();
    if (!thread || thread->getTask() != &caller || request.length < 4)
        return failure(os::ErrorCode::InvalidArgument);
    const auto* args = request.data;
    // Recovery has a separate owner/epoch/sequence protocol after this shared
    // entry. An opcode is not evidence of service-instance authorization.
    if (request.type >=
            static_cast<uint64_t>(os::TaskOperation::RecoveryAcquire) &&
        request.type <= static_cast<uint64_t>(os::TaskOperation::RecoveryList))
        return serviceRecovery().call(caller, request, reply);
    if (request.type ==
            static_cast<uint64_t>(os::TaskOperation::RecoveryAppend) ||
        request.type ==
            static_cast<uint64_t>(os::TaskOperation::RecoveryReadLog) ||
        request.type ==
            static_cast<uint64_t>(os::TaskOperation::RecoveryGeneration))
        return serviceRecovery().call(caller, request, reply);
    switch (static_cast<os::TaskOperation>(request.type)) {
        // Current-context mechanisms execute inline. Exit does not return;
        // exec success installs a new continuation and bypasses the obsolete
        // reply area.
        case os::TaskOperation::ExitTask:
            task::exitCurrent(static_cast<int>(args[0]));
        case os::TaskOperation::ExitThread:
            exitCurrentThread(static_cast<int>(args[0]));
        case os::TaskOperation::CreateThread:
            reply.data[0] =
                TaskMechanisms(caller).createThread(args[0], args[1], args[2]);
            break;
        case os::TaskOperation::Yield:
            task::yieldThread();
            break;
        case os::TaskOperation::Sleep:
            sleepCurrentThread(args[0]);
            break;
        case os::TaskOperation::StandardHandle:
            reply.data[0] =
                caller.standardHandle(static_cast<uint32_t>(args[0]));
            break;
        case os::TaskOperation::Fork:
            reply.data[0] = TaskMechanisms(caller).fork();
            break;
        case os::TaskOperation::ReplaceImage: {
            uint64_t failureResult = 0;
            if (!TaskMechanisms(caller).replace(args[0], failureResult))
                reply.data[0] = failureResult;
            break;
        }

        // Query uses suboperation zero; changes apply to caller credentials
        // only. Validate a complete supplemental-group vector before changing
        // any IDs.
        case os::TaskOperation::Credentials: {
            if (args[0] > 9) {
                reply.data[1] = 22;
                break;
            }
            if (!args[0]) {
                auto target = task::find(args[1] ? args[1] : caller.id());
                if (!target) return failure(os::ErrorCode::NotFound);
                if (target != &caller && caller.effectiveUid() &&
                    caller.serviceRole() != os::ServiceRole::Fs &&
                    caller.serviceRole() != os::ServiceRole::Users)
                    return failure(os::ErrorCode::PermissionDenied);
                os::wire::CredentialInfo info{};
                target->credentials(info);
                copyMessageData(&reply.data[2], &info, sizeof(info));
            } else {
                if (args[1] > UINT32_MAX || args[2] > UINT32_MAX ||
                    args[3] > UINT32_MAX) {
                    reply.data[1] = 22;
                    break;
                }
                uint32_t groups[os::maximumSupplementaryGroups]{};
                if (args[0] == 9) {
                    if (args[1] > os::maximumSupplementaryGroups ||
                        request.length < 4 + args[1]) {
                        reply.data[1] = 22;
                        break;
                    }
                    bool valid = true;
                    for (uint32_t i = 0; i < args[1]; ++i) {
                        if (args[4 + i] >= UINT32_MAX) valid = false;
                        groups[i] = args[4 + i];
                    }
                    if (!valid) {
                        reply.data[1] = 22;
                        break;
                    }
                }
                reply.data[1] = caller.changeCredentials(
                    args[0], args[1], args[2], args[3], groups);
            }
            break;
        }
        // Use minimum TID rather than an unstable linked-list position to
        // enumerate.
        case os::TaskOperation::ThreadInfo: {
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target) return failure(os::ErrorCode::NotFound);
            os::wire::ThreadInfo info{};
            if (!target->threadInfo(args[1], info))
                return failure(os::ErrorCode::NotFound);
            copyMessageData(&reply.data[1], &info, sizeof(info));
            break;
        }
        // An RPC can be delivered successfully while its POSIX business
        // operation reports errno. Keep that status separate from the
        // resulting nice value.
        case os::TaskOperation::Priority: {
            if (args[0] > 2 || args[1] > 2) {
                reply.data[1] = 22;
                break;
            }
            int result = 0;
            reply.data[1] =
                applyTaskPriority(caller, *thread, args[0], args[1], args[2],
                                  static_cast<int>(args[3]), result);
            reply.data[2] = static_cast<int64_t>(result);
            break;
        }
        // Retain the target, then let its resourceLimit method check identity
        // and soft/hard ceilings under the target state lock.
        case os::TaskOperation::ResourceLimit: {
            if (request.length < 5 || args[2] > 1 || args[1] >= 16)
                return failure(os::ErrorCode::InvalidArgument);
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target) {
                reply.data[1] = 3;
                break;
            }
            uint64_t soft = args[3], hard = args[4];
            reply.data[1] = target->resourceLimit(
                caller, static_cast<int>(args[1]), args[2], soft, hard);
            reply.data[2] = soft;
            reply.data[3] = hard;
            break;
        }
        // Authenticated provenance differs from mutable process IDs.
        // Cross-process reads require Users authority or an approved managed
        // identity observer.
        case os::TaskOperation::Authentication: {
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target) return failure(os::ErrorCode::NotFound);
            const bool identityObserver =
                caller.managedService() &&
                caller.hasCapability(taskCapabilityReadIdentity);
            if (target != &caller && !identityObserver &&
                (caller.serviceRole() != os::ServiceRole::Users ||
                 !caller.hasCapability(taskCapabilityUsers)))
                return failure(os::ErrorCode::PermissionDenied);
            os::SessionIdentityInfo identity{};
            target->authentication(identity.session, identity.uid,
                                   identity.gid);
            copyMessageData(&reply.data[1], &identity, sizeof(identity));
            break;
        }
        // Only the Users role with Users capability can authenticate a target.
        // Validate every supplemental group before committing the identity
        // update.
        case os::TaskOperation::AuthenticateSession: {
            if (caller.serviceRole() != os::ServiceRole::Users ||
                !caller.hasCapability(taskCapabilityUsers))
                return failure(os::ErrorCode::PermissionDenied);
            auto target          = task::find(args[0]);
            const uint64_t count = request.length >= 5 ? args[4] : 0;
            if (count > os::maximumSupplementaryGroups ||
                request.length < (count ? 5 + count : 4))
                return failure(os::ErrorCode::InvalidArgument);
            uint32_t groups[os::maximumSupplementaryGroups]{};
            for (uint32_t i = 0; i < count; ++i) {
                if (args[5 + i] >= UINT32_MAX)
                    return failure(os::ErrorCode::InvalidArgument);
                groups[i] = args[5 + i];
            }
            if (!target || args[1] >= 65535 || args[2] >= 65535 ||
                args[3] > 1 ||
                !target->authenticate(args[1], args[2], args[3], count, groups))
                return failure(os::ErrorCode::InvalidArgument);
            break;
        }
        // Advertise created Task handles in the reply vector. Clients must not
        // interpret a successful delivery status as the created resource
        // number.
        case os::TaskOperation::SpawnProgram:
        case os::TaskOperation::SpawnImage: {
            TaskImages images(caller);
            reply.data[0] =
                request.type ==
                        static_cast<uint64_t>(os::TaskOperation::SpawnProgram)
                    ? images.spawnProgram(args[0], args[1], args[2], args[3])
                    : images.spawnImage(args[0]);
            if (static_cast<int64_t>(reply.data[0]) >= 0)
                addCreatedHandle(reply, reply.data[0]);
            break;
        }
        // Wait methods inspect/register under condition locks; this loop
        // blocks only after those locks are gone and rechecks after wake. A
        // notification alone does not reserve a particular child or thread
        // event.
        case os::TaskOperation::WaitChild:
        case os::TaskOperation::WaitId:
        case os::TaskOperation::WaitThread: {
            const bool waitId = request.type == static_cast<uint64_t>(
                                                    os::TaskOperation::WaitId);
            const bool child =
                request.type !=
                static_cast<uint64_t>(os::TaskOperation::WaitThread);
            // Reject unknown wait bits and INT64_MIN before negating a group
            // selector. waitid additionally requires at least one selected
            // event class.
            const uint64_t allowed = waitId ? 0x0100000fULL : 11ULL;
            if ((child &&
                 ((args[1] & ~allowed) || (waitId && !(args[1] & 14U)))) ||
                (!child && args[1] > 1) ||
                static_cast<int64_t>(args[0]) == INT64_MIN)
                return failure(os::ErrorCode::InvalidArgument);
            const uint32_t options = args[1] | (waitId ? 0x10000000U : 0U);
            os::wire::ChildEvent event{};
            while (true) {
                int status  = 0;
                uint64_t id = 0;
                const int result =
                    child ? caller.waitForChild(static_cast<int64_t>(args[0]),
                                                thread, status, options, id,
                                                waitId ? &event : nullptr)
                          : caller.waitForThread(args[0], thread, status,
                                                 args[1]);
                if (result < 0) return failure(os::ErrorCode::NotFound);
                if (result == 2) {
                    if (!child) return failure(os::ErrorCode::WouldBlock);
                    break;
                }
                if (result == 0) {
                    reply.data[0] = child ? id : 0;
                    reply.data[1] = static_cast<int64_t>(status);
                    break;
                }
                blockWaitingThread();
                if (const int error = thread->waitError()) {
                    // SIGCHLD and the wait event can wake the same blocked
                    // RPC. Prefer an already completed child event before
                    // reporting EINTR.
                    if (child &&
                        error == static_cast<int>(os::ErrorCode::Interrupted) &&
                        caller.waitForChild(static_cast<int64_t>(args[0]),
                                            thread, status, options | 1U, id,
                                            waitId ? &event : nullptr) == 0) {
                        reply.data[0] = id;
                        reply.data[1] = static_cast<int64_t>(status);
                        break;
                    }
                    return error;
                }
            }
            if (waitId) {
                reply.data[2] = event.uid;
                reply.data[3] = event.code;
                reply.data[4] = static_cast<int64_t>(event.status);
            }
            break;
        }
        // Read rights are broad; manage rights require root, self or direct
        // parent. Later control operations still impose supervised-service
        // restrictions.
        case os::TaskOperation::OpenTask: {
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target || target == task::getIdle())
                return failure(os::ErrorCode::NotFound);
            uint32_t rights = handleRightRead;
            if (caller.effectiveUid() == 0 ||
                target->parentId() == caller.id() || target == &caller)
                rights |= handleRightManage;
            const uint64_t handle =
                caller.handles().add(target, HandleType::Task, rights);
            if (!handle) return failure(os::ErrorCode::OutOfMemory);
            addCreatedHandle(reply, handle);
            break;
        }
        case os::TaskOperation::CorePolicyQuery: {
            os::wire::CorePolicy policy{};
            coreDump().query(policy);
            copyMessageData(&reply.data[1], &policy, sizeof(policy));
            break;
        }
        // Pin for the whole query/control path. Normal termination excludes
        // managed services; control requires Init or an authorized direct
        // supervising parent.
        case os::TaskOperation::State:
        case os::TaskOperation::Info:
        case os::TaskOperation::Terminate:
        case os::TaskOperation::ControlSignal:
        case os::TaskOperation::ControlTerminate: {
            const bool controlled =
                request.type ==
                    static_cast<uint64_t>(os::TaskOperation::ControlSignal) ||
                request.type ==
                    static_cast<uint64_t>(os::TaskOperation::ControlTerminate);
            const bool terminate =
                controlled || request.type == static_cast<uint64_t>(
                                                  os::TaskOperation::Terminate);
            HandleTable::Pin pin(
                caller.handles(), args[0], HandleType::Task,
                terminate ? handleRightManage : handleRightRead);
            Task* target = pin.as<Task>();
            if (!target) return failure(os::ErrorCode::InvalidHandle);
            if (terminate) {
                if (target == &caller || target == task::getIdle())
                    return failure(os::ErrorCode::InvalidHandle);
                if (controlled) {
                    if (!target->managedService() ||
                        (caller.serviceRole() != os::ServiceRole::Init &&
                         (target->parentId() != caller.id() ||
                          caller.serviceRole() == os::ServiceRole::None)))
                        return failure(os::ErrorCode::PermissionDenied);
                    if (request.type == static_cast<uint64_t>(
                                            os::TaskOperation::ControlSignal)) {
                        if (args[1] != 15 || !target->deliverSignal(15))
                            return failure(os::ErrorCode::InvalidArgument);
                        break;
                    }
                } else if (target->managedService())
                    return failure(os::ErrorCode::PermissionDenied);
                target->terminateOtherThreads(static_cast<int>(args[1]));
                target->destroy();
            } else if (request.type ==
                       static_cast<uint64_t>(os::TaskOperation::Info))
                describe(*target, caller, reply);
            else {
                reply.data[1] = static_cast<int64_t>(target->exitCode());
                reply.data[0] =
                    target->state() == TaskState::Stopped ? 2
                    : target->isTerminated() && !coreDump().pending(*target)
                        ? 1
                        : 0;
                if (reply.data[0] == 1) target->destroy();
            }
            break;
        }
        case os::TaskOperation::Identity:
            reply.data[0] = caller.id();
            break;
        case os::TaskOperation::ThreadIdentity:
            reply.data[0] = thread->getId();
            break;
        // Restore the current syscall frame and mark it restored so normal
        // result assignment and copying into the old reply buffer cannot
        // overwrite it.
        case os::TaskOperation::SignalReturn:
            if (thread->getType() != ThreadType::User ||
                !thread->syscallContext() ||
                !task::restoreSignalFrame(thread->syscallContext(), args[0]))
                task::fatal(11);
            thread->markContextRestored();
            break;
        // An active alternate stack cannot be replaced underneath its handler.
        // Check bounds now; delivery later confirms writable backing while
        // copying.
        case os::TaskOperation::SignalStack: {
            const auto& old     = thread->signalStack;
            const uint64_t rsp  = thread->syscallContext()
                                      ? thread->syscallContext()->userRsp
                                      : 0;
            const uint64_t base = reinterpret_cast<uint64_t>(old.pointer);
            const bool active =
                old.size && rsp >= base && rsp - base < old.size;
            OsSignalStack previous = old;
            previous.flags         = old.size ? (active ? 1 : 0) : 2;
            if (args[0]) {
                if (active) return failure(os::ErrorCode::PermissionDenied);
                OsSignalStack value{};
                copyMessageData(&value, &args[1], sizeof(value));
                const uint64_t address =
                    reinterpret_cast<uint64_t>(value.pointer);
                if (value.flags != 0 && value.flags != 2)
                    return failure(os::ErrorCode::InvalidArgument);
                if (value.flags == 2)
                    value = {};
                else if (value.size < 2048 || address < 0x10000 ||
                         address >= 0x800000000000ULL ||
                         value.size > 0x800000000000ULL - address)
                    return failure(os::ErrorCode::InvalidArgument);
                thread->signalStack = value;
            }
            copyMessageData(&reply.data[1], &previous, sizeof(previous));
            break;
        }
        // Return prior per-task limits while applying any authorized update;
        // global writer policy is a different request.
        case os::TaskOperation::CoreLimit:
            reply.data[1] = caller.coreLimit();
            reply.data[2] = caller.coreHardLimit();
            if (args[0] && !caller.setCoreLimit(args[1], args[2]))
                return failure(os::ErrorCode::PermissionDenied);
            break;
        // Fault registers/addresses can disclose data. Check relationship,
        // identity and dumpability or explicit authority before returning
        // diagnostics.
        case os::TaskOperation::ExitInfo: {
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target ||
                (target != &caller && caller.effectiveUid() &&
                 !caller.hasCapability(taskCapabilitySpawn) &&
                 (target->parentId() != caller.id() || !target->dumpable() ||
                  target->effectiveUid() != caller.effectiveUid())))
                return failure(os::ErrorCode::PermissionDenied);
            os::wire::ExitInfo info{};
            target->getExitInfo(info);
            copyMessageData(&reply.data[1], &info, sizeof(info));
            break;
        }
        // Record only a bounded mapped user interval and terminated path for
        // NT_FILE. Metadata registration does not itself create or authorize a
        // mapping.
        case os::TaskOperation::ImageMapping: {
            os::wire::ImageMapping image{};
            if (request.length < sizeof(image) / 8)
                return failure(os::ErrorCode::InvalidArgument);
            copyMessageData(&image, args, sizeof(image));
            if (image.begin < 0x10000 || image.begin >= image.end ||
                image.end > 0x800000000000ULL ||
                ((image.begin | image.end | image.offset) & 4095) ||
                !image.path[0] || image.path[sizeof(image.path) - 1] ||
                !mm::validateUserMemory(
                    caller.addressSpace().rootPhysicalAddress(), image.begin, 1,
                    false) ||
                !caller.recordImage(image))
                return failure(os::ErrorCode::InvalidArgument);
            break;
        }
        // Prepare legitimate lazy memory in the current task, preserving
        // write/COW checks instead of offering an arbitrary physical-memory
        // copy primitive.
        case os::TaskOperation::ProbeMemory: {
            if (args[2] > 1) return failure(os::ErrorCode::InvalidArgument);
            if (caller.isSystemService() && args[0] >= 0xffff800000000000ULL) {
                if (!mm::validateUserMemory(
                        caller.addressSpace().rootPhysicalAddress(), args[0],
                        args[1], args[2]))
                    return failure(os::ErrorCode::BadAddress);
            } else if (!mm::pagerBridge().ensureUserMemory(args[0], args[1],
                                                           args[2]))
                return failure(os::ErrorCode::BadAddress);
            break;
        }
        // Delivery generation distinguishes a real installed handler from a
        // wake.
        case os::TaskOperation::SignalGeneration:
            reply.data[1] = thread->signalGeneration;
            break;
        // Bound program headers before preserving debugger auxiliary metadata.
        case os::TaskOperation::Auxiliary:
            if (!args[1] || args[1] > 128 || args[2] < 0x10000 ||
                args[2] >= 0x800000000000ULL || args[3] >= 0x800000000000ULL ||
                !mm::validateUserMemory(
                    caller.addressSpace().rootPhysicalAddress(), args[0],
                    args[1] * 56, false))
                return failure(os::ErrorCode::InvalidArgument);
            caller.recordAuxiliary(args[0], args[1], args[2], args[3]);
            break;
        // CoreDump authenticates the fixed Task writer role. Its snapshot is
        // already separate from the dying address space, allowing later
        // user-space disk I/O.
        case os::TaskOperation::CoreConfigure: {
            os::wire::CorePolicy policy{};
            copyMessageData(&policy, args, sizeof(policy));
            const int result = coreDump().configure(caller, policy);
            if (result) return result;
            break;
        }
        case os::TaskOperation::CoreAcquire: {
            os::wire::CoreTicket ticket{};
            const int result = coreDump().acquire(caller, ticket);
            if (result) return result;
            copyMessageData(&reply.data[1], &ticket, sizeof(ticket));
            break;
        }
        // Validate the ticket into a bounded kernel scratch chunk, then copy
        // with normal user-memory checks. A ticket cannot read arbitrary Task
        // memory.
        case os::TaskOperation::CoreRead: {
            if (caller.serviceRole() != os::ServiceRole::Task)
                return failure(os::ErrorCode::PermissionDenied);
            uint8_t buffer[4096];
            if (!args[3] || args[2] > sizeof(buffer))
                return failure(os::ErrorCode::InvalidArgument);
            const int result =
                coreDump().read(caller, args[0], args[1], buffer, args[2]);
            if (result < 0) return result;
            if (!task::copyToUser(reinterpret_cast<void*>(args[3]), buffer,
                                  result))
                return failure(os::ErrorCode::InvalidArgument);
            reply.data[0] = result;
            break;
        }
        case os::TaskOperation::CoreFinish: {
            const int result = coreDump().finish(caller, args[0], args[1]);
            if (result) return result;
            break;
        }
        // TaskReference retains before list unlock. Minimum-PID enumeration
        // avoids assuming a linked position still names the same task on the
        // next request.
        case os::TaskOperation::Query:
        case os::TaskOperation::Enumerate: {
            auto target =
                request.type == static_cast<uint64_t>(os::TaskOperation::Query)
                    ? task::find(args[0] ? args[0] : caller.id())
                : args[1] == 1          ? task::next(args[0])
                : args[0] <= UINT32_MAX ? task::at(args[0])
                                        : TaskReference();
            if (!target) return failure(os::ErrorCode::NotFound);
            describe(*target, caller, reply);
            break;
        }
        // Authorize here: the low-level setter assumes its caller already
        // checked permissions and does not independently authenticate the
        // requester.
        case os::TaskOperation::SetCredentials: {
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target || args[1] >= 65535 || args[2] >= 65535 || args[3] > 1)
                return failure(os::ErrorCode::InvalidArgument);
            if (!caller.hasCapability(taskCapabilityUsers) &&
                (&caller != target ||
                 (caller.effectiveUid() != 0 &&
                  (args[1] != caller.uid() || args[2] != caller.gid()))))
                return failure(os::ErrorCode::PermissionDenied);
            target->setCredentials(args[1], args[2], args[3]);
            break;
        }
        // Console and PTY slots are controlled by their matching fixed service
        // roles. Foreground groups do not describe GUI windows or pixel
        // geometry.
        case os::TaskOperation::SetTerminalForeground:
            if ((args[0] == 1 &&
                 caller.serviceRole() != os::ServiceRole::Console) ||
                (args[0] == 2 &&
                 caller.serviceRole() != os::ServiceRole::Tty) ||
                (args[0] != 1 && args[0] != 2))
                return failure(os::ErrorCode::PermissionDenied);
            if (!task::setTerminalForeground(args[0], args[1], args[2]))
                return failure(os::ErrorCode::InvalidArgument);
            break;
        // Use real caller identity and PID/group selectors. Managed service
        // control has its own path; signal zero probes existence, and -1
        // broadcast is absent.
        case os::TaskOperation::SendSignal: {
            const int64_t target = static_cast<int64_t>(args[0]);
            if (args[1] > maximumSignal || target == INT64_MIN)
                return failure(os::ErrorCode::InvalidArgument);
            if (target > 0 && args[1]) {
                auto process = task::find(target);
                if (process && process->managedService())
                    return failure(os::ErrorCode::PermissionDenied);
            }
            const bool delivered =
                target > 0    ? task::sendSignalToTask(target, args[1], &caller)
                : target == 0 ? task::sendSignalToTaskGroup(
                                    caller.taskGroupId(), args[1], &caller)
                              : target < -1 && task::sendSignalToTaskGroup(
                                                   -target, args[1], &caller);
            if (!delivered) return failure(os::ErrorCode::NotFound);
            break;
        }
        // Thread-directed delivery only searches the caller's own thread list.
        case os::TaskOperation::SendThreadSignal:
            if (args[1] > maximumSignal)
                return failure(os::ErrorCode::InvalidArgument);
            if (!caller.deliverThreadSignal(args[0], args[1]))
                return failure(os::ErrorCode::NotFound);
            break;
        // Task rejects an existing group leader before changing SID/PGID and
        // terminal/authentication associations.
        case os::TaskOperation::CreateSession:
            if (!caller.createSession())
                return failure(os::ErrorCode::PermissionDenied);
            reply.data[0] = caller.sessionId();
            break;
        // Return parent/group/session numbers rather than object handles.
        case os::TaskOperation::Identifiers: {
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target) return failure(os::ErrorCode::NotFound);
            os::wire::TaskIdentifiers ids{
                target->parentId(), target->taskGroupId(), target->sessionId()};
            copyMessageData(&reply.data[1], &ids, sizeof(ids));
            break;
        }
        // A parent can target a direct child, but Task still checks
        // same-session membership for the requested group.
        case os::TaskOperation::SetGroup: {
            auto target = task::find(args[1] ? args[1] : caller.id());
            if (!target ||
                (target != &caller && target->parentId() != caller.id()))
                return failure(os::ErrorCode::NotFound);
            if (!target->setTaskGroup(args[0]))
                return failure(os::ErrorCode::PermissionDenied);
            break;
        }
        // Validate supported flags and handler/restorer mappings now. Delivery
        // also requires executable user pages, so registration is not a
        // permanent waiver of later execute-permission checks.
        case os::TaskOperation::SignalAction: {
            if (args[0] > maximumSignal || !task::isValidSignal(args[0]) ||
                args[1] > 1)
                return failure(os::ErrorCode::InvalidArgument);
            SignalAction action{}, old{};
            if (args[1]) {
                if (request.length < 8)
                    return failure(os::ErrorCode::InvalidArgument);
                copyMessageData(&action, &args[4], sizeof(action));
                if ((action.getFlags() &
                     ~(signalFlagNoDefer | signalFlagResetHand |
                       signalFlagOnStack | 0x00000004ULL | 0x10000000ULL)) ||
                    (action.getHandler() > signalIgnore &&
                     (!action.getRestorer() ||
                      !mm::validateUserMemory(
                          caller.addressSpace().rootPhysicalAddress(),
                          action.getHandler(), 1, false) ||
                      !mm::validateUserMemory(
                          caller.addressSpace().rootPhysicalAddress(),
                          action.getRestorer(), 1, false))) ||
                    !caller.setSignalAction(args[0], action, &old))
                    return failure(os::ErrorCode::InvalidArgument);
            } else if (!caller.getSignalAction(args[0], old))
                return failure(os::ErrorCode::InvalidArgument);
            copyMessageData(&reply.data[1], &old, sizeof(old));
            break;
        }
        // The mask belongs to this thread; always clear Kill/Stop bits
        // regardless of whether the operation adds, removes or replaces the
        // remaining mask.
        case os::TaskOperation::SignalMask: {
            if (args[0] > 2) return failure(os::ErrorCode::InvalidArgument);
            const uint64_t old = thread->getSignalMask();
            const uint64_t mask =
                args[1] &
                ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
                  (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
            thread->setSignalMask(args[0] == 0   ? old | mask
                                  : args[0] == 1 ? old & ~mask
                                                 : mask);
            reply.data[1] = old;
            break;
        }
        case os::TaskOperation::PendingSignals:
            reply.data[1] = caller.pendingSignals(thread);
            break;
        // Explicit synchronous selection can consume accepted pending bits
        // despite the usual delivery mask; it does not invoke a user handler
        // in this call.
        case os::TaskOperation::TakeSignal: {
            if (!args[0]) return failure(os::ErrorCode::InvalidArgument);
            SignalAction action{};
            const uint32_t value =
                caller.takePendingSignal(thread, args[0], action, false);
            if (!value) return failure(os::ErrorCode::WouldBlock);
            reply.data[0] = value;
            break;
        }
        default:
            return failure(os::ErrorCode::InvalidArgument);
    }
    reply.length = os::ipcDataWordCount;
    return 0;
}
}  // namespace TaskService

}  // namespace
// Bind concrete dispatch. Task/Thread own per-process state, not a shared
// service object pretending to store a single current client.
EndpointService& taskKernelService() {
    static EndpointService service(TaskService::call);
    return service;
}

}  // namespace kernel::task
