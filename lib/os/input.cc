#include "service/common/queries.h"
#include <os/input.h>

#include <os/ipc.h>
#include <os/service.h>
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
namespace os {
namespace {
int inputError(ErrorCode value) { return static_cast<int>(value); }
bool disconnected(int status) {
    return status == inputError(ErrorCode::InvalidHandle) ||
           status == inputError(ErrorCode::EndpointClosed) ||
           status == inputError(ErrorCode::PeerDied);
}
int releaseInputEndpoint(uint64_t& endpoint) {
    if (!endpoint) return 0;
    Ipc::Handle reference(endpoint, false);
    const int status = reference.close();
    if (status && !disconnected(status)) return status;
    endpoint = 0;
    return 0;
}
int inputResult(uint64_t endpoint, IpcMessage& request, IpcMessage& reply) {
    const int status = callService(endpoint, request, reply);
    if (status) return status;
    if (!reply.length || static_cast<int64_t>(reply.data[0]) > 0)
        return inputError(ErrorCode::IoError);
    return static_cast<int>(reply.data[0]);
}
}  // namespace
Input::Input() = default;
Input::~Input() { close(); }
int Input::open() {
    if (service_ || wakeEndpoint_) return error_ = inputError(ErrorCode::Busy);
    service_ = detail::RegistryClient().lookup(inputServiceName, 5000);
    if (!service_) return error_ = inputError(ErrorCode::NotFound);
    const int64_t wake = static_cast<int64_t>(detail::EndpointControl().create());
    if (wake <= 0) {
        releaseInputEndpoint(service_);
        return error_ = wake ? static_cast<int>(wake)
                             : inputError(ErrorCode::OutOfMemory);
    }
    wakeEndpoint_ = wake;
    Ipc::Handle wakeReference(wakeEndpoint_, false);
    int status = wakeReference.setFlags(handleFlagCloseOnExec);
    if (status) {
        close();
        return error_ = status;
    }
    IpcMessage request{}, reply{};
    request.type          = static_cast<uint64_t>(InputRequest::Subscribe);
    request.handleCount   = 1;
    request.handles[0]    = wakeEndpoint_;
    subscriptionPossible_ = true;
    status                = inputResult(service_, request, reply);
    if (status) {
        if (reply.length) subscriptionPossible_ = false;
        close();
        return error_ = status;
    }
    connected_ = true;
    status     = refresh();
    if (status) {
        close();
        return error_ = status;
    }
    return error_ = 0;
}
int Input::close() {
    if (subscriptionPossible_) {
        IpcMessage request{}, reply{};
        request.type        = static_cast<uint64_t>(InputRequest::Unsubscribe);
        request.handleCount = 1;
        request.handles[0]  = wakeEndpoint_;
        const int status    = inputResult(service_, request, reply);
        if (status && !disconnected(status)) return error_ = status;
        subscriptionPossible_ = connected_ = false;
    }
    int status = releaseInputEndpoint(wakeEndpoint_);
    if (status) return error_ = status;
    status = releaseInputEndpoint(service_);
    if (status) return error_ = status;
    connected_ = false;
    Runtime::clearMemory(keys_, sizeof(keys_));
    modifiers_ = buttons_ = 0;
    return error_         = 0;
}
int Input::refresh() {
    if (!connected_) return error_ = inputError(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(InputRequest::GetState);
    const int status = inputResult(service_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 2 + sizeof(wire::InputState) / 8)
        return error_ = inputError(ErrorCode::IoError);
    wire::InputState state{};
    Runtime::copyBytes(&state, &reply.data[2], sizeof(state));
    if (state.modifiers > 63 || state.buttons > 31)
        return error_ = inputError(ErrorCode::IoError);
    for (unsigned i = 0; i < 128; ++i)
        if (state.keys[i] > 1 ||
            (i > static_cast<uint32_t>(Key::KeypadEnter) && state.keys[i]))
            return error_ = inputError(ErrorCode::IoError);
    Runtime::copyBytes(keys_, state.keys, sizeof(keys_));
    modifiers_    = state.modifiers;
    buttons_      = state.buttons;
    return error_ = 0;
}
int Input::wait(uint64_t deadline) {
    if (!connected_) return error_ = inputError(ErrorCode::InvalidHandle);
    while (true) {
        IpcMessage notification{};
        const int status =
            receiveMessage(wakeEndpoint_, notification, deadline);
        if (status) return error_ = status;
        for (uint32_t i = 0; i < notification.handleCount && i < 4; ++i) {
            Ipc::Handle reference(notification.handles[i]);
            reference.close();
        }
        if (notification.type == inputEventNotification) return error_ = 0;
    }
}
bool Input::receive(Event& event) {
    if (!connected_) {
        error_ = inputError(ErrorCode::InvalidHandle);
        return false;
    }
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(InputRequest::GetEvent);
    int status   = callService(service_, request, reply);
    if (status) {
        error_ = status;
        return false;
    }
    if (reply.length == 1 && static_cast<int64_t>(reply.data[0]) < 0) {
        error_ = static_cast<int>(reply.data[0]);
        return false;
    }
    if (reply.length < sizeof(wire::InputEvent) / 8) {
        error_ = inputError(ErrorCode::IoError);
        return false;
    }
    wire::InputEvent record{};
    Runtime::copyBytes(&record, reply.data, sizeof(record));
    if (record.type < Event::Type::KeyDown ||
        record.type > Event::Type::MouseWheel) {
        error_ = inputError(ErrorCode::IoError);
        return false;
    }
    Event next;
    next.type_      = record.type;
    next.timestamp_ = record.timestamp;
    if (record.type == Event::Type::KeyDown ||
        record.type == Event::Type::KeyUp) {
        const uint32_t key = static_cast<uint32_t>(record.key.keyCode);
        if (!key || key > static_cast<uint32_t>(Key::KeypadEnter) ||
            record.key.modifiers > 63 || record.key.pressed > 1 ||
            static_cast<bool>(record.key.pressed) !=
                (record.type == Event::Type::KeyDown)) {
            error_ = inputError(ErrorCode::IoError);
            return false;
        }
        next.key_       = record.key.keyCode;
        next.modifiers_ = record.key.modifiers;
        next.pressed_   = record.key.pressed;
        keys_[key]      = record.key.pressed;
        modifiers_      = record.key.modifiers;
    } else {
        if (record.mouse.buttons > 31) {
            error_ = inputError(ErrorCode::IoError);
            return false;
        }
        next.deltaX_  = record.mouse.deltaX;
        next.deltaY_  = record.mouse.deltaY;
        next.wheel_   = record.mouse.wheel;
        next.buttons_ = record.mouse.buttons;
        buttons_      = record.mouse.buttons;
    }
    event  = next;
    error_ = 0;
    return true;
}
bool Input::getConnected() const { return connected_; }
bool Input::getKeyPressed(Key key) const {
    const auto index = static_cast<uint32_t>(key);
    return connected_ && index < 128 && keys_[index];
}
uint32_t Input::getModifiers() const { return modifiers_; }
uint32_t Input::getButtons() const { return buttons_; }
bool Input::getValid() const { return connected_; }
int Input::getError() const { return error_; }
Input::Event::Type Input::Event::getType() const { return type_; }
uint64_t Input::Event::getTimestamp() const { return timestamp_; }
Input::Key Input::Event::getKeyCode() const { return key_; }
uint32_t Input::Event::getModifiers() const { return modifiers_; }
bool Input::Event::getPressed() const { return pressed_; }
int32_t Input::Event::getDeltaX() const { return deltaX_; }
int32_t Input::Event::getDeltaY() const { return deltaY_; }
int32_t Input::Event::getWheel() const { return wheel_; }
uint32_t Input::Event::getButtons() const { return buttons_; }
}  // namespace os
