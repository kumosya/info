#include "service/common/records.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "kernel/ipc.h"
#include "kernel/task.h"
#include "service/users/identity.h"
#include <os/device.h>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/time.h"
#include "kernel/new.h"

namespace kernel {
namespace {
class TaskImages {
    Task& caller_;
    static uint64_t error(SyscallError value) { return static_cast<uint64_t>(static_cast<int64_t>(value)); }
    bool copyFromUser(void* destination, const void* source, size_t size) {
        const auto address = reinterpret_cast<uint64_t>(source);
        if (!destination || !size || address < 0x10000 || address + size < address || (address + size > 0x0000800000000000ULL && !(caller_.isSystemService() && address >= 0xffff800000000000ULL))) return false;
        PreemptionSpinLockGuard guard(caller_.addressSpace().lock());
        const auto root = caller_.addressSpace().rootPhysicalAddress();
        if (!memory::validateUserMemory(root, address, size, false)) return false;
        if (address >= 0xffff800000000000ULL && caller_.isSystemService()) {
            MessageData::copy(destination, source, size);
            return true;
        }
        return memory::copyFromAddressSpace(destination, root, address, size);
    }
    static uint64_t elfLoadError(ElfLoadError loadError) {
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
   public:
    explicit TaskImages(Task& caller) : caller_(caller) {}
    uint64_t spawnProgram(uint64_t program, uint64_t bootstrapHandle, uint64_t argumentCount, uint64_t userArguments) {

    constexpr uint32_t maximumArgumentCount  = 64;
    constexpr uint32_t maximumArgumentLength = 383;
    Task* parent = &caller_;
    const auto requested = static_cast<os::ProgramId>(program);
    const bool permitted = parent && (parent->serviceRole() == os::ServiceRole::Init ||
        (parent->serviceRole() == os::ServiceRole::Task && (requested == os::ProgramId::PipeServer || requested == os::ProgramId::MessageQueueServer)) ||
        (parent->serviceRole() == os::ServiceRole::Device && (requested == os::ProgramId::PciServer || requested == os::ProgramId::StorageServer)));
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
    auto child = UserProgramLoader::load(
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
    uint64_t spawnImage(uint64_t userInfo) {

    constexpr uint32_t maximumArgumentCount    = 64;
    constexpr uint32_t maximumArgumentLength   = 383;
    constexpr uint32_t maximumNameLength       = 63;
    constexpr uint32_t maximumSharedImageCount = 4;
    constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
    Task* parent = &caller_;
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
        info.capabilities != 0 || info.terminalKind > 2 || (info.flags & ~(os::spawnFlagNewSession | os::spawnFlagManagedService)) ||
        !memory::validateUserMemory(
            parent->addressSpace().rootPhysicalAddress(),
            reinterpret_cast<uint64_t>(info.image), info.imageSize, false)) {
        return error(SyscallError::InvalidArgument);
    }

    os::wire::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
    ElfSharedImage sharedImages[maximumSharedImageCount]{};
    if (info.sharedImageCount != 0 &&
        !copyFromUser(userSharedImages, info.sharedImages,
                      info.sharedImageCount * sizeof(os::wire::SharedImageInfo))) {
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

    auto logicalParentReference = task::find(info.parentTaskId ? info.parentTaskId : parent->id());
    Task* logicalParent = logicalParentReference;
    if (info.parentTaskId != 0) {
        if (logicalParent == nullptr || logicalParent == task::getIdle() ||
            logicalParent->isTerminated()) {
            return error(SyscallError::InvalidArgument);
        }
    }
    uint64_t serviceCapabilities = 0;
    const bool managed = info.flags & os::spawnFlagManagedService;
    if ((managed || info.serviceRole) && (parent->serviceRole() != os::ServiceRole::Task ||
        !logicalParent || logicalParent->serviceRole() != os::ServiceRole::Init || !managed))
        return error(SyscallError::PermissionDenied);
    if (info.serviceRole && !UserProgramLoader::serviceCapabilities(
            static_cast<os::ServiceRole>(info.serviceRole), serviceCapabilities))
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
    auto child = UserProgramLoader::loadImage(
        reinterpret_cast<uint64_t>(info.image), info.imageSize, name, parent,
        logicalParent, info.bootstrapHandle, serviceCapabilities,
        info.standardInputHandle, info.standardOutputHandle,
        info.standardErrorHandle, info.argumentCount, arguments, sharedImages,
        info.sharedImageCount, &loadError, static_cast<os::ServiceRole>(info.serviceRole), (info.flags & os::spawnFlagNewSession) != 0,
        managed, info.terminalKind, info.terminalIndex);
    if (child == nullptr) {
        serial::serialWrite("[ELF] spawn image failed: ");
        serial::serialWrite(ElfLoader::errorName(loadError));
        serial::serialWrite("\n");
        return elfLoadError(loadError);
    }
    const uint64_t handle = parent->handles().add(
        child, HandleType::Task, handleRightRead | handleRightManage);
    return handle == 0 ? error(SyscallError::OutOfMemory) : handle;

    }
};

class TaskService final : public EndpointService {
    static int failure(os::ErrorCode code) { return static_cast<int>(code); }
    static void describe(Task& target, Task& caller, IpcMessage& reply) {
        os::wire::TaskInfo info{};
        os::wire::CredentialInfo credentials{}; target.credentials(credentials);
        info.id = target.id(); info.state = static_cast<uint32_t>(target.state());
        info.exitCode = target.exitCode(); info.uid = credentials.uid; info.gid = credentials.gid;
        info.effectiveUid = credentials.effectiveUid; info.effectiveGid = credentials.effectiveGid;
        info.parentId = target.parentId(); info.serviceRole = static_cast<uint32_t>(target.serviceRole());
        info.taskGroupId = target.taskGroupId(); info.sessionId = target.sessionId();
        info.terminalForegroundGroup = task::terminalForeground(target.terminalKind(), target.terminalIndex());
        info.startedNanoseconds = target.startedNanoseconds(); info.cpuNanoseconds = target.cpuNanoseconds();
        info.terminalKind = target.terminalKind(); info.terminalIndex = target.terminalIndex();
        info.commandLineSize = caller.effectiveUid() == 0 || caller.effectiveUid() == target.effectiveUid() ? target.commandLineSize() : 0;
        info.managedService = target.managedService();
        info.mainThreadId = target.mainThreadId(); info.nice = target.mainNice();
        info.threadCount = target.threadCount();
        for (size_t i = 0; i < info.commandLineSize; ++i)
            info.commandLine[i] = target.commandLine()[i];
        info.trustedDeviceService = target.hasCapability(taskCapabilityDevice) && (target.serviceRole() == os::ServiceRole::Device || target.serviceRole() == os::ServiceRole::Storage);
        for (size_t i = 0; i + 1 < sizeof(info.name) && target.name()[i]; ++i) info.name[i] = target.name()[i];
        MessageData::copy(&reply.data[1], &info, sizeof(info));
    }
   public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        // This endpoint runs inline in the authenticated calling thread, never
        // in a server worker impersonating the sender by changing CPU current.
        Thread* thread = Thread::getCurrent();
        if (!thread || thread->getTask() != &caller || request.length < 4) return failure(os::ErrorCode::InvalidArgument);
        const auto* args = request.data;
        if (request.type >= static_cast<uint64_t>(os::TaskOperation::RecoveryAcquire) &&
            request.type <= static_cast<uint64_t>(os::TaskOperation::RecoveryList)) return ServiceRecovery::call(caller, request, reply);
        if (request.type == static_cast<uint64_t>(os::TaskOperation::RecoveryAppend) ||
            request.type == static_cast<uint64_t>(os::TaskOperation::RecoveryReadLog) ||
            request.type == static_cast<uint64_t>(os::TaskOperation::RecoveryGeneration)) return ServiceRecovery::call(caller, request, reply);
        switch (static_cast<os::TaskOperation>(request.type)) {
            case os::TaskOperation::ExitTask: task::exitCurrent(static_cast<int>(args[0]));
            case os::TaskOperation::ExitThread: Thread::exitCurrent(static_cast<int>(args[0]));
            case os::TaskOperation::CreateThread:
                reply.data[0]=TaskMechanisms(caller).createThread(args[0],args[1],args[2]); break;
            case os::TaskOperation::Yield: schedule::yieldThread(); break;
            case os::TaskOperation::Sleep: Thread::sleepCurrent(args[0]); break;
            case os::TaskOperation::StandardHandle: reply.data[0]=caller.standardHandle(static_cast<uint32_t>(args[0])); break;
            case os::TaskOperation::Fork: reply.data[0]=TaskMechanisms(caller).fork(); break;
            case os::TaskOperation::ReplaceImage: {
                uint64_t failureResult=0;
                if (!TaskMechanisms(caller).replace(args[0],failureResult)) reply.data[0]=failureResult;
                break;
            }

            case os::TaskOperation::Credentials: {
                if (args[0] > 9) { reply.data[1] = 22; break; }
                if (!args[0]) {
                    auto target = task::find(args[1] ? args[1] : caller.id());
                    if (!target) return failure(os::ErrorCode::NotFound);
                    if (target != &caller && caller.effectiveUid() && caller.serviceRole() != os::ServiceRole::Fs && caller.serviceRole() != os::ServiceRole::Users)
                        return failure(os::ErrorCode::PermissionDenied);
                    os::wire::CredentialInfo info{}; target->credentials(info);
                    MessageData::copy(&reply.data[2], &info, sizeof(info));
                } else {
                    if (args[1] > UINT32_MAX || args[2] > UINT32_MAX || args[3] > UINT32_MAX) { reply.data[1] = 22; break; }
                    uint32_t groups[os::maximumSupplementaryGroups]{};
                    if (args[0] == 9) {
                        if (args[1] > os::maximumSupplementaryGroups || request.length < 4 + args[1]) { reply.data[1] = 22; break; }
                        bool valid = true;
                        for (uint32_t i = 0; i < args[1]; ++i) { if (args[4 + i] >= UINT32_MAX) valid = false; groups[i] = args[4 + i]; }
                        if (!valid) { reply.data[1] = 22; break; }
                    }
                    reply.data[1] = caller.changeCredentials(args[0], args[1], args[2], args[3], groups);
                }
                break;
            }
            case os::TaskOperation::ThreadInfo: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target) return failure(os::ErrorCode::NotFound);
                os::wire::ThreadInfo info{};
                if (!target->threadInfo(args[1], info)) return failure(os::ErrorCode::NotFound);
                MessageData::copy(&reply.data[1], &info, sizeof(info));
                break;
            }
            case os::TaskOperation::Priority: {
                if (args[0] > 2 || args[1] > 2) { reply.data[1] = 22; break; }
                int result = 0;
                reply.data[1] = TaskPriorities::apply(caller, *thread, args[0], args[1], args[2], static_cast<int>(args[3]), result);
                reply.data[2] = static_cast<int64_t>(result);
                break;
            }
            case os::TaskOperation::ResourceLimit: {
                if (request.length < 5 || args[2] > 1 || args[1] >= 16) return failure(os::ErrorCode::InvalidArgument);
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target) { reply.data[1] = 3; break; }
                uint64_t soft = args[3], hard = args[4];
                reply.data[1] = target->resourceLimit(caller, static_cast<int>(args[1]), args[2], soft, hard);
                reply.data[2] = soft; reply.data[3] = hard;
                break;
            }
            case os::TaskOperation::Authentication: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target) return failure(os::ErrorCode::NotFound);
                const bool identityObserver = caller.managedService() && caller.hasCapability(taskCapabilityReadIdentity);
                if (target != &caller && !identityObserver && (caller.serviceRole() != os::ServiceRole::Users || !caller.hasCapability(taskCapabilityUsers)))
                    return failure(os::ErrorCode::PermissionDenied);
                os::SessionIdentityInfo identity{};
                target->authentication(identity.session, identity.uid, identity.gid);
                MessageData::copy(&reply.data[1], &identity, sizeof(identity)); break;
            }
            case os::TaskOperation::AuthenticateSession: {
                if (caller.serviceRole() != os::ServiceRole::Users || !caller.hasCapability(taskCapabilityUsers)) return failure(os::ErrorCode::PermissionDenied);
                auto target = task::find(args[0]);
                const uint64_t count = request.length >= 5 ? args[4] : 0;
                if (count > os::maximumSupplementaryGroups || request.length < (count ? 5 + count : 4)) return failure(os::ErrorCode::InvalidArgument);
                uint32_t groups[os::maximumSupplementaryGroups]{};
                for (uint32_t i = 0; i < count; ++i) { if (args[5 + i] >= UINT32_MAX) return failure(os::ErrorCode::InvalidArgument); groups[i] = args[5 + i]; }
                if (!target || args[1] >= 65535 || args[2] >= 65535 || args[3] > 1 || !target->authenticate(args[1], args[2], args[3], count, groups))
                    return failure(os::ErrorCode::InvalidArgument);
                break;
            }
            case os::TaskOperation::SpawnProgram:
            case os::TaskOperation::SpawnImage: {
                TaskImages images(caller);
                reply.data[0] = request.type == static_cast<uint64_t>(os::TaskOperation::SpawnProgram)
                    ? images.spawnProgram(args[0], args[1], args[2], args[3]) : images.spawnImage(args[0]);
                if (static_cast<int64_t>(reply.data[0]) >= 0) MessageData::created(reply, reply.data[0]);
                break;
            }
            case os::TaskOperation::WaitChild:
            case os::TaskOperation::WaitId:
            case os::TaskOperation::WaitThread: {
                const bool waitId = request.type == static_cast<uint64_t>(os::TaskOperation::WaitId);
                const bool child = request.type != static_cast<uint64_t>(os::TaskOperation::WaitThread);
                const uint64_t allowed = waitId ? 0x0100000fULL : 11ULL;
                if ((child && ((args[1] & ~allowed) || (waitId && !(args[1] & 14U)))) || (!child && args[1] > 1) || static_cast<int64_t>(args[0]) == INT64_MIN)
                    return failure(os::ErrorCode::InvalidArgument);
                const uint32_t options = args[1] | (waitId ? 0x10000000U : 0U);
                os::wire::ChildEvent event{};
                while (true) {
                    int status = 0;
                    uint64_t id = 0;
                    const int result = child ? caller.waitForChild(static_cast<int64_t>(args[0]), thread, status, options, id, waitId ? &event : nullptr)
                        : caller.waitForThread(args[0], thread, status, args[1]);
                    if (result < 0) return failure(os::ErrorCode::NotFound);
                    if (result == 2) {
                        if (!child) return failure(os::ErrorCode::WouldBlock);
                        break;
                    }
                    if (result == 0) {
                        reply.data[0] = child ? id : 0;
                        reply.data[1] = static_cast<int64_t>(status); break;
                    }
                    Thread::blockCurrent();
                    if (const int error = thread->waitError()) {
                        // SIGCHLD and the wait event can wake the same blocked RPC.
                        // Prefer an already completed child event before reporting EINTR.
                        if (child && error == static_cast<int>(os::ErrorCode::Interrupted) &&
                            caller.waitForChild(static_cast<int64_t>(args[0]), thread, status, options | 1U, id, waitId ? &event : nullptr) == 0) {
                            reply.data[0] = id; reply.data[1] = static_cast<int64_t>(status); break;
                        }
                        return error;
                    }
                }
                if (waitId) { reply.data[2] = event.uid; reply.data[3] = event.code; reply.data[4] = static_cast<int64_t>(event.status); }
                break;
            }
            case os::TaskOperation::OpenTask: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target || target == task::getIdle()) return failure(os::ErrorCode::NotFound);
                uint32_t rights = handleRightRead;
                if (caller.effectiveUid() == 0 || target->parentId() == caller.id() || target == &caller)
                    rights |= handleRightManage;
                const uint64_t handle = caller.handles().add(target, HandleType::Task, rights);
                if (!handle) return failure(os::ErrorCode::OutOfMemory);
                MessageData::created(reply, handle); break;
            }
            case os::TaskOperation::CorePolicyQuery: {
                os::wire::CorePolicy policy{}; CoreDump::query(policy);
                MessageData::copy(&reply.data[1], &policy, sizeof(policy)); break;
            }
            case os::TaskOperation::State:
            case os::TaskOperation::Info:
            case os::TaskOperation::Terminate:
            case os::TaskOperation::ControlSignal:
            case os::TaskOperation::ControlTerminate: {
                const bool controlled = request.type == static_cast<uint64_t>(os::TaskOperation::ControlSignal) || request.type == static_cast<uint64_t>(os::TaskOperation::ControlTerminate);
                const bool terminate = controlled || request.type == static_cast<uint64_t>(os::TaskOperation::Terminate);
                HandleTable::Pin pin(caller.handles(), args[0], HandleType::Task,
                    terminate ? handleRightManage : handleRightRead);
                Task* target = pin.as<Task>();
                if (!target) return failure(os::ErrorCode::InvalidHandle);
                if (terminate) {
                    if (target == &caller || target == task::getIdle()) return failure(os::ErrorCode::InvalidHandle);
                    if (controlled) {
                        if (!target->managedService() || (caller.serviceRole() != os::ServiceRole::Init &&
                            (target->parentId() != caller.id() || caller.serviceRole() == os::ServiceRole::None)))
                            return failure(os::ErrorCode::PermissionDenied);
                        if (request.type == static_cast<uint64_t>(os::TaskOperation::ControlSignal)) {
                            if (args[1] != 15 || !target->deliverSignal(15)) return failure(os::ErrorCode::InvalidArgument);
                            break;
                        }
                    } else if (target->managedService()) return failure(os::ErrorCode::PermissionDenied);
                    target->terminateOtherThreads(static_cast<int>(args[1])); target->destroy();
                } else if (request.type == static_cast<uint64_t>(os::TaskOperation::Info)) describe(*target, caller, reply);
                else {
                    reply.data[1] = static_cast<int64_t>(target->exitCode());
                    reply.data[0] = target->state() == TaskState::Stopped ? 2 : target->isTerminated() && !CoreDump::pending(*target) ? 1 : 0;
                    if (reply.data[0] == 1) target->destroy();
                }
                break;
            }
            case os::TaskOperation::Identity: reply.data[0] = caller.id(); break;
            case os::TaskOperation::ThreadIdentity: reply.data[0] = thread->getId(); break;
            case os::TaskOperation::SignalReturn:
                if (thread->getType() != ThreadType::User || !thread->syscallContext() ||
                    !signal::restoreSignalFrame(thread->syscallContext(), args[0])) signal::fatal(11);
                thread->markContextRestored(); break;
            case os::TaskOperation::SignalStack: {
                const auto& old = thread->signalStack;
                const uint64_t rsp = thread->syscallContext() ? thread->syscallContext()->userRsp : 0;
                const uint64_t base = reinterpret_cast<uint64_t>(old.pointer);
                const bool active = old.size && rsp >= base && rsp - base < old.size;
                OsSignalStack previous = old; previous.flags = old.size ? (active ? 1 : 0) : 2;
                if (args[0]) {
                    if (active) return failure(os::ErrorCode::PermissionDenied);
                    OsSignalStack value{}; MessageData::copy(&value, &args[1], sizeof(value));
                    const uint64_t address = reinterpret_cast<uint64_t>(value.pointer);
                    if (value.flags != 0 && value.flags != 2) return failure(os::ErrorCode::InvalidArgument);
                    if (value.flags == 2) value = {};
                    else if (value.size < 2048 || address < 0x10000 || address >= 0x800000000000ULL || value.size > 0x800000000000ULL - address)
                        return failure(os::ErrorCode::InvalidArgument);
                    thread->signalStack = value;
                }
                MessageData::copy(&reply.data[1], &previous, sizeof(previous)); break;
            }
            case os::TaskOperation::CoreLimit:
                reply.data[1] = caller.coreLimit(); reply.data[2] = caller.coreHardLimit();
                if (args[0] && !caller.setCoreLimit(args[1], args[2])) return failure(os::ErrorCode::PermissionDenied);
                break;
            case os::TaskOperation::ExitInfo: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target || (target != &caller && caller.effectiveUid() && !caller.hasCapability(taskCapabilitySpawn) &&
                    (target->parentId() != caller.id() || !target->dumpable() || target->effectiveUid() != caller.effectiveUid())))
                    return failure(os::ErrorCode::PermissionDenied);
                os::wire::ExitInfo info{}; target->getExitInfo(info);
                MessageData::copy(&reply.data[1], &info, sizeof(info)); break;
            }
            case os::TaskOperation::ImageMapping: {
                os::wire::ImageMapping image{};
                if (request.length < sizeof(image) / 8) return failure(os::ErrorCode::InvalidArgument);
                MessageData::copy(&image, args, sizeof(image));
                if (image.begin < 0x10000 || image.begin >= image.end || image.end > 0x800000000000ULL ||
                    ((image.begin | image.end | image.offset) & 4095) || !image.path[0] || image.path[sizeof(image.path) - 1] ||
                    !memory::validateUserMemory(caller.addressSpace().rootPhysicalAddress(), image.begin, 1, false) ||
                    !caller.recordImage(image)) return failure(os::ErrorCode::InvalidArgument);
                break;
            }
            case os::TaskOperation::ProbeMemory: {
                if (args[2] > 1) return failure(os::ErrorCode::InvalidArgument);
                if (caller.isSystemService() && args[0] >= 0xffff800000000000ULL) {
                    if (!memory::validateUserMemory(caller.addressSpace().rootPhysicalAddress(), args[0], args[1], args[2])) return failure(os::ErrorCode::BadAddress);
                } else if (!memory::PagerBridge::ensureUserMemory(args[0], args[1], args[2])) return failure(os::ErrorCode::BadAddress);
                break;
            }
            case os::TaskOperation::SignalGeneration: reply.data[1] = thread->signalGeneration; break;
            case os::TaskOperation::Auxiliary:
                if (!args[1] || args[1] > 128 || args[2] < 0x10000 || args[2] >= 0x800000000000ULL || args[3] >= 0x800000000000ULL ||
                    !memory::validateUserMemory(caller.addressSpace().rootPhysicalAddress(), args[0], args[1] * 56, false))
                    return failure(os::ErrorCode::InvalidArgument);
                caller.recordAuxiliary(args[0], args[1], args[2], args[3]); break;
            case os::TaskOperation::CoreConfigure: {
                os::wire::CorePolicy policy{}; MessageData::copy(&policy, args, sizeof(policy));
                const int result = CoreDump::configure(caller, policy); if (result) return result; break;
            }
            case os::TaskOperation::CoreAcquire: {
                os::wire::CoreTicket ticket{}; const int result = CoreDump::acquire(caller, ticket);
                if (result) return result;
                MessageData::copy(&reply.data[1], &ticket, sizeof(ticket)); break;
            }
            case os::TaskOperation::CoreRead: {
                if (caller.serviceRole() != os::ServiceRole::Task) return failure(os::ErrorCode::PermissionDenied);
                uint8_t buffer[4096];
                if (!args[3] || args[2] > sizeof(buffer)) return failure(os::ErrorCode::InvalidArgument);
                const int result = CoreDump::read(caller, args[0], args[1], buffer, args[2]);
                if (result < 0) return result;
                if (!syscall::copyToUser(reinterpret_cast<void*>(args[3]), buffer, result)) return failure(os::ErrorCode::InvalidArgument);
                reply.data[0] = result; break;
            }
            case os::TaskOperation::CoreFinish: {
                const int result = CoreDump::finish(caller, args[0], args[1]); if (result) return result; break;
            }
            case os::TaskOperation::Query:
            case os::TaskOperation::Enumerate: {
                auto target = request.type == static_cast<uint64_t>(os::TaskOperation::Query)
                    ? task::find(args[0] ? args[0] : caller.id())
                    : args[1] == 1 ? task::next(args[0])
                    : args[0] <= UINT32_MAX ? task::at(args[0]) : TaskReference();
                if (!target) return failure(os::ErrorCode::NotFound);
                describe(*target, caller, reply); break;
            }
            case os::TaskOperation::SetCredentials: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target || args[1] >= 65535 || args[2] >= 65535 || args[3] > 1) return failure(os::ErrorCode::InvalidArgument);
                if (!caller.hasCapability(taskCapabilityUsers) &&
                    (&caller != target || (caller.effectiveUid() != 0 && (args[1] != caller.uid() || args[2] != caller.gid()))))
                    return failure(os::ErrorCode::PermissionDenied);
                target->setCredentials(args[1], args[2], args[3]); break;
            }
            case os::TaskOperation::SetTerminalForeground:
                if ((args[0] == 1 && caller.serviceRole() != os::ServiceRole::Console) ||
                    (args[0] == 2 && caller.serviceRole() != os::ServiceRole::Tty) ||
                    (args[0] != 1 && args[0] != 2)) return failure(os::ErrorCode::PermissionDenied);
                if (!task::setTerminalForeground(args[0], args[1], args[2])) return failure(os::ErrorCode::InvalidArgument);
                break;
            case os::TaskOperation::SendSignal: {
                const int64_t target = static_cast<int64_t>(args[0]);
                if (args[1] > maximumSignal || target == INT64_MIN) return failure(os::ErrorCode::InvalidArgument);
                if (target > 0 && args[1]) {
                    auto process = task::find(target);
                    if (process && process->managedService()) return failure(os::ErrorCode::PermissionDenied);
                }
                const bool delivered = target > 0 ? signal::sendSignalToTask(target, args[1], &caller)
                    : target == 0 ? signal::sendSignalToTaskGroup(caller.taskGroupId(), args[1], &caller)
                    : target < -1 && signal::sendSignalToTaskGroup(-target, args[1], &caller);
                if (!delivered) return failure(os::ErrorCode::NotFound);
                break;
            }
            case os::TaskOperation::SendThreadSignal:
                if (args[1] > maximumSignal) return failure(os::ErrorCode::InvalidArgument);
                if (!caller.deliverThreadSignal(args[0], args[1])) return failure(os::ErrorCode::NotFound);
                break;
            case os::TaskOperation::CreateSession:
                if (!caller.createSession()) return failure(os::ErrorCode::PermissionDenied);
                reply.data[0] = caller.sessionId(); break;
            case os::TaskOperation::Identifiers: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target) return failure(os::ErrorCode::NotFound);
                os::wire::TaskIdentifiers ids{target->parentId(), target->taskGroupId(), target->sessionId()};
                MessageData::copy(&reply.data[1], &ids, sizeof(ids)); break;
            }
            case os::TaskOperation::SetGroup: {
                auto target = task::find(args[1] ? args[1] : caller.id());
                if (!target || (target != &caller && target->parentId() != caller.id())) return failure(os::ErrorCode::NotFound);
                if (!target->setTaskGroup(args[0])) return failure(os::ErrorCode::PermissionDenied);
                break;
            }
            case os::TaskOperation::SignalAction: {
                if (args[0] > maximumSignal || !signal::isValidSignal(args[0]) || args[1] > 1) return failure(os::ErrorCode::InvalidArgument);
                SignalAction action{}, old{};
                if (args[1]) {
                    if (request.length < 8) return failure(os::ErrorCode::InvalidArgument);
                    MessageData::copy(&action, &args[4], sizeof(action));
                    if ((action.getFlags() & ~(signalFlagNoDefer | signalFlagResetHand | signalFlagOnStack | 0x00000004ULL | 0x10000000ULL)) ||
                        (action.getHandler() > signalIgnore && (!action.getRestorer() ||
                         !memory::validateUserMemory(caller.addressSpace().rootPhysicalAddress(), action.getHandler(), 1, false) ||
                         !memory::validateUserMemory(caller.addressSpace().rootPhysicalAddress(), action.getRestorer(), 1, false))) ||
                        !caller.setSignalAction(args[0], action, &old)) return failure(os::ErrorCode::InvalidArgument);
                } else if (!caller.getSignalAction(args[0], old)) return failure(os::ErrorCode::InvalidArgument);
                MessageData::copy(&reply.data[1], &old, sizeof(old)); break;
            }
            case os::TaskOperation::SignalMask: {
                if (args[0] > 2) return failure(os::ErrorCode::InvalidArgument);
                const uint64_t old = thread->getSignalMask();
                const uint64_t mask = args[1] & ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
                    (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
                thread->setSignalMask(args[0] == 0 ? old | mask : args[0] == 1 ? old & ~mask : mask);
                reply.data[1] = old; break;
            }
            case os::TaskOperation::PendingSignals: reply.data[1] = caller.pendingSignals(thread); break;
            case os::TaskOperation::TakeSignal: {
                if (!args[0]) return failure(os::ErrorCode::InvalidArgument);
                SignalAction action{};
                const uint32_t value = caller.takePendingSignal(thread, args[0], action, false);
                if (!value) return failure(os::ErrorCode::WouldBlock);
                reply.data[0] = value; break;
            }
            default: return failure(os::ErrorCode::InvalidArgument);
        }
        reply.length = os::ipcDataWordCount;
        return 0;
    }
};

}  // namespace
EndpointService& taskKernelService() {
    static TaskService service;
    return service;
}
}  // namespace kernel
