#include "os/pty.h"

#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"

namespace {

void copyBytes(void* destination, const void* source, size_t count) {
    auto* target = static_cast<uint8_t*>(destination);
    const auto* bytes = static_cast<const uint8_t*>(source);
    for (size_t index = 0; index < count; ++index) target[index] = bytes[index];
}

bool request(os::TtyRequest operation, uint64_t handle, const void* input,
             size_t inputSize, void* output, size_t outputSize) {
    const uint64_t endpoint = os::lookupService(os::serviceRegistry(),
                                                os::ttyServiceName, 100);
    if (!endpoint) return false;
    os::IpcMessage message{};
    message.type = static_cast<uint64_t>(operation);
    message.handleCount = 1;
    message.handles[0] = handle;
    message.length = static_cast<uint32_t>((inputSize + 7) / 8);
    if (input) copyBytes(message.data, input, inputSize);
    os::IpcMessage reply{};
    const bool success = os::callService(endpoint, message, reply) == 0 &&
                         reply.data[0] == 0;
    if (success && output) copyBytes(output, &reply.data[1], outputSize);
    os::closeHandle(endpoint);
    return success;
}

}  // namespace

namespace os {

bool createPty(PtyHandles& handles) {
    handles = {};
    const uint64_t endpoint = lookupService(serviceRegistry(), ttyServiceName,
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

bool getPtyAttributes(uint64_t handle, TerminalAttributes& attributes) {
    return request(TtyRequest::GetAttributes, handle, nullptr, 0,
                   &attributes, sizeof(attributes));
}

bool setPtyAttributes(uint64_t handle,
                      const TerminalAttributes& attributes) {
    return request(TtyRequest::SetAttributes, handle, &attributes,
                   sizeof(attributes), nullptr, 0);
}

bool getPtyWindowSize(uint64_t handle, TerminalWindowSize& size) {
    return request(TtyRequest::GetWindowSize, handle, nullptr, 0,
                   &size, sizeof(size));
}

bool setPtyWindowSize(uint64_t handle, const TerminalWindowSize& size) {
    return request(TtyRequest::SetWindowSize, handle, &size, sizeof(size),
                   nullptr, 0);
}

bool setPtyForegroundGroup(uint64_t handle, uint64_t taskGroupId) {
    return request(TtyRequest::SetForegroundGroup, handle, &taskGroupId,
                   sizeof(taskGroupId), nullptr, 0);
}

bool setPtyNonblocking(uint64_t handle, bool enabled) {
    const uint64_t value = enabled ? 1 : 0;
    return request(TtyRequest::SetNonblocking, handle, &value, sizeof(value),
                   nullptr, 0);
}

}  // namespace os
