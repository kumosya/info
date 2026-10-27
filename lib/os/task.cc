#include <new>

#include "service/common/channel.h"
#include "service/common/entry.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/console/client.h"
#include <os/console.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace os {
int detail::TaskQuery::credentials(uint64_t taskId,
                                   os::wire::CredentialInfo& info) {
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TaskOperation::Credentials);
    request.length   = 4;
    request.data[1]  = taskId;
    const int result = this->result(request, reply);
    if (!result && !reply.data[1])
        Runtime::copyBytes(&info, &reply.data[2], sizeof(info));
    return result          ? result
           : reply.data[1] ? static_cast<int>(ErrorCode::InvalidArgument)
                           : 0;
}
int detail::TaskQuery::threadInfo(uint64_t taskId, uint64_t minimumTid,
                                  os::wire::ThreadInfo& info) {
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TaskOperation::ThreadInfo);
    request.length   = 4;
    request.data[0]  = taskId;
    request.data[1]  = minimumTid;
    const int result = this->result(request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}

namespace {

constexpr uint32_t maximumArgumentCount = 64;
constexpr uint32_t executePayloadSize =
    (ipcDataWordCount - 1) * sizeof(uint64_t);
class EndpointReference {
    uint64_t handle_;

   public:
    explicit EndpointReference(uint64_t handle) : handle_(handle) {}
    ~EndpointReference() {
        if (handle_) closeHandle(handle_);
    }
};

}  // namespace

uint64_t Task::execute(const char* path) {
    const char* arguments[] = {path};
    return detail::TaskControl().execute(path, 1, arguments);
}

uint64_t Task::execute(const char* path, int argumentCount,
                       const char* const* arguments) {
    return detail::TaskControl().executeWithIo(
        path, argumentCount, arguments, getStandardHandle(0),
        getStandardHandle(1), getStandardHandle(2));
}

uint64_t Task::executeWithIo(const char* path, int argumentCount,
                             const char* const* arguments,
                             uint64_t standardInputHandle,
                             uint64_t standardOutputHandle,
                             uint64_t standardErrorHandle) {
    return executeRequest(path, argumentCount, arguments, standardInputHandle,
                          standardOutputHandle, standardErrorHandle, false);
}
uint64_t Task::executeService(const char* path, const char* name) {
    const char* arguments[] = {name};
    return executeRequest(path, 1, arguments, 0, 0, 0, true);
}
uint64_t Task::executeService(const char* path, int count,
                              const char* const* arguments) {
    return executeRequest(path, count, arguments, 0, 0, 0, true);
}
uint64_t Task::executeRequest(const char* path, int argumentCount,
                              const char* const* arguments,
                              uint64_t standardInputHandle,
                              uint64_t standardOutputHandle,
                              uint64_t standardErrorHandle, bool service) {
    const uint64_t registry = os::detail::RegistryClient().endpoint();
    const uint64_t taskService =
        os::detail::RegistryClient(registry).lookup(taskServiceName, 1000);
    EndpointReference taskReference(taskService);
    if (registry == 0) {
        Sys().log("[RUNTIME] task registry unavailable\n");
        return 0;
    }
    if (taskService == 0) {
        Sys().log("[RUNTIME] task service unavailable\n");
        return 0;
    }
    if (path == nullptr || argumentCount <= 0 ||
        argumentCount > static_cast<int>(maximumArgumentCount) ||
        arguments == nullptr) {
        Sys().log("[RUNTIME] invalid execute arguments\n");
        return 0;
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(service ? TaskRequest::ExecuteService
                                                 : TaskRequest::Execute);
    os::wire::TaskInfo self{};
    if (os::detail::TaskQuery().query(0, self)) return 0;
    request.data[0] =
        static_cast<uint64_t>(argumentCount) |
        (static_cast<uint64_t>(
             !service && self.terminalKind == 1 ? self.terminalIndex + 1 : 0)
         << 32);
    char* payload = reinterpret_cast<char*>(&request.data[1]);
    uint32_t used = 0;
    const char* values[maximumArgumentCount + 1]{};
    values[0] = path;
    for (int index = 0; index < argumentCount; ++index) {
        values[index + 1] = arguments[index];
    }
    for (int index = 0; index <= argumentCount; ++index) {
        const char* argument = values[index];
        const size_t length  = Runtime::stringLength(argument) + 1;
        if (argument == nullptr || length > executePayloadSize - used) {
            return 0;
        }
        for (size_t byte = 0; byte < length; ++byte) {
            payload[used + byte] = argument[byte];
        }
        used += static_cast<uint32_t>(length);
    }
    request.length           = 1 + (used + 7) / 8;
    request.handleCount      = 1;
    request.handles[0]       = registry;
    request.handleOptions[0] = ipcHandleRestrictRights;
    request.handleRights[0]  = 2;
    if (standardInputHandle != 0) {
        request.data[0] |= taskExecuteHasStdin;
        request.handles[request.handleCount++] = standardInputHandle;
    }
    if (standardOutputHandle != 0) {
        request.data[0] |= taskExecuteHasStdout;
        request.handles[request.handleCount++] = standardOutputHandle;
    }
    if (standardErrorHandle != 0) {
        request.data[0] |= taskExecuteHasStderr;
        request.handles[request.handleCount++] = standardErrorHandle;
    }
    IpcMessage reply{};
    if (callService(taskService, request, reply) != 0) {
        Sys().log("[RUNTIME] task service call failed\n");
        return 0;
    }
    if (reply.data[0] != 0 || reply.handleCount != 1) {
        Sys().log("[RUNTIME] task service rejected execute\n");
        return 0;
    }
    return reply.handles[0];
}

int Task::wait(uint64_t task, int* exitCode) {
    const uint64_t registry = os::detail::RegistryClient().endpoint();
    const uint64_t taskService =
        os::detail::RegistryClient(registry).lookup(taskServiceName, 1000);
    EndpointReference taskReference(taskService);
    if (task == 0 || taskService == 0 || exitCode == nullptr) {
        return static_cast<int>(ErrorCode::InvalidArgument);
    }
    while (true) {
        IpcMessage request{};
        request.type        = static_cast<uint64_t>(TaskRequest::Wait);
        request.handleCount = 1;
        request.handles[0]  = task;
        IpcMessage reply{};
        if (callService(taskService, request, reply) != 0 ||
            reply.data[0] > 2) {
            return static_cast<int>(ErrorCode::InvalidHandle);
        }
        if (reply.data[0] == 1) {
            *exitCode = static_cast<int>(reply.data[1]);
            os::wire::TaskInfo info{};
            if (os::detail::TaskQuery().getInfo(task, info) == 0) {
                int status = 0;
                detail::TaskControl().waitChild(static_cast<int64_t>(info.id),
                                                status, 1);
            }
            closeHandle(task);
            return 0;
        }
        if (reply.data[0] == 2) {
            return static_cast<int>(ErrorCode::Interrupted);
        }
        sleep(1);
    }
}

bool Task::kill(uint64_t taskId) {
    return taskId != 0 && detail::TaskControl().sendSignal(taskId, 15) == 0;
}

int detail::TaskQuery::query(uint64_t taskId, os::wire::TaskInfo& info) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::Query);
    request.length       = 4;
    request.data[0]      = taskId;
    const int64_t result = this->result(request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}
