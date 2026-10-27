#include "service/console/display-session.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace os {
bool GraphicalSession::bind(uint64_t endpoint, uint64_t task,
                            uint64_t started) {
    IpcMessage request{}, reply{};
    request.type    = uint64_t(GraphicalRequest::Bind);
    request.length  = 2;
    request.data[0] = task;
    request.data[1] = started;
    return !callService(endpoint, request, reply,
                        Sys().readMonotonicTime() + 500000000ULL) &&
           reply.length && !reply.data[0];
}
bool GraphicalSession::state(uint64_t endpoint, GraphicalPhase& phase,
                             uint64_t& controller) {
    IpcMessage request{}, reply{};
    request.type = uint64_t(GraphicalRequest::State);
    if (callService(endpoint, request, reply,
                    Sys().readMonotonicTime() + 500000000ULL) ||
        reply.length != 3 || reply.data[0] ||
        reply.data[1] > uint64_t(GraphicalPhase::Desktop))
        return false;
    phase      = GraphicalPhase(reply.data[1]);
    controller = reply.data[2];
    return true;
}
bool GraphicalSession::command(uint64_t endpoint, GraphicalRequest operation,
                               uint64_t window) {
    IpcMessage request{}, reply{};
    request.type    = uint64_t(operation);
    request.length  = 1;
    request.data[0] = window;
    return !callService(endpoint, request, reply,
                        Sys().readMonotonicTime() + 500000000ULL) &&
           reply.length && !reply.data[0];
}
}  // namespace os
