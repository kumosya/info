#include "service/common/kernel-requests.h"
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
bool BootConsole::report(uint64_t endpoint, const char* name, bool started,
                         int32_t error) {
    if (!endpoint || !name || !*name || Runtime::stringLength(name) >= 64)
        return false;
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TerminalRequest::BootStatus);
    request.length  = 10;
    request.data[0] = started ? 1 : 0;
    request.data[1] = static_cast<int64_t>(error);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[2]), name, 64);
    return !callService(endpoint, request, reply,
                        Sys().readMonotonicTime() + 250000000ULL) &&
           reply.length >= 1 && !reply.data[0];
}

bool BootConsole::finish(uint64_t endpoint, uint32_t terminal) {
    if (!endpoint || terminal > graphicalTerminalId) return false;
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(TerminalRequest::BootComplete);
    request.length  = 1;
    request.data[0] = terminal;
    return !callService(endpoint, request, reply,
                        Sys().readMonotonicTime() + 3000000000ULL) &&
           reply.length >= 1 && !reply.data[0];
}

bool BootConsole::state(uint64_t endpoint, bool& booting,
                        uint32_t& preferredTerminal) {
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TerminalRequest::GetBootState);
    if (!endpoint ||
        callService(endpoint, request, reply,
                    Sys().readMonotonicTime() + 250000000ULL) ||
        reply.length != 3 || reply.data[0] ||
        reply.data[2] > graphicalTerminalId)
        return false;
    booting           = reply.data[1] != 0;
    preferredTerminal = static_cast<uint32_t>(reply.data[2]);
    return true;
}

bool DisplayHandoff::setGraphicalActive(uint64_t windowService, bool active) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(GraphicalDisplayRequest::SetActive);
    request.data[0] = active ? 1 : 0;
    request.length  = 1;
    if (callService(windowService, request, reply,
                    Sys().readMonotonicTime() + 1000000000ULL) == 0 &&
        reply.length >= 1 && reply.data[0] == 0)
        return true;
    // Reconcile a lost acknowledgement before console commits its own active
    // TTY.
    request      = {};
    reply        = {};
    request.type = static_cast<uint64_t>(GraphicalDisplayRequest::GetActive);
    return !callService(windowService, request, reply,
                        Sys().readMonotonicTime() + 1000000000ULL) &&
           reply.length == 2 && !reply.data[0] &&
           reply.data[1] == uint64_t(active);
}

}  // namespace os
