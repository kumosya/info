#include "os/task.h"

#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"

namespace os {

namespace {

constexpr uint32_t maximumArgumentCount = 64;
constexpr uint32_t executePayloadSize   = 7 * sizeof(uint64_t);

}  // namespace

TaskHandle execute(const char* path) {
    const char* arguments[] = {path};
    return execute(path, 1, arguments);
}

TaskHandle execute(const char* path, int argumentCount,
                      const char* const* arguments) {
    return executeWithIo(path, argumentCount, arguments,
                         getStandardHandle(0), getStandardHandle(1),
                         getStandardHandle(2));
}

TaskHandle executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle) {
    const uint64_t registry = serviceRegistry();
    const uint64_t taskService =
        lookupService(registry, taskServiceName, 1000);
    if (registry == 0) {
        log("[RUNTIME] task registry unavailable\n");
        return 0;
    }
    if (taskService == 0) {
        log("[RUNTIME] task service unavailable\n");
        return 0;
    }
    if (path == nullptr || argumentCount <= 0 ||
        argumentCount > static_cast<int>(maximumArgumentCount) ||
        arguments == nullptr) {
        log("[RUNTIME] invalid execute arguments\n");
        return 0;
    }
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(TaskRequest::Execute);
    request.data[0] = static_cast<uint64_t>(argumentCount) |
                      (static_cast<uint64_t>(getTerminalId() + 1) << 32);
    char* payload = reinterpret_cast<char*>(&request.data[1]);
    uint32_t used = 0;
    const char* values[maximumArgumentCount + 1]{};
    values[0] = path;
    for (int index = 0; index < argumentCount; ++index) {
        values[index + 1] = arguments[index];
    }
    for (int index = 0; index <= argumentCount; ++index) {
        const char* argument = values[index];
        const size_t length  = stringLength(argument) + 1;
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
        log("[RUNTIME] task service call failed\n");
        return 0;
    }
    if (reply.data[0] != 0 || reply.handleCount != 1) {
        log("[RUNTIME] task service rejected execute\n");
        return 0;
    }
    return reply.handles[0];
}

int waitTask(TaskHandle task, int* exitCode) {
    const uint64_t registry = serviceRegistry();
    const uint64_t taskService =
        lookupService(registry, taskServiceName, 1000);
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
            closeHandle(task);
            return 0;
        }
        if (reply.data[0] == 2) {
            return static_cast<int>(ErrorCode::Interrupted);
        }
        sleep(1);
    }
}

bool listTask(uint32_t index, TaskListEntry& entry) {
    TaskInfo info{};
    if (invokeSyscall(SyscallNumber::EnumerateTask, index, reinterpret_cast<uint64_t>(&info)) != 0) return false;
    entry.id = info.id;
    entry.state = info.state;
    copyString(entry.name, info.name, sizeof(entry.name));
    return true;
}

bool killTask(uint64_t taskId) {
    return taskId != 0 && sendSignal(taskId, 15) == 0;
}

}  // namespace os