int Task::setCredentials(uint64_t taskId, uint32_t uid, uint32_t gid,
                         bool effectiveOnly) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::SetCredentials);
    request.length  = 4;
    request.data[0] = taskId;
    request.data[1] = uid;
    request.data[2] = gid;
    request.data[3] = effectiveOnly;
    return this->result(request, reply);
}

Task::Task(uint64_t id) : Task() { id_ = id; }
uint64_t Task::id() const { return id_ ? id_ : detail::TaskControl().getId(); }

int Task::sendSignal(uint32_t signal) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return detail::TaskControl().sendSignal(id_, signal);
}

int Task::waitThread(uint64_t threadId, int& exitCode, bool noHang) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::WaitThread);
    request.length       = 4;
    request.data[0]      = threadId;
    request.data[1]      = noHang;
    const int64_t result = this->result(request, reply);
    if (!result) exitCode = reply.data[1];
    return result;
}

int Task::setSignalAction(uint32_t signal, const wire::SignalAction* action,
                          wire::SignalAction* oldAction) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::SignalAction);
    request.length  = 4;
    request.data[0] = signal;
    request.data[1] = action != nullptr;
    if (action) Runtime::copyBytes(&request.data[4], action, sizeof(*action));
    request.length       = 8;
    const int64_t status = this->result(request, reply);
    if (!status && oldAction)
        Runtime::copyBytes(oldAction, &reply.data[1], sizeof(*oldAction));
    return status;
}

int Task::setSignalMask(int how, uint64_t mask, uint64_t& oldMask) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::SignalMask);
    request.length       = 4;
    request.data[0]      = how;
    request.data[1]      = mask;
    const int64_t status = this->result(request, reply);
    if (!status) oldMask = reply.data[1];
    return status;
}

int Task::getPendingSignals(uint64_t& mask) {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::PendingSignals);
    request.length = 4;

    const int64_t status = this->result(request, reply);
    if (!status) mask = reply.data[1];
    return status;
}

int Task::signalStack(const OsSignalStack* stack, OsSignalStack* oldStack) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::SignalStack);
    request.length  = 4;
    request.data[0] = stack != nullptr;
    if (stack) Runtime::copyBytes(&request.data[1], stack, sizeof(*stack));
    const int result = this->result(request, reply);
    if (!result && oldStack)
        Runtime::copyBytes(oldStack, &reply.data[1], sizeof(*oldStack));
    return result;
}

int Task::coreLimit(bool set, uint64_t& soft, uint64_t& hard) {
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TaskOperation::CoreLimit);
    request.length   = 4;
    request.data[0]  = set;
    request.data[1]  = soft;
    request.data[2]  = hard;
    const int result = this->result(request, reply);
    if (!result && !set) {
        soft = reply.data[1];
        hard = reply.data[2];
    }
    return result;
}

int detail::TaskQuery::exitInfo(uint64_t id, os::wire::ExitInfo& info) {
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TaskOperation::ExitInfo);
    request.length   = 4;
    request.data[0]  = id;
    const int result = this->result(request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}

int Task::recordImage(const char* path, uint64_t begin, uint64_t end,
                      uint64_t offset) {
    if (!path ||
        Runtime::stringLength(path) >= sizeof(wire::ImageMapping::path))
        return -1;
    wire::ImageMapping image{begin, end, offset, {}};
    Runtime::copyString(image.path, path, sizeof(image.path));
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::ImageMapping);
    request.length = sizeof(image) / 8;
    Runtime::copyBytes(request.data, &image, sizeof(image));
    return this->result(request, reply);
}

bool Task::accessible(const void* address, uint64_t bytes, bool writable) {
    if (!bytes) return true;
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::ProbeMemory);
    request.length  = 4;
    request.data[0] = reinterpret_cast<uint64_t>(address);
    request.data[1] = bytes;
    request.data[2] = writable;
    return !this->result(request, reply);
}

int Task::recordAuxiliary(uint64_t headers, uint64_t count, uint64_t entry,
                          uint64_t loader) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::Auxiliary);
    request.length  = 4;
    request.data[0] = headers;
    request.data[1] = count;
    request.data[2] = entry;
    request.data[3] = loader;
    return this->result(request, reply);
}

int Task::waitSignal(uint64_t mask) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::TakeSignal);
    request.length       = 4;
    request.data[0]      = mask;
    const int64_t status = this->result(request, reply);

    return status;
}

void Task::signalReturn(void* frame) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::SignalReturn);
    request.length  = 5;
    request.data[0] = reinterpret_cast<uint64_t>(frame);
    this->result(request, reply);
    while (true) asm volatile("ud2");
}

int Task::sendThreadSignal(uint64_t threadId, uint32_t signal) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::SendThreadSignal);
    request.length  = 4;
    request.data[0] = threadId;
    request.data[1] = signal;
    const int64_t status = this->result(request, reply);

    return status;
}

int64_t Task::waitChild(int64_t taskId, int& status, int options) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::WaitChild);
    request.length       = 4;
    request.data[0]      = taskId;
    request.data[1]      = options;
    const int64_t result = this->result(request, reply);
    if (result >= 0) status = reply.data[1];
    return result;
}

uint64_t Task::spawnProgram(ProgramId program, uint64_t bootstrapHandle,
                            uint32_t argumentCount,
                            const char* const* arguments) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::SpawnProgram);
    request.length       = 4;
    request.data[0]      = static_cast<uint64_t>(program);
    request.data[1]      = bootstrapHandle;
    request.data[2]      = argumentCount;
    request.data[3]      = reinterpret_cast<uint64_t>(arguments);
    const int64_t result = this->result(request, reply);

    return result;
}

uint64_t detail::TaskQuery::spawnImage(const os::wire::SpawnImageInfo& info) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::SpawnImage);
    request.length       = 4;
    request.data[0]      = reinterpret_cast<uint64_t>(&info);
    const int64_t result = this->result(request, reply);

    return result;
}

int Task::getState(uint64_t taskHandle, int& exitCode) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::State);
    request.length       = 4;
    request.data[0]      = taskHandle;
    const int64_t status = this->result(request, reply);
    if (status >= 0) exitCode = static_cast<int>(reply.data[1]);
    return status;
}

