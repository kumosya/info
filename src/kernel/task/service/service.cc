#include "kernel/core_service.h"
#include "os/pci.h"

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "kernel/new.h"
#include "kernel/thread.h"
#include "kernel/signal.h"

#include "kernel/service_message.h"
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
        info.capabilities != 0 || info.terminalKind > 2 || (info.flags & ~(os::spawnFlagNewSession | os::spawnFlagManagedService)) ||
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
    switch (static_cast<os::ServiceRole>(info.serviceRole)) {
        case os::ServiceRole::None: case os::ServiceRole::Tty: break;
        case os::ServiceRole::Users: serviceCapabilities = taskCapabilityUsers; break;
        case os::ServiceRole::Console: case os::ServiceRole::Window: serviceCapabilities = taskCapabilityDisplay; break;
        default: return error(SyscallError::PermissionDenied);
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
        os::TaskInfo info{};
        info.id = target.id(); info.state = static_cast<uint32_t>(target.state());
        info.exitCode = target.exitCode(); info.uid = target.uid(); info.gid = target.gid();
        info.effectiveUid = target.effectiveUid(); info.effectiveGid = target.effectiveGid();
        info.parentId = target.parentId(); info.serviceRole = static_cast<uint32_t>(target.serviceRole());
        info.taskGroupId = target.taskGroupId(); info.sessionId = target.sessionId();
        info.terminalForegroundGroup = task::terminalForeground(target.terminalKind(), target.terminalIndex());
        info.startedNanoseconds = target.startedNanoseconds(); info.cpuNanoseconds = target.cpuNanoseconds();
        info.terminalKind = target.terminalKind(); info.terminalIndex = target.terminalIndex();
        info.commandLineSize = caller.effectiveUid() == 0 || caller.effectiveUid() == target.effectiveUid() ? target.commandLineSize() : 0;
        info.managedService = target.managedService();
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
        switch (static_cast<os::TaskOperation>(request.type)) {
            case os::TaskOperation::SpawnProgram:
            case os::TaskOperation::SpawnImage: {
                TaskImages images(caller);
                reply.data[0] = request.type == static_cast<uint64_t>(os::TaskOperation::SpawnProgram)
                    ? images.spawnProgram(args[0], args[1], args[2], args[3]) : images.spawnImage(args[0]);
                if (static_cast<int64_t>(reply.data[0]) >= 0) MessageData::created(reply, reply.data[0]);
                break;
            }
            case os::TaskOperation::WaitChild:
            case os::TaskOperation::WaitThread: {
                const bool child = request.type == static_cast<uint64_t>(os::TaskOperation::WaitChild);
                if ((child && (args[1] & ~11ULL)) || (!child && args[1] > 1) || static_cast<int64_t>(args[0]) == INT64_MIN)
                    return failure(os::ErrorCode::InvalidArgument);
                while (true) {
                    int status = 0;
                    uint64_t id = 0;
                    const int result = child ? caller.waitForChild(static_cast<int64_t>(args[0]), thread, status, args[1], id)
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
                    if (const int error = thread->waitError()) return error;
                }
                break;
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
                    reply.data[0] = target->state() == TaskState::Stopped ? 2 : target->isTerminated() ? 1 : 0;
                    if (reply.data[0] == 1) target->destroy();
                }
                break;
            }
            case os::TaskOperation::Identity: reply.data[0] = caller.id(); break;
            case os::TaskOperation::ThreadIdentity: reply.data[0] = thread->getId(); break;
            case os::TaskOperation::SignalReturn:
                if (thread->getType() != ThreadType::User || !thread->syscallContext() ||
                    !signal::restoreSignalFrame(thread->syscallContext(), args[0])) return failure(os::ErrorCode::InvalidArgument);
                thread->markContextRestored(); break;
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
                os::TaskIdentifiers ids{target->parentId(), target->taskGroupId(), target->sessionId()};
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
                    if ((action.getFlags() & ~(signalFlagNoDefer | signalFlagResetHand | 0x00000004ULL | 0x10000000ULL)) ||
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
