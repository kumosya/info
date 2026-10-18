#include "os/task.h"
#include "os/core_service.h"

#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"

namespace os {

namespace {

constexpr uint32_t maximumArgumentCount = 64;
constexpr uint32_t executePayloadSize   = 7 * sizeof(uint64_t);

}  // namespace

TaskHandle Process::execute(const char* path) {
    const char* arguments[] = {path};
    return Process::execute(path, 1, arguments);
}

TaskHandle Process::execute(const char* path, int argumentCount,
                      const char* const* arguments) {
    return Process::executeWithIo(path, argumentCount, arguments,
                         getStandardHandle(0), getStandardHandle(1),
                         getStandardHandle(2));
}

TaskHandle Process::executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle) {
    const uint64_t registry = ServiceRegistry::serviceRegistry();
    const uint64_t taskService =
        ServiceRegistry::lookupService(registry, taskServiceName, 1000);
    if (registry == 0) {
        System::log("[RUNTIME] task registry unavailable\n");
        return 0;
    }
    if (taskService == 0) {
        System::log("[RUNTIME] task service unavailable\n");
        return 0;
    }
    if (path == nullptr || argumentCount <= 0 ||
        argumentCount > static_cast<int>(maximumArgumentCount) ||
        arguments == nullptr) {
        System::log("[RUNTIME] invalid execute arguments\n");
        return 0;
    }
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(TaskRequest::Execute);
    request.data[0] = static_cast<uint64_t>(argumentCount) |
                      (static_cast<uint64_t>(Terminal::getId() + 1) << 32);
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
    request.length      = 8;
    request.handleCount = 1;
    request.handles[0]  = registry;
    request.handleOptions[0] = ipcHandleRestrictRights;
    request.handleRights[0] = 2;
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
        System::log("[RUNTIME] task service call failed\n");
        return 0;
    }
    if (reply.data[0] != 0 || reply.handleCount != 1) {
        System::log("[RUNTIME] task service rejected execute\n");
        return 0;
    }
    return reply.handles[0];
}

int Process::waitTask(TaskHandle task, int* exitCode) {
    const uint64_t registry = ServiceRegistry::serviceRegistry();
    const uint64_t taskService =
        ServiceRegistry::lookupService(registry, taskServiceName, 1000);
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
            TaskInfo info{};
            if (Process::getTaskInfo(task, info) == 0) {
                int status = 0;
                Process::waitChild(static_cast<int64_t>(info.id), status, 1);
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

bool Process::listTask(uint32_t index, TaskListEntry& entry) {
    TaskInfo info{};
    if (Process::enumerateTask(index, info) != 0) return false;
    entry.id = info.id;
    entry.state = info.state;
    Runtime::copyString(entry.name, info.name, sizeof(entry.name));
    return true;
}

bool Process::killTask(uint64_t taskId) {
    return taskId != 0 && Process::sendSignal(taskId, 15) == 0;
}

}  // namespace os

namespace os {
int Process::queryTask(uint64_t taskId, TaskInfo& info) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::Query);
    request.length = 4; request.data[0] = taskId;
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}
int Process::enumerateTask(uint32_t index, TaskInfo& info) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::Enumerate);
    request.length = 4; request.data[0] = index;
    const int64_t result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}
int Process::setCredentials(uint64_t taskId, uint32_t uid, uint32_t gid, bool effectiveOnly) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TaskOperation::SetCredentials);
    request.length = 4;
    request.data[0] = taskId; request.data[1] = uid; request.data[2] = gid; request.data[3] = effectiveOnly;
    return CoreServiceClient::result(CoreServiceKind::Task, request, reply);
}
}  // namespace os