int detail::TaskQuery::getInfo(uint64_t taskHandle, os::wire::TaskInfo& info) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::Info);
    request.length       = 4;
    request.data[0]      = taskHandle;
    const int64_t status = this->result(request, reply);
    if (!status) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return status;
}

int Task::terminate(uint64_t taskHandle, int exitCode) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::Terminate);
    request.length       = 4;
    request.data[0]      = taskHandle;
    request.data[1]      = static_cast<int64_t>(exitCode);
    const int64_t status = this->result(request, reply);

    return status;
}

int Task::sendSignal(uint64_t taskId, uint32_t signal) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::SendSignal);
    request.length       = 4;
    request.data[0]      = taskId;
    request.data[1]      = signal;
    const int64_t status = this->result(request, reply);

    return status;
}

int Task::setGroupTarget(uint64_t taskGroupId, uint64_t taskId) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::SetGroup);
    request.length       = 4;
    request.data[0]      = taskGroupId;
    request.data[1]      = taskId;
    const int64_t status = this->result(request, reply);

    return status;
}

int64_t Task::createSession() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::CreateSession);
    request.length = 4;

    const int64_t status = this->result(request, reply);

    return status;
}

int Task::getIdentifiers(wire::TaskIdentifiers& identifiers, uint64_t taskId) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(TaskOperation::Identifiers);
    request.length       = 4;
    request.data[0]      = taskId;
    const int64_t status = this->result(request, reply);
    if (!status)
        Runtime::copyBytes(&identifiers, &reply.data[1], sizeof(identifiers));
    return status;
}

uint64_t Task::readId() const {
    if (id_) return id_;
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::Identity);
    request.length = 4;

    const int64_t status = const_cast<Task*>(this)->result(request, reply);

    return status;
}

uint64_t Task::getThreadId() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::ThreadIdentity);
    request.length = 4;
    return this->result(request, reply);
}

detail::TaskQuery::TaskQuery() : Sys::Transport(CoreServiceKind::Task) {}

Task::Credential::Credential(const Task& task) : taskId_(task.id_) {}

int Task::Credential::refresh() {
    if (!taskId_) return static_cast<int>(ErrorCode::InvalidHandle);
    wire::CredentialInfo value{};
    const int status = detail::TaskQuery().credentials(taskId_, value);
    if (status) return status;
    if (value.groupCount > maximumSupplementaryGroups)
        return static_cast<int>(ErrorCode::InvalidArgument);
    {
        uid_          = value.uid;
        effectiveUid_ = value.effectiveUid;
        savedUid_     = value.savedUid;
        gid_          = value.gid;
        effectiveGid_ = value.effectiveGid;
        savedGid_     = value.savedGid;
        groupCount_   = value.groupCount;
        Runtime::copyBytes(groups_, value.groups, sizeof(groups_));
    }
    return 0;
}

uint32_t Task::Credential::getUid() const { return uid_; }

uint32_t Task::Credential::getEffectiveUid() const { return effectiveUid_; }

uint32_t Task::Credential::getSavedUid() const { return savedUid_; }

uint32_t Task::Credential::getGid() const { return gid_; }

uint32_t Task::Credential::getEffectiveGid() const { return effectiveGid_; }

uint32_t Task::Credential::getSavedGid() const { return savedGid_; }

uint32_t Task::Credential::getGroupCount() const { return groupCount_; }

uint32_t Task::Credential::getGroup(uint32_t index) const {
    return index < groupCount_ ? groups_[index] : UINT32_MAX;
}

uint32_t Task::getState() const { return state_; }

int32_t Task::getExitCode() const { return exitCode_; }

const char* Task::getName() const { return name_; }

uint32_t Task::getUid() const { return uid_; }

uint32_t Task::getGid() const { return gid_; }

uint32_t Task::getEffectiveUid() const { return effectiveUid_; }

uint32_t Task::getEffectiveGid() const { return effectiveGid_; }

uint64_t Task::getParentId() const { return parentId_; }

uint32_t Task::getTrustedDeviceService() const { return trustedDeviceService_; }

uint32_t Task::getServiceRole() const { return serviceRole_; }

uint64_t Task::getGroupId() const { return taskGroupId_; }

uint64_t Task::getSessionId() const { return sessionId_; }

uint64_t Task::getTerminalForegroundGroup() const {
    return terminalForegroundGroup_;
}

uint64_t Task::getStartedNanoseconds() const { return startedNanoseconds_; }

uint64_t Task::getCpuNanoseconds() const { return cpuNanoseconds_; }

uint32_t Task::getTerminalKind() const { return terminalKind_; }

uint32_t Task::getTerminalIndex() const { return terminalIndex_; }

uint32_t Task::getCommandLineSize() const { return commandLineSize_; }

uint32_t Task::getManagedService() const { return managedService_; }

const char* Task::getCommandLine() const { return commandLine_; }

uint64_t Task::getMainThreadId() const { return mainThreadId_; }

int32_t Task::getNice() const { return nice_; }

uint32_t Task::getThreadCount() const { return threadCount_; }

uint64_t Task::Thread::getId() const { return id_; }

uint64_t Task::Thread::getTaskId() const { return taskId_; }

uint64_t Task::Thread::getCpuNanoseconds() const { return cpuNanoseconds_; }

int32_t Task::Thread::getNice() const { return nice_; }

uint32_t Task::Thread::getState() const { return state_; }

void Task::decode(const void* bytes) {
    wire::TaskInfo value{};
    Runtime::copyBytes(&value, bytes, sizeof(value));
    id_       = value.id;
    state_    = value.state;
    exitCode_ = value.exitCode;
    Runtime::copyBytes(name_, value.name, sizeof(name_));
    uid_                     = value.uid;
    gid_                     = value.gid;
    effectiveUid_            = value.effectiveUid;
    effectiveGid_            = value.effectiveGid;
    parentId_                = value.parentId;
    trustedDeviceService_    = value.trustedDeviceService;
    serviceRole_             = value.serviceRole;
    taskGroupId_             = value.taskGroupId;
    sessionId_               = value.sessionId;
    terminalForegroundGroup_ = value.terminalForegroundGroup;
    startedNanoseconds_      = value.startedNanoseconds;
    cpuNanoseconds_          = value.cpuNanoseconds;
    terminalKind_            = value.terminalKind;
    terminalIndex_           = value.terminalIndex;
    commandLineSize_         = value.commandLineSize;
    managedService_          = value.managedService;
    Runtime::copyBytes(commandLine_, value.commandLine, sizeof(commandLine_));
    mainThreadId_                          = value.mainThreadId;
    nice_                                  = value.nice;
    threadCount_                           = value.threadCount;
    name_[sizeof(name_) - 1]               = 0;
    commandLine_[sizeof(commandLine_) - 1] = 0;
}

