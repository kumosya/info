#include "os/input.h"

#include "os/abi.h"

namespace os {

InputClient::~InputClient() {
    if (wakeEndpoint_ != 0) {
        closeHandle(wakeEndpoint_);
    }
    if (service_ != 0) {
        closeHandle(service_);
    }
}

bool InputClient::connect(uint64_t service) {
    if (service == 0 || service_ != 0 || wakeEndpoint_ != 0) {
        return false;
    }
    service_      = service;
    wakeEndpoint_ = createEndpoint();
    if (wakeEndpoint_ == 0) {
        return false;
    }
    IpcMessage request{};
    request.type        = static_cast<uint64_t>(InputRequest::Subscribe);
    request.handleCount = 1;
    request.handles[0]  = wakeEndpoint_;
    IpcMessage reply{};
    return callService(service_, request, reply) == 0 && reply.data[0] == 0;
}

bool InputClient::wait() {
    if (wakeEndpoint_ == 0) {
        return false;
    }
    while (true) {
        IpcMessage notification{};
        if (receiveMessage(wakeEndpoint_, notification) != 0) {
            return false;
        }
        if (notification.type == inputEventNotification) {
            return true;
        }
    }
}

bool InputClient::receive(InputEvent& event) {
    if (service_ == 0) {
        return false;
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(InputRequest::GetEvent);
    IpcMessage reply{};
    if (callService(service_, request, reply) != 0 ||
        reply.length * sizeof(uint64_t) < sizeof(event)) {
        return false;
    }
    const uint8_t* source = reinterpret_cast<const uint8_t*>(reply.data);
    uint8_t* destination  = reinterpret_cast<uint8_t*>(&event);
    for (uint32_t index = 0; index < sizeof(event); ++index) {
        destination[index] = source[index];
    }
    return true;
}

}  // namespace os
