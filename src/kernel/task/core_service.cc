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

namespace kernel {
namespace {
class MessageData {
   public:
    static bool name(const IpcMessage& message, uint32_t word, const char*& value, size_t limit = 64) {
        if (message.length <= word) return false;
        value = reinterpret_cast<const char*>(&message.data[word]);
        const size_t bytes = (message.length - word) * sizeof(uint64_t);
        for (size_t i = 0; i < bytes && i < limit; ++i) if (!value[i]) return i != 0;
        return false;
    }
    static void copy(void* destination, const void* source, size_t bytes) {
        auto* to = static_cast<uint8_t*>(destination);
        auto* from = static_cast<const uint8_t*>(source);
        for (size_t i = 0; i < bytes; ++i) to[i] = from[i];
    }
    static void created(IpcMessage& reply, uint64_t handle) {
        if (handle) reply.handles[reply.handleCount++] = handle;
    }
};

class PciService final : public EndpointService {
 public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        if (!caller.hasCapability(taskCapabilityPci) || caller.serviceRole() != os::ServiceRole::Pci || request.length < 4) return -3;
        const auto operation = static_cast<os::PciOperation>(request.type);
        os::PciFunctionInfo function{};
        MessageData::copy(&function, &request.data[1], sizeof(function));
        if (operation == os::PciOperation::Enumerate) {
            if (!request.data[0] || request.data[0] > 64) return -1;
            const uint32_t count = device::PciAccess::enumerate(reinterpret_cast<os::PciFunctionInfo*>(&reply.data[1]), request.data[0]);
            reply.data[0] = count;
        } else if (operation == os::PciOperation::ReadConfig) {
            uint32_t value = 0;
            if (request.data[0] > UINT16_MAX || !device::PciAccess::read(function, request.data[0], value)) return -1;
            reply.data[1] = value;
        } else if (operation == os::PciOperation::WriteConfig) {
            if (request.data[0] > UINT16_MAX || request.data[3] > UINT32_MAX ||
                !device::PciAccess::write(function, request.data[0], request.data[3])) return -1;
        } else return -1;
        reply.length = os::ipcDataWordCount; return 0;
    }
};
PciService pciService;
class DeviceService final : public EndpointService {
   public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        if (request.length < 5) return -1;
        const auto* args = request.data;
        const auto operation = static_cast<os::DeviceOperation>(request.type);
        if (operation == os::DeviceOperation::Framebuffer) {
            os::DisplayInfo info{};
            if (!caller.hasCapability(taskCapabilityDisplay) || !device::DisplayAccess::mapFramebuffer(caller, args[0], info)) return -3;
            MessageData::copy(&reply.data[1], &info, sizeof(info));
        } else {
            // Only fixed boot identities receive device authority.
            if (!caller.hasCapability(taskCapabilityDevice) ||
                (caller.serviceRole() != os::ServiceRole::Device && caller.serviceRole() != os::ServiceRole::Storage)) return -3;
            switch (operation) {
                case os::DeviceOperation::InitializeInput:
                    if (caller.serviceRole() != os::ServiceRole::Device) return -3;
                    reply.data[0] = device::Ps2Access::initialize(); break;
                case os::DeviceOperation::ReadInput: {
                    if (caller.serviceRole() != os::ServiceRole::Device) return -3;
                    uint8_t value = 0; bool mouse = false;
                    if (!device::Ps2Access::read(value, mouse)) return -6;
                    reply.data[1] = value; reply.data[2] = mouse; break;
                }
                case os::DeviceOperation::MapMmio: {
                    os::PciFunctionInfo function{};
                    MessageData::copy(&function, &args[3], sizeof(function));
                    if (args[0] > 5) return -1;
                    auto* object = device::MmioRegion::map(caller, function, args[0], args[1], args[2]);
                    reply.data[0] = object ? caller.handles().add(object, HandleType::MmioRegion, 3) : 0;
                    MessageData::created(reply, reply.data[0]);
                    break;
                }
                case os::DeviceOperation::AllocateDma: {
                    os::DmaBufferInfo info{};
                    auto* object = device::DmaBuffer::create(caller, args[0], info);
                    if (!object) return -5;
                    info.handle = caller.handles().add(object, HandleType::DmaBuffer, 3);
                    MessageData::created(reply, info.handle);
                    MessageData::copy(&reply.data[1], &info, sizeof(info));
                    break;
                }
                case os::DeviceOperation::BindIrq: {
                    HandleTable::Pin pin(caller.handles(), args[1], HandleType::Endpoint, handleRightRead);
                    if (args[0] > 15 || args[2] > 1) return -1;
                    auto* object = device::InterruptBinding::create(caller, args[0], pin.as<Endpoint>(), args[2]);
                    reply.data[0] = object ? caller.handles().add(object, HandleType::InterruptBinding, 4) : 0;
                    MessageData::created(reply, reply.data[0]);
                    break;
                }
                case os::DeviceOperation::AcknowledgeIrq: {
                    HandleTable::Pin pin(caller.handles(), args[0], HandleType::InterruptBinding, handleRightManage);
                    auto* object = pin.as<device::InterruptBinding>();
                    if (!object || !object->acknowledge(caller)) return -2;
                    break;
                }
                default: return -1;
            }
        }
        reply.length = os::ipcDataWordCount;
        return 0;
    }
};