int Task::refreshTarget(uint64_t id) {
    if (!id) id = id_;
    wire::TaskInfo value{};
    const int status = detail::TaskQuery().query(id, value);
    if (!status) {
        decode(&value);
    }
    return status;
}

int Task::enumerate(uint64_t minimumId) {
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TaskOperation::Enumerate);
    request.length   = 4;
    request.data[0]  = minimumId;
    request.data[1]  = 1;
    const int status = result(request, reply);
    if (status) return status;
    if (reply.length < 1 + (sizeof(wire::TaskInfo) + 7) / 8)
        return static_cast<int>(ErrorCode::InvalidArgument);
    wire::TaskInfo value{};
    Runtime::copyBytes(&value, &reply.data[1], sizeof(value));
    decode(&value);
    name_[sizeof(name_) - 1]               = 0;
    commandLine_[sizeof(commandLine_) - 1] = 0;
    return 0;
}

int Task::refreshHandle(uint64_t handle) {
    wire::TaskInfo value{};
    const int status = detail::TaskQuery().getInfo(handle, value);
    if (!status) {
        decode(&value);
    }
    return status;
}

int Task::Thread::refresh() {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    const uint64_t task = taskId_, minimum = id_;
    wire::ThreadInfo value{};
    const int status = detail::TaskQuery().threadInfo(task, minimum, value);
    if (!status && value.id != id_)
        return error_ = static_cast<int>(ErrorCode::NotFound);
    if (!status) {
        id_             = value.id;
        taskId_         = value.taskId;
        cpuNanoseconds_ = value.cpuNanoseconds;
        nice_           = value.nice;
        state_          = value.state;
    }
    return status;
}

Task::Task() : Sys::Transport(CoreServiceKind::Task) {}

const void* Task::Image::getImage() const { return image_; }
uint64_t Task::Image::getImageSize() const { return imageSize_; }
const char* Task::Image::getName() const { return name_; }

void Task::Image::setImage(const void* image, uint64_t size) {
    image_     = image;
    imageSize_ = size;
}
void Task::Image::setName(const char* value) { name_ = value; }
const void* Task::Spawn::getImage() const { return image_; }
uint64_t Task::Spawn::getImageSize() const { return imageSize_; }
const char* Task::Spawn::getName() const { return name_; }
uint64_t Task::Spawn::getBootstrapHandle() const { return bootstrapHandle_; }
uint64_t Task::Spawn::getStandardInputHandle() const {
    return standardInputHandle_;
}
uint64_t Task::Spawn::getStandardOutputHandle() const {
    return standardOutputHandle_;
}
uint64_t Task::Spawn::getStandardErrorHandle() const {
    return standardErrorHandle_;
}
uint64_t Task::Spawn::getCapabilities() const { return capabilities_; }
uint64_t Task::Spawn::getParentTaskId() const { return parentTaskId_; }
uint32_t Task::Spawn::getArgumentCount() const { return argumentCount_; }
uint32_t Task::Spawn::getServiceRole() const { return serviceRole_; }
const char* const* Task::Spawn::getArguments() const { return arguments_; }
uint32_t Task::Spawn::getSharedImageCount() const { return sharedImageCount_; }
uint32_t Task::Spawn::getFlags() const { return flags_; }
uint32_t Task::Spawn::getTerminalKind() const { return terminalKind_; }
uint32_t Task::Spawn::getTerminalIndex() const { return terminalIndex_; }
const Task::Image* Task::Spawn::getSharedImages() const {
    return sharedImages_;
}

void Task::Spawn::setImage(const void* image, uint64_t size) {
    image_     = image;
    imageSize_ = size;
}
void Task::Spawn::setName(const char* value) { name_ = value; }
void Task::Spawn::setBootstrapHandle(uint64_t value) {
    bootstrapHandle_ = value;
}
void Task::Spawn::setStandardInputHandle(uint64_t value) {
    standardInputHandle_ = value;
}
void Task::Spawn::setStandardOutputHandle(uint64_t value) {
    standardOutputHandle_ = value;
}
void Task::Spawn::setStandardErrorHandle(uint64_t value) {
    standardErrorHandle_ = value;
}
void Task::Spawn::setCapabilities(uint64_t value) { capabilities_ = value; }
void Task::Spawn::setParentTaskId(uint64_t value) { parentTaskId_ = value; }
void Task::Spawn::setServiceRole(uint32_t value) { serviceRole_ = value; }
void Task::Spawn::setArguments(const char* const* value, uint32_t count) {
    arguments_     = value;
    argumentCount_ = count;
}
void Task::Spawn::setFlags(uint32_t value) { flags_ = value; }
void Task::Spawn::setTerminalKind(uint32_t value) { terminalKind_ = value; }
void Task::Spawn::setTerminalIndex(uint32_t value) { terminalIndex_ = value; }
void Task::Spawn::setSharedImages(const Task::Image* value, uint32_t count) {
    sharedImages_     = value;
    sharedImageCount_ = count;
}
const void* Task::Exec::getImage() const { return image_; }
uint64_t Task::Exec::getImageSize() const { return imageSize_; }
const char* Task::Exec::getName() const { return name_; }
uint32_t Task::Exec::getArgumentCount() const { return argumentCount_; }
uint32_t Task::Exec::getEnvironmentCount() const { return environmentCount_; }
const char* const* Task::Exec::getArguments() const { return arguments_; }
const char* const* Task::Exec::getEnvironment() const { return environment_; }
const void* Task::Exec::getRuntimeState() const { return runtimeState_; }
uint64_t Task::Exec::getRuntimeStateSize() const { return runtimeStateSize_; }
uint32_t Task::Exec::getSharedImageCount() const { return sharedImageCount_; }
const Task::Image* Task::Exec::getSharedImages() const { return sharedImages_; }

void Task::Exec::setImage(const void* image, uint64_t size) {
    image_     = image;
    imageSize_ = size;
}
void Task::Exec::setName(const char* value) { name_ = value; }
void Task::Exec::setArguments(const char* const* value, uint32_t count) {
    arguments_     = value;
    argumentCount_ = count;
}
void Task::Exec::setEnvironment(const char* const* value, uint32_t count) {
    environment_      = value;
    environmentCount_ = count;
}
void Task::Exec::setRuntimeState(const void* value) { runtimeState_ = value; }
void Task::Exec::setRuntimeStateSize(uint64_t value) {
    runtimeStateSize_ = value;
}
void Task::Exec::setSharedImages(const Task::Image* value, uint32_t count) {
    sharedImages_     = value;
    sharedImageCount_ = count;
}
uint32_t Task::getExitSignal() const { return exitSignal_; }
Task::Core::State Task::getExitCoreState() const {
    return static_cast<Core::State>(exitCoreState_);
}
uint64_t Task::getExitFaultThread() const { return exitFaultThread_; }
uint64_t Task::getExitVector() const { return exitVector_; }
uint64_t Task::getExitError() const { return exitError_; }
uint64_t Task::getExitRip() const { return exitRip_; }
uint64_t Task::getExitRsp() const { return exitRsp_; }
uint64_t Task::getExitAddress() const { return exitAddress_; }
uint64_t Task::getExitStarted() const { return exitStarted_; }
uint32_t Task::getExitAddressValid() const { return exitAddressValid_; }
int32_t Task::getExitSignalCode() const { return exitSignalCode_; }

