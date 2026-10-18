#include "os/pty.h"

#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"

namespace {


class PtyProtocol {
 public:
static bool request(os::TtyRequest operation, uint64_t handle, const void* input,
             size_t inputSize, void* output, size_t outputSize) {
    const uint64_t endpoint = os::ServiceRegistry::lookupService(os::ServiceRegistry::serviceRegistry(),
                                                os::ttyServiceName, 100);
    if (!endpoint) return false;
    os::IpcMessage message{};
    message.type = static_cast<uint64_t>(operation);
    message.handleCount = 1;
    message.handles[0] = handle;
    message.length = static_cast<uint32_t>((inputSize + 7) / 8);
    if (input) os::Runtime::copyBytes(message.data, input, inputSize);
    os::IpcMessage reply{};
    const bool success = os::callService(endpoint, message, reply) == 0 &&
                         reply.data[0] == 0;
    if (success && output) os::Runtime::copyBytes(output, &reply.data[1], outputSize);
    os::closeHandle(endpoint);
    return success;
}

};
}  // namespace

namespace os {

bool Pty::createPty(PtyHandles& handles) {
    handles = {};
    const uint64_t endpoint = ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), ttyServiceName,
                                             100);
    if (!endpoint) return false;
    IpcMessage message{};
    message.type = static_cast<uint64_t>(TtyRequest::CreatePty);
    IpcMessage reply{};
    const bool success = callService(endpoint, message, reply) == 0 &&
                         reply.data[0] == 0 && reply.handleCount == 2;
    if (success) {
        handles.masterHandle = reply.handles[0];
        handles.slaveHandle = reply.handles[1];
    }
    closeHandle(endpoint);
    return success;
}

bool Pty::getPtyAttributes(uint64_t handle, TerminalAttributes& attributes) {
    return PtyProtocol::request(TtyRequest::GetAttributes, handle, nullptr, 0,
                   &attributes, sizeof(attributes));
}

bool Pty::setPtyAttributes(uint64_t handle,
                      const TerminalAttributes& attributes) {
    return PtyProtocol::request(TtyRequest::SetAttributes, handle, &attributes,
                   sizeof(attributes), nullptr, 0);
}

bool Pty::getPtyWindowSize(uint64_t handle, TerminalWindowSize& size) {
    return PtyProtocol::request(TtyRequest::GetWindowSize, handle, nullptr, 0,
                   &size, sizeof(size));
}

bool Pty::setPtyWindowSize(uint64_t handle, const TerminalWindowSize& size) {
    return PtyProtocol::request(TtyRequest::SetWindowSize, handle, &size, sizeof(size),
                   nullptr, 0);
}

bool Pty::setPtyForegroundGroup(uint64_t handle, uint64_t taskGroupId) {
    return PtyProtocol::request(TtyRequest::SetForegroundGroup, handle, &taskGroupId,
                   sizeof(taskGroupId), nullptr, 0);
}

bool Pty::setPtyNonblocking(uint64_t handle, bool enabled) {
    const uint64_t value = enabled ? 1 : 0;
    return PtyProtocol::request(TtyRequest::SetNonblocking, handle, &value, sizeof(value),
                   nullptr, 0);
}

}  // namespace os