class SystemService final : public EndpointService {
   public:
    int call(Task&, const IpcMessage& request, IpcMessage& reply) override {
        switch (static_cast<os::SystemOperation>(request.type)) {
            case os::SystemOperation::MonotonicTime: reply.data[0] = time::getMonotonicTime(); break;
            case os::SystemOperation::WallClockTime: reply.data[0] = time::getWallClockTime(); break;
            case os::SystemOperation::ClockResolution: reply.data[0] = time::getClockResolution(); break;
            case os::SystemOperation::Info: {
                const os::SystemInfo info{time::getMonotonicTime(), memory::getTotalMemoryBytes(),
                    memory::getFreeMemoryBytes(), smp::getOnlineCpuCount(), 0};
                MessageData::copy(&reply.data[1], &info, sizeof(info));
                break;
            }
            case os::SystemOperation::Log: {
                const char* text;
                if (!MessageData::name(request, 0, text, 512)) return -1;
                serial::serialWrite(text);
                break;
            }
            default: return -1;
        }
        reply.length = os::ipcDataWordCount;
        return 0;
    }
};



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
        info.capabilities != 0 || (info.flags & ~os::spawnFlagNewSession) ||
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

    if (info.serviceRole != 0)
        return error(SyscallError::PermissionDenied);
    auto logicalParentReference = task::find(info.parentTaskId ? info.parentTaskId : parent->id());
    Task* logicalParent = logicalParentReference;
    if (info.parentTaskId != 0) {
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
    auto child = UserProgramLoader::loadImage(
        reinterpret_cast<uint64_t>(info.image), info.imageSize, name, parent,
        logicalParent, info.bootstrapHandle, info.capabilities,
        info.standardInputHandle, info.standardOutputHandle,
        info.standardErrorHandle, info.argumentCount, arguments, sharedImages,
        info.sharedImageCount, &loadError, static_cast<os::ServiceRole>(info.serviceRole), (info.flags & os::spawnFlagNewSession) != 0);
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
    static void describe(Task& target, IpcMessage& reply) {
        os::TaskInfo info{};
        info.id = target.id(); info.state = static_cast<uint32_t>(target.state());
        info.exitCode = target.exitCode(); info.uid = target.uid(); info.gid = target.gid();
        info.effectiveUid = target.effectiveUid(); info.effectiveGid = target.effectiveGid();
        info.parentId = target.parentId(); info.serviceRole = static_cast<uint32_t>(target.serviceRole());
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
            case os::TaskOperation::Terminate: {
                const bool terminate = request.type == static_cast<uint64_t>(os::TaskOperation::Terminate);
                HandleTable::Pin pin(caller.handles(), args[0], HandleType::Task,
                    terminate ? handleRightManage : handleRightRead);
                Task* target = pin.as<Task>();
                if (!target) return failure(os::ErrorCode::InvalidHandle);
                if (terminate) {
                    if (target == &caller || target == task::getIdle()) return failure(os::ErrorCode::InvalidHandle);
                    target->terminateOtherThreads(static_cast<int>(args[1])); target->destroy();
                } else if (request.type == static_cast<uint64_t>(os::TaskOperation::Info)) describe(*target, reply);
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
                    : args[0] <= UINT32_MAX ? task::at(args[0]) : TaskReference();
                if (!target) return failure(os::ErrorCode::NotFound);
                describe(*target, reply); break;
            }
            case os::TaskOperation::SetCredentials: {
                auto target = task::find(args[0] ? args[0] : caller.id());
                if (!target || args[1] >= 65535 || args[2] >= 65535 || args[3] > 1) return failure(os::ErrorCode::InvalidArgument);
                if (!caller.hasCapability(taskCapabilityUsers) &&
                    (&caller != target || (caller.effectiveUid() != 0 && (args[1] != caller.uid() || args[2] != caller.gid()))))
                    return failure(os::ErrorCode::PermissionDenied);
                target->setCredentials(args[1], args[2], args[3]); break;
            }
            case os::TaskOperation::SendSignal: {
                const int64_t target = static_cast<int64_t>(args[0]);
                if (args[1] > maximumSignal || target == INT64_MIN) return failure(os::ErrorCode::InvalidArgument);
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

TaskService taskService;
class ResidentMemoryService final : public EndpointService {
    memory::MemoryService implementation_;
   public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        return implementation_.call(caller, request, reply);
    }
};
ResidentMemoryService memoryService;
DeviceService deviceService;
SystemService systemService;
Endpoint* coreEndpoints[static_cast<uint64_t>(os::CoreServiceKind::Count)];
}  // namespace

bool CoreServices::initialize() {
    new (&taskService) TaskService;
    new (&pciService) PciService;
    if (!memory::PageMappings::initialize()) return false;
    new (&memoryService) ResidentMemoryService;
    new (&deviceService) DeviceService;
    new (&systemService) SystemService;
    EndpointService* services[] = {nullptr, nullptr, &memoryService, &deviceService, &systemService, &taskService, &pciService};
    for (uint32_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i) {
        if (!services[i]) continue;
        coreEndpoints[i] = Endpoint::create(services[i]);
        if (!coreEndpoints[i]) return false;
    }
    return true;
}
uint64_t CoreServices::getHandle(Task& caller, os::CoreServiceKind kind) {
    const auto index = static_cast<uint64_t>(kind);
    return index < static_cast<uint64_t>(os::CoreServiceKind::Count) && coreEndpoints[index]
        ? caller.handles().add(coreEndpoints[index], HandleType::Endpoint, handleRightWrite) : 0;
}
}  // namespace kernel