int Task::refreshExitTarget(uint64_t id) {
    wire::ExitInfo value{};
    const int status = detail::TaskQuery().exitInfo(id, value);
    if (!status) {
        exitSignal_       = value.signal;
        exitCoreState_    = value.coreState;
        exitFaultThread_  = value.faultThread;
        exitVector_       = value.vector;
        exitError_        = value.error;
        exitRip_          = value.rip;
        exitRsp_          = value.rsp;
        exitAddress_      = value.address;
        exitStarted_      = value.started;
        exitAddressValid_ = value.addressValid;
        exitSignalCode_   = value.signalCode;
    }
    return status;
}
uint64_t Task::spawnImage(const Task::Spawn& image) {
    if (image.getSharedImageCount() > 32 ||
        (image.getSharedImageCount() && !image.getSharedImages()))
        return static_cast<int>(ErrorCode::InvalidArgument);
    wire::SpawnImageInfo value{};
    wire::SharedImageInfo shared[32]{};
    value.image                = image.getImage();
    value.imageSize            = image.getImageSize();
    value.name                 = image.getName();
    value.bootstrapHandle      = image.getBootstrapHandle();
    value.standardInputHandle  = image.getStandardInputHandle();
    value.standardOutputHandle = image.getStandardOutputHandle();
    value.standardErrorHandle  = image.getStandardErrorHandle();
    value.capabilities         = image.getCapabilities();
    value.parentTaskId         = image.getParentTaskId();
    value.argumentCount        = image.getArgumentCount();
    value.serviceRole          = image.getServiceRole();
    value.arguments            = image.getArguments();
    value.sharedImageCount     = image.getSharedImageCount();
    value.flags                = image.getFlags();
    value.terminalKind         = image.getTerminalKind();
    value.terminalIndex        = image.getTerminalIndex();
    for (uint32_t i = 0; i < value.sharedImageCount; ++i) {
        const auto& item = image.getSharedImages()[i];
        shared[i] = {item.getImage(), item.getImageSize(), item.getName()};
    }
    value.sharedImages = shared;
    return detail::TaskQuery().spawnImage(value);
}

}  // namespace os

namespace os {
bool Task::Core::getEnabled() const { return enabled_; }
uint64_t Task::Core::getMaximumBytes() const { return maximumBytes_; }
uint64_t Task::Core::getMaximumConcurrent() const { return maximumConcurrent_; }
uint64_t Task::Core::getTimeoutMilliseconds() const {
    return timeoutMilliseconds_;
}
uint64_t Task::Core::getId() const { return id_; }
uint64_t Task::Core::getTaskId() const { return taskId_; }
uint64_t Task::Core::getStarted() const { return started_; }
uint64_t Task::Core::getBytes() const { return bytes_; }
uint64_t Task::Core::getDeadline() const { return deadline_; }
uint32_t Task::Core::getSignal() const { return signal_; }
bool Task::Core::getTruncated() const { return truncated_; }
Task::Core::State Task::Core::getState() const { return state_; }
void Task::Core::setEnabled(bool value) { enabled_ = value; }
void Task::Core::setMaximumBytes(uint64_t value) { maximumBytes_ = value; }
void Task::Core::setMaximumConcurrent(uint64_t value) {
    maximumConcurrent_ = value;
}
void Task::Core::setTimeoutMilliseconds(uint64_t value) {
    timeoutMilliseconds_ = value;
}
uint64_t Task::Signal::getHandler() const { return handler_; }
uint64_t Task::Signal::getMask() const { return mask_; }
uint64_t Task::Signal::getFlags() const { return flags_; }
uint64_t Task::Signal::getRestorer() const { return restorer_; }
void Task::Signal::setHandler(uint64_t value) { handler_ = value; }
void Task::Signal::setMask(uint64_t value) { mask_ = value; }
void Task::Signal::setFlags(uint64_t value) { flags_ = value; }
void Task::Signal::setRestorer(uint64_t value) { restorer_ = value; }

int Task::querySignalAction(uint32_t signal, Signal& action) {
    wire::SignalAction value{};
    const int status = Task::setSignalAction(signal, nullptr, &value);
    if (!status) {
        action.setHandler(value.handler);
        action.setMask(value.mask);
        action.setFlags(value.flags);
        action.setRestorer(value.restorer);
    }
    return status;
}
int Task::setSignalAction(uint32_t signal, const Signal& action) {
    const wire::SignalAction value{action.getHandler(), action.getMask(),
                                   action.getFlags(), action.getRestorer()};
    return Task::setSignalAction(signal, &value, nullptr);
}
uint32_t Task::Credential::getMaximumGroups() const {
    return maximumSupplementaryGroups;
}
int Task::Credential::setUserIds(uint32_t real, uint32_t effective,
                                 uint32_t saved) {
    if (taskId_ != detail::TaskControl().getId())
        return static_cast<int>(ErrorCode::PermissionDenied);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::Credentials);
    request.length  = 4;
    request.data[0] = 5;
    request.data[1] = real;
    request.data[2] = effective;
    request.data[3] = saved;
    const int status =
        services::RequestChannel(CoreServiceKind::Task).result(request, reply);
    return status ? status
           : !reply.data[1]
               ? 0
               : static_cast<int>(reply.data[1] == 1
                                      ? ErrorCode::PermissionDenied
                                      : ErrorCode::InvalidArgument);
}
int Task::Credential::setGroupIds(uint32_t real, uint32_t effective,
                                  uint32_t saved) {
    if (taskId_ != detail::TaskControl().getId())
        return static_cast<int>(ErrorCode::PermissionDenied);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::Credentials);
    request.length  = 4;
    request.data[0] = 6;
    request.data[1] = real;
    request.data[2] = effective;
    request.data[3] = saved;
    const int status =
        services::RequestChannel(CoreServiceKind::Task).result(request, reply);
    return status ? status
           : !reply.data[1]
               ? 0
               : static_cast<int>(reply.data[1] == 1
                                      ? ErrorCode::PermissionDenied
                                      : ErrorCode::InvalidArgument);
}
int Task::Credential::setGroups(const uint32_t* groups, uint32_t count) {
    if (taskId_ != detail::TaskControl().getId())
        return static_cast<int>(ErrorCode::PermissionDenied);
    if (count > maximumSupplementaryGroups || (count && !groups))
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::Credentials);
    request.length  = 4 + count;
    request.data[0] = 9;
    request.data[1] = count;
    for (uint32_t index = 0; index < count; ++index)
        request.data[4 + index] = groups[index];
    const int status =
        services::RequestChannel(CoreServiceKind::Task).result(request, reply);
    return status ? status
           : !reply.data[1]
               ? 0
               : static_cast<int>(reply.data[1] == 1
                                      ? ErrorCode::PermissionDenied
                                      : ErrorCode::InvalidArgument);
}

