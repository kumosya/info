#include "os/process.h"

#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"

namespace os {

namespace {

constexpr uint32_t maximumArgumentCount = 8;
constexpr uint32_t executePayloadSize   = 7 * sizeof(uint64_t);

}  // namespace

ProcessHandle execute(const char* path) {
    const char* arguments[] = {path};
    return execute(path, 1, arguments);
}

ProcessHandle execute(const char* path, int argumentCount,
                      const char* const* arguments) {
    return executeWithIo(path, argumentCount, arguments,
                         getStandardHandle(0), getStandardHandle(1),
                         getStandardHandle(2));
}

ProcessHandle executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle) {
    const uint64_t registry = serviceRegistry();
    const uint64_t processService =
        lookupService(registry, taskServiceName, 1000);
    if (registry == 0) {
        log("[RUNTIME] process registry unavailable\n");
        return 0;
    }
    if (processService == 0) {
        log("[RUNTIME] process service unavailable\n");
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
    if (callService(processService, request, reply) != 0) {
        log("[RUNTIME] process service call failed\n");
        return 0;
    }
    if (reply.data[0] != 0 || reply.handleCount != 1) {
        log("[RUNTIME] process service rejected execute\n");
        return 0;
    }
    return reply.handles[0];
}

int waitProcess(ProcessHandle process, int* exitCode) {
    const uint64_t registry = serviceRegistry();
    const uint64_t processService =
        lookupService(registry, taskServiceName, 1000);
    if (process == 0 || processService == 0 || exitCode == nullptr) {
        return static_cast<int>(ErrorCode::InvalidArgument);
    }
    while (true) {
        IpcMessage request{};
        request.type        = static_cast<uint64_t>(TaskRequest::Wait);
        request.handleCount = 1;
        request.handles[0]  = process;
        IpcMessage reply{};
        if (callService(processService, request, reply) != 0 ||
            reply.data[0] > 2) {
            return static_cast<int>(ErrorCode::InvalidHandle);
        }
        if (reply.data[0] == 1) {
            *exitCode = static_cast<int>(reply.data[1]);
            closeHandle(process);
            return 0;
        }
        if (reply.data[0] == 2) {
            return static_cast<int>(ErrorCode::Interrupted);
        }
        sleep(1);
    }
}

bool listProcess(uint32_t index, ProcessListEntry& entry) {
    const uint64_t registry = serviceRegistry();
    const uint64_t service =
        lookupService(registry, taskServiceName, 1000);
    if (service == 0) return false;
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(TaskRequest::List);
    request.data[0] = index;
    request.length  = 1;
    IpcMessage reply{};
    if (callService(service, request, reply) != 0 || reply.data[0] != 0) {
        return false;
    }
    entry.id    = reply.data[1];
    entry.state = static_cast<uint32_t>(reply.data[2]);
    copyString(entry.name, reinterpret_cast<const char*>(&reply.data[3]),
               sizeof(entry.name));
    return true;
}

bool killProcess(uint64_t processId) {
    return processId != 0 && sendSignal(processId, 15) == 0;
}

}  // namespace os