[[noreturn]] void exitTask(int exitCode) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::ExitTask);
    request.length  = 5;
    request.data[0] = static_cast<uint64_t>(exitCode);
    services::RequestChannel(CoreServiceKind::Task).result(request, reply);
    while (true) asm volatile("ud2");
}
}  // namespace os

namespace os {
[[noreturn]] void exitThread(int exitCode) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::ExitThread);
    request.length  = 5;
    request.data[0] = static_cast<uint64_t>(exitCode);
    services::RequestChannel(CoreServiceKind::Task).result(request, reply);
    while (true) asm volatile("ud2");
}
}  // namespace os

namespace os {
uint64_t createThread(void (*entry)(), uint64_t stackPointer) {
    if (stackPointer < 16) return 0;
    // The kernel jumps into a C function; reserve its ABI return-address slot.
    stackPointer = (stackPointer & ~15ULL) - sizeof(uint64_t);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::CreateThread);
    request.length  = 5;
    request.data[0] = reinterpret_cast<uint64_t>(entry);
    request.data[1] = stackPointer;
    request.data[2] = 0;
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}
}  // namespace os

namespace os {
uint64_t createThread(void (*entry)(void*), uint64_t stackPointer,
                      void* argument) {
    if (stackPointer < 16) return 0;
    stackPointer = (stackPointer & ~15ULL) - sizeof(uint64_t);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::CreateThread);
    request.length  = 5;
    request.data[0] = reinterpret_cast<uint64_t>(entry);
    request.data[1] = stackPointer;
    request.data[2] = reinterpret_cast<uint64_t>(argument);
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}
}  // namespace os

namespace os {
int64_t forkTask() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::Fork);
    request.length = 5;
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}
}  // namespace os

namespace os {
void yield() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TaskOperation::Yield);
    request.length = 5;
    services::RequestChannel(CoreServiceKind::Task).result(request, reply);
}
}  // namespace os

namespace os {
void sleep(uint64_t milliseconds) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::Sleep);
    request.length  = 5;
    request.data[0] = milliseconds;
    services::RequestChannel(CoreServiceKind::Task).result(request, reply);
}
}  // namespace os

namespace os {
uint64_t getStandardHandle(uint32_t index) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::StandardHandle);
    request.length  = 5;
    request.data[0] = index;
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}
}  // namespace os

namespace os {
int replaceTaskImage(const Task::Exec& image) {
    if (image.getSharedImageCount() > 32 ||
        (image.getSharedImageCount() && !image.getSharedImages()))
        return static_cast<int>(ErrorCode::InvalidArgument);
    wire::ExecImageInfo value{};
    wire::SharedImageInfo shared[32]{};
    value.image            = image.getImage();
    value.imageSize        = image.getImageSize();
    value.name             = image.getName();
    value.argumentCount    = image.getArgumentCount();
    value.environmentCount = image.getEnvironmentCount();
    value.arguments        = image.getArguments();
    value.environment      = image.getEnvironment();
    value.runtimeState     = image.getRuntimeState();
    value.runtimeStateSize = image.getRuntimeStateSize();
    value.sharedImageCount = image.getSharedImageCount();
    value.sharedReserved   = 0;
    for (uint32_t i = 0; i < value.sharedImageCount; ++i) {
        const auto& item = image.getSharedImages()[i];
        shared[i] = {item.getImage(), item.getImageSize(), item.getName()};
    }
    value.sharedImages = shared;
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TaskOperation::ReplaceImage);
    request.length  = 5;
    request.data[0] = reinterpret_cast<uint64_t>(&value);
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}
}  // namespace os

namespace os {
namespace {
class ThreadLaunch {
   public:
    ThreadLaunch(void (*entry)(void*), void* argument)
        : entry_(entry), argument_(argument) {}
    void run() { entry_(argument_); }

   private:
    void (*entry_)(void*);
    void* argument_;
};
void startLibraryThread(void* storage) {
    auto* pending       = static_cast<ThreadLaunch*>(storage);
    ThreadLaunch launch = *pending;
    pending->~ThreadLaunch();
    Runtime::freeMemory(pending);
    launch.run();
    exitThread(0);
}
int taskAttribute(TaskOperation operation, uint64_t id, uint64_t a, uint64_t b,
                  uint64_t c, uint64_t d, IpcMessage& reply) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(operation);
    request.length  = 5;
    request.data[0] = id;
    request.data[1] = a;
    request.data[2] = b;
    request.data[3] = c;
    request.data[4] = d;
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}
int posixAttribute(int status, const IpcMessage& reply, uint32_t length) {
    if (status) return status;
    if (reply.length < length) return static_cast<int>(ErrorCode::IoError);
    return !reply.data[1]       ? 0
           : reply.data[1] == 1 ? static_cast<int>(ErrorCode::PermissionDenied)
           : reply.data[1] == 3 ? static_cast<int>(ErrorCode::NotFound)
                                : static_cast<int>(ErrorCode::InvalidArgument);
}
}  // namespace
detail::TaskControl::TaskControl() = default;
detail::TaskControl::TaskControl(uint64_t id) : Task(id) {}
uint64_t detail::TaskControl::getId() const { return readId(); }
int detail::TaskControl::refresh(uint64_t id) { return refreshTarget(id); }
int detail::TaskControl::next(uint64_t id) { return enumerate(id); }
int detail::TaskControl::refreshExit(uint64_t id) {
    return refreshExitTarget(id);
}
int detail::TaskControl::setGroup(uint64_t group, uint64_t id) {
    return setGroupTarget(group, id);
}
Task::~Task() { close(); }
uint64_t Task::getId() const { return id_; }
bool Task::getValid() const { return id_ || handle_.getValid(); }
int Task::getError() const { return error_; }
Task& Task::operator=(Task&& other) noexcept {
    if (this == &other) return *this;
    if (getValid()) {
        error_ = static_cast<int>(ErrorCode::Busy);
        return *this;
    }
    // Copy cached snapshots explicitly; never copy the owning handle.
    wire::TaskInfo value{};
    value.id       = other.id_;
    value.state    = other.state_;
    value.exitCode = other.exitCode_;
    Runtime::copyBytes(value.name, other.name_, sizeof(value.name));
    value.uid                     = other.uid_;
    value.gid                     = other.gid_;
    value.effectiveUid            = other.effectiveUid_;
    value.effectiveGid            = other.effectiveGid_;
    value.parentId                = other.parentId_;
    value.trustedDeviceService    = other.trustedDeviceService_;
    value.serviceRole             = other.serviceRole_;
    value.taskGroupId             = other.taskGroupId_;
    value.sessionId               = other.sessionId_;
    value.terminalForegroundGroup = other.terminalForegroundGroup_;
    value.startedNanoseconds      = other.startedNanoseconds_;
    value.cpuNanoseconds          = other.cpuNanoseconds_;
    value.terminalKind            = other.terminalKind_;
    value.terminalIndex           = other.terminalIndex_;
    value.commandLineSize         = other.commandLineSize_;
    value.managedService          = other.managedService_;
    Runtime::copyBytes(value.commandLine, other.commandLine_,
                       sizeof(value.commandLine));
    value.mainThreadId = other.mainThreadId_;
    value.nice         = other.nice_;
    value.threadCount  = other.threadCount_;
    decode(&value);
    exitSignal_       = other.exitSignal_;
    exitCoreState_    = other.exitCoreState_;
    exitFaultThread_  = other.exitFaultThread_;
    exitVector_       = other.exitVector_;
    exitError_        = other.exitError_;
    exitRip_          = other.exitRip_;
    exitRsp_          = other.exitRsp_;
    exitAddress_      = other.exitAddress_;
    exitStarted_      = other.exitStarted_;
    exitAddressValid_ = other.exitAddressValid_;
    exitSignalCode_   = other.exitSignalCode_;
    handle_           = static_cast<Ipc::Handle&&>(other.handle_);
    error_            = other.error_;
    other.id_         = 0;
    other.error_      = 0;
    return *this;
}
int Task::open(uint64_t id) {
    if (getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!id) return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage reply{};
    const int status =
        taskAttribute(TaskOperation::OpenTask, id, 0, 0, 0, 0, reply);
    if (status) return error_ = status;
    if (reply.handleCount != 1 || !reply.handles[0]) {
        for (uint32_t index = 0; index < reply.handleCount && index < 4;
             ++index) {
            Ipc::Handle unexpected(reply.handles[index]);
        }
        return error_ = static_cast<int>(ErrorCode::IoError);
    }
    handle_.adopt(reply.handles[0]);
    return error_ = refreshHandle(handle_.getValue());
}
int Task::openCurrent() {
    const auto id = readId();
    return static_cast<int64_t>(id) > 0 ? open(id)
                                        : (error_ = static_cast<int>(id));
}
int Task::create(const Spawn& image) {
    if (getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    const int64_t handle = spawnImage(image);
    if (handle <= 0)
        return error_ =
                   handle ? handle : static_cast<int>(ErrorCode::InvalidHandle);
    handle_.adopt(handle);
    return error_ = refreshHandle(handle);
}
int Task::close() {
    const int status = handle_.close();
    if (handle_.getValid()) return error_ = status;
    id_           = 0;
    return error_ = status;
}
int Task::refresh() {
    if (!getValid()) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = handle_.getValid() ? refreshHandle(handle_.getValue())
                                       : refreshTarget(id_);
}
int Task::refreshExit() {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = refreshExitTarget(id_);
}
int Task::terminate(int code) {
    if (!handle_.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = terminate(handle_.getValue(), code);
}
int Task::wait(int& code) {
    if (!handle_.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    const int result = wait(handle_.getValue(), &code);
    if (!result) {
        handle_.release();
        state_    = 2;
        exitCode_ = code;
    }
    return error_ = result;
}
int Task::next(uint64_t minimum, Task& target) {
    if (target.getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    Task candidate;
    const int result = candidate.enumerate(minimum);
    if (result) return error_ = result;
    const uint64_t id = candidate.getId();
    candidate.id_     = 0;
    return error_     = target.open(id);
}
int Task::querySignalMask(uint64_t& mask) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    return error_ = setSignalMask(0, 0, mask);
}
int Task::queryPendingSignals(uint64_t& mask) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    return error_ = getPendingSignals(mask);
}
int Task::setSignalMask(int how, uint64_t mask) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    uint64_t ignored = 0;
    return error_    = setSignalMask(how, mask, ignored);
}
int Task::setGroup(uint64_t group) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = setGroupTarget(group, id_);
}
int Task::setNice(int value) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage reply{};
    const int result =
        taskAttribute(TaskOperation::Priority, 1, 0, mainThreadId_,
                      static_cast<int64_t>(value), 0, reply);
    const int status = posixAttribute(result, reply, 3);
    if (!status) nice_ = value;
    return error_ = status;
}
int Task::queryResourceLimit(int resource, uint64_t& soft, uint64_t& hard) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage reply{};
    const int result = taskAttribute(TaskOperation::ResourceLimit, id_,
                                     resource, 0, 0, 0, reply);
    const int status = posixAttribute(result, reply, 4);
    if (!status) {
        soft = reply.data[2];
        hard = reply.data[3];
    }
    return error_ = status;
}
int Task::setResourceLimit(int resource, uint64_t soft, uint64_t hard) {
    IpcMessage reply{};
    const int result = taskAttribute(TaskOperation::ResourceLimit, id_,
                                     resource, 1, soft, hard, reply);
    return error_    = posixAttribute(result, reply, 4);
}
int Task::querySignalStack(SignalStack& stack) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    OsSignalStack value{};
    const int result = signalStack(nullptr, &value);
    if (!result) {
        stack.setAddress(reinterpret_cast<uint64_t>(value.pointer));
        stack.setSize(value.size);
        stack.setFlags(value.flags);
    }
    return error_ = result;
}
int Task::setSignalStack(const SignalStack& stack) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    const OsSignalStack value{reinterpret_cast<void*>(stack.getAddress()),
                              static_cast<int>(stack.getFlags()),
                              static_cast<size_t>(stack.getSize())};
    return error_ = signalStack(&value, nullptr);
}
int Task::queryCorePolicy(Core& policy) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    IpcMessage reply{};
    const int result =
        taskAttribute(TaskOperation::CorePolicyQuery, 0, 0, 0, 0, 0, reply);
    if (result) return error_ = result;
    if (reply.length < 5) return error_ = static_cast<int>(ErrorCode::IoError);
    policy.setEnabled(reply.data[1]);
    policy.setMaximumBytes(reply.data[2]);
    policy.setMaximumConcurrent(reply.data[3]);
    policy.setTimeoutMilliseconds(reply.data[4]);
    return error_ = 0;
}
int Task::setCorePolicy(const Core& policy) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (id_ != detail::TaskControl().getId())
        return error_ = static_cast<int>(ErrorCode::PermissionDenied);
    IpcMessage reply{};
    return error_ = taskAttribute(TaskOperation::CoreConfigure,
                                  policy.getEnabled(), policy.getMaximumBytes(),
                                  policy.getMaximumConcurrent(),
                                  policy.getTimeoutMilliseconds(), 0, reply);
}
uint64_t Task::SignalStack::getAddress() const { return address_; }
uint64_t Task::SignalStack::getSize() const { return size_; }
uint32_t Task::SignalStack::getFlags() const { return flags_; }
void Task::SignalStack::setAddress(uint64_t value) { address_ = value; }
void Task::SignalStack::setSize(uint64_t value) { size_ = value; }
void Task::SignalStack::setFlags(uint32_t value) { flags_ = value; }
uint64_t Task::Child::getId() const { return id_; }
uint32_t Task::Child::getUid() const { return uid_; }
uint32_t Task::Child::getCode() const { return code_; }
int32_t Task::Child::getStatus() const { return status_; }
uint64_t Task::ImageMapping::getBegin() const { return begin_; }
uint64_t Task::ImageMapping::getEnd() const { return end_; }
uint64_t Task::ImageMapping::getOffset() const { return offset_; }
const char* Task::ImageMapping::getPath() const { return path_; }
Task::Thread::Thread() = default;
Task::Thread::~Thread() { close(); }
Task::Thread& Task::Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        if (id_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        id_             = other.id_;
        taskId_         = other.taskId_;
        cpuNanoseconds_ = other.cpuNanoseconds_;
        nice_           = other.nice_;
        state_          = other.state_;
        error_          = other.error_;
        other.id_ = other.taskId_ = 0;
    }
    return *this;
}
int Task::Thread::open(uint64_t task, uint64_t thread) {
    if (id_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!task || !thread)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    wire::ThreadInfo value{};
    const int result = detail::TaskQuery().threadInfo(task, thread, value);
    if (result) return error_ = result;
    if (value.id != thread)
        return error_ = static_cast<int>(ErrorCode::NotFound);
    id_             = value.id;
    taskId_         = value.taskId;
    cpuNanoseconds_ = value.cpuNanoseconds;
    nice_           = value.nice;
    state_          = value.state;
    return error_   = 0;
}
int Task::Thread::openCurrent() {
    return open(detail::TaskControl().getId(),
                detail::TaskControl().getThreadId());
}
int Task::Thread::create(void (*entry)(void*), void* argument, void* stack,
                         size_t size) {
    if (id_) return error_ = static_cast<int>(ErrorCode::Busy);
    const uint64_t begin = reinterpret_cast<uint64_t>(stack);
    if (!entry || !begin || size < 4096 || begin > UINT64_MAX - size)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    void* storage = Runtime::allocateMemory(sizeof(ThreadLaunch));
    if (!storage) return error_ = static_cast<int>(ErrorCode::OutOfMemory);
    auto* launch = new (storage) ThreadLaunch(entry, argument);
    const int64_t id =
        createThread(startLibraryThread, (begin + size) & ~15ULL, launch);
    if (id <= 0) {
        launch->~ThreadLaunch();
        Runtime::freeMemory(launch);
    }

    if (id <= 0)
        return error_ = id ? id : static_cast<int>(ErrorCode::InvalidHandle);
    id_           = id;
    taskId_       = detail::TaskControl().getId();
    state_        = 0;
    return error_ = 0;
}
int Task::Thread::close() {
    id_ = taskId_ = 0;
    return error_ = 0;
}
int Task::Thread::wait(int& code) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = detail::TaskControl().waitThread(id_, code);
}
int Task::Thread::sendSignal(uint32_t signal) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = detail::TaskControl().sendThreadSignal(id_, signal);
}
bool Task::Thread::getValid() const { return id_ != 0; }
int Task::Thread::getError() const { return error_; }
int Task::Thread::setNice(int value) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage reply{};
    const int result = taskAttribute(TaskOperation::Priority, 1, 0, id_,
                                     static_cast<int64_t>(value), 0, reply);
    const int status = posixAttribute(result, reply, 3);
    if (!status) nice_ = value;
    return error_ = status;
}
Task::Session::Session() = default;
Task::Session::~Session() { close(); }
Task::Session& Task::Session::operator=(Session&& other) noexcept {
    if (this != &other) {
        if (id_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        id_            = other.id_;
        leader_        = other.leader_;
        foreground_    = other.foreground_;
        terminalKind_  = other.terminalKind_;
        terminalIndex_ = other.terminalIndex_;
        error_         = other.error_;
        other.id_      = 0;
    }
    return *this;
}
int Task::Session::refresh() {
    Task task;
    const int result = task.refreshTarget(0);
    if (result) return error_ = result;
    if (id_ && id_ != task.sessionId_)
        return error_ = static_cast<int>(ErrorCode::NotFound);
    id_            = task.sessionId_;
    leader_        = id_;
    foreground_    = task.terminalForegroundGroup_;
    terminalKind_  = task.terminalKind_;
    terminalIndex_ = task.terminalIndex_;
    task.id_       = 0;
    return error_  = 0;
}
int Task::Session::openCurrent() {
    if (id_) return error_ = static_cast<int>(ErrorCode::Busy);
    return refresh();
}
int Task::Session::create() {
    if (id_) return error_ = static_cast<int>(ErrorCode::Busy);
    const auto result = detail::TaskControl().createSession();
    if (result < 0) return error_ = result;
    id_ = result;
    return refresh();
}
int Task::Session::close() {
    id_ = leader_ = foreground_ = 0;
    return error_               = 0;
}
uint64_t Task::Session::getId() const { return id_; }
uint64_t Task::Session::getLeaderId() const { return leader_; }
uint64_t Task::Session::getForegroundGroup() const { return foreground_; }
uint32_t Task::Session::getTerminalKind() const { return terminalKind_; }
uint32_t Task::Session::getTerminalIndex() const { return terminalIndex_; }
bool Task::Session::getValid() const { return id_ != 0; }
int Task::Session::getError() const { return error_; }
int Task::Session::setForegroundGroup(uint64_t group) {
    if (!id_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage reply{};
    const int result =
        taskAttribute(TaskOperation::SetTerminalForeground, terminalKind_,
                      terminalIndex_, group, 0, 0, reply);
    if (!result) foreground_ = group;
    return error_ = result;
}
}  // namespace os
