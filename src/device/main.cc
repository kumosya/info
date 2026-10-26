#include <os/device.h>
#include <os/input.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/sync.h>
#include <os/task.h>
#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/lifecycle.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"

namespace {

enum class DeviceCategory : uint8_t {
    Pci,
    Storage,
    Keyboard,
    Mouse,
    Display,
    Network,
    Timer,
    Serial
};

enum class DriverState : uint8_t {
    Created,
    Initialized,
    Running,
    Stopping,
    Stopped
};

constexpr uint64_t irqNotification = 0x495251;

class InputQueue {
   public:
    bool enqueue(const os::wire::InputEvent& event) {
        os::SpinLock::Guard guard(lock_);
        if (event.type == os::Input::Event::Type::KeyDown ||
            event.type == os::Input::Event::Type::KeyUp) {
            const uint32_t key = static_cast<uint32_t>(event.key.keyCode);
            if (key < sizeof(keys_)) keys_[key] = event.key.pressed;
            modifiers_ = event.key.modifiers;
        } else
            buttons_ = event.mouse.buttons;
        if (nextSequence_ != firstSequence_ && canCoalesceLast()) {
            os::wire::InputEvent& previous =
                events_[(nextSequence_ - 1) % capacity];
            if (event.type == os::Input::Event::Type::MouseMove &&
                previous.type == os::Input::Event::Type::MouseMove) {
                previous.mouse.deltaX += event.mouse.deltaX;
                previous.mouse.deltaY += event.mouse.deltaY;
                previous.timestamp = event.timestamp;
                return true;
            }
            if (event.type == os::Input::Event::Type::MouseWheel &&
                previous.type == os::Input::Event::Type::MouseWheel) {
                previous.mouse.wheel += event.mouse.wheel;
                previous.timestamp = event.timestamp;
                return true;
            }
        }
        if (nextSequence_ - firstSequence_ == capacity) {
            ++firstSequence_;
        }
        events_[nextSequence_ % capacity] = event;
        ++nextSequence_;
        return true;
    }

    bool dequeue(uint64_t taskId, os::wire::InputEvent& event) {
        os::SpinLock::Guard guard(lock_);
        Subscriber* subscriber = findSubscriber(taskId);
        if (subscriber == nullptr) {
            return false;
        }
        if (subscriber->nextSequence < firstSequence_) {
            subscriber->nextSequence = firstSequence_;
        }
        if (subscriber->nextSequence == nextSequence_) {
            subscriber->notified = false;
            return false;
        }
        event = events_[subscriber->nextSequence % capacity];
        ++subscriber->nextSequence;
        if (subscriber->nextSequence == nextSequence_) {
            subscriber->notified = false;
        }
        return true;
    }

    bool subscribe(uint64_t taskId, uint64_t wakeEndpoint) {
        if (taskId == 0 || wakeEndpoint == 0) {
            return false;
        }
        const uint64_t identity = os::Ipc::Handle(wakeEndpoint, false).queryIdentity();
        if (!identity || static_cast<int64_t>(identity) < 0) return false;
        os::SpinLock::Guard guard(lock_);
        Subscriber* existing = findSubscriber(taskId);
        if (existing != nullptr) {
            return existing->wakeEndpoint == wakeEndpoint;
        }
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.taskId == 0) {
                subscriber = {taskId, nextSequence_, wakeEndpoint,
                              false,  identity,      false};
                return true;
            }
        }
        return false;
    }

    uint32_t collectWakeEndpoints(uint64_t* endpoints, uint32_t capacity) {
        if (endpoints == nullptr || capacity == 0) {
            return 0;
        }
        os::SpinLock::Guard guard(lock_);
        uint32_t count = 0;
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.taskId == 0 || subscriber.wakeEndpoint == 0 ||
                subscriber.notified || subscriber.closing) {
                continue;
            }
            if (subscriber.nextSequence < firstSequence_) {
                subscriber.nextSequence = firstSequence_;
            }
            if (subscriber.nextSequence == nextSequence_) {
                continue;
            }
            subscriber.notified = true;
            endpoints[count++]  = subscriber.wakeEndpoint;
            if (count == capacity) {
                break;
            }
        }
        return count;
    }

    bool unsubscribe(uint64_t wakeEndpoint) {
        os::SpinLock::Guard guard(lock_);
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.wakeEndpoint == wakeEndpoint) {
                subscriber = {};
                return true;
            }
        }
        return false;
    }

    uint64_t removeSubscription(uint64_t taskId, uint64_t identity) {
        os::SpinLock::Guard guard(lock_);
        auto* subscriber = findSubscriber(taskId);
        if (!subscriber || subscriber->identity != identity) return 0;
        subscriber->closing = true;
        return subscriber->wakeEndpoint;
    }
    void snapshot(os::wire::InputState& state) {
        os::SpinLock::Guard guard(lock_);
        state.modifiers = modifiers_;
        state.buttons   = buttons_;
        os::Runtime::copyBytes(state.keys, keys_, sizeof(keys_));
    }
    void retryNotification(uint64_t wakeEndpoint) {
        os::SpinLock::Guard guard(lock_);
        for (auto& subscriber : subscribers_)
            if (subscriber.wakeEndpoint == wakeEndpoint)
                subscriber.notified = false;
    }

    uint32_t size(uint64_t taskId) {
        os::SpinLock::Guard guard(lock_);
        Subscriber* subscriber = findSubscriber(taskId);
        if (subscriber == nullptr) {
            return 0;
        }
        const uint64_t sequence = subscriber->nextSequence < firstSequence_
                                      ? firstSequence_
                                      : subscriber->nextSequence;
        return static_cast<uint32_t>(nextSequence_ - sequence);
    }

   private:
    struct Subscriber {
        uint64_t taskId;
        uint64_t nextSequence;
        uint64_t wakeEndpoint;
        bool notified;
        uint64_t identity;
        bool closing;
    };

    bool canCoalesceLast() const {
        for (const Subscriber& subscriber : subscribers_) {
            if (subscriber.taskId != 0 &&
                subscriber.nextSequence == nextSequence_) {
                return false;
            }
        }
        return true;
    }

    Subscriber* findSubscriber(uint64_t taskId) {
        if (taskId == 0) {
            return nullptr;
        }
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.taskId == taskId) {
                return &subscriber;
            }
        }
        return nullptr;
    }

    static constexpr uint32_t capacity = 256;
    uint8_t keys_[128]{};
    uint32_t modifiers_{}, buttons_{};
    os::wire::InputEvent events_[capacity];
    Subscriber subscribers_[4]{};
    uint64_t firstSequence_ = 0;
    uint64_t nextSequence_  = 0;
    os::SpinLock lock_;
};

class Ps2Controller {
   public:
    bool init() {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(os::DeviceOperation::InitializeInput);
        request.length = 5;
        const int64_t result =
            services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply);
        if (result < 3 || result > 4) return false;
        packetSize_ = result;
        return true;
    }
    bool readInterruptByte(uint8_t& value, bool& mouse) {
        os::IpcMessage request{}, reply{};
        request.type   = static_cast<uint64_t>(os::DeviceOperation::ReadInput);
        request.length = 5;
        if (services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply))
            return false;
        value = reply.data[1];
        mouse = reply.data[2] != 0;
        return true;
    }
    uint8_t mousePacketSize() const { return packetSize_; }

   private:
    uint8_t packetSize_ = 3;
};

class KeyboardDriver {
   public:
    void consume(uint8_t scanCode, InputQueue& queue) {
        if (scanCode == 0xe0) {
            extended_ = true;
            return;
        }
        const bool pressed       = (scanCode & 0x80U) == 0;
        const uint8_t code       = scanCode & 0x7fU;
        const os::Input::Key key = translate(code, extended_);
        extended_                = false;
        if (key == os::Input::Key::Unknown) {
            return;
        }
        updateModifiers(key, pressed);
        os::wire::InputEvent event{};
        event.type          = pressed ? os::Input::Event::Type::KeyDown
                                      : os::Input::Event::Type::KeyUp;
        event.timestamp     = os::Sys().readMonotonicTime();
        event.key.keyCode   = key;
        event.key.modifiers = modifiers_;
        event.key.pressed   = pressed;
        queue.enqueue(event);
    }

   private:
    static os::Input::Key translate(uint8_t code, bool extended) {
        if (extended) {
            switch (code) {
                case 0x1d:
                    return os::Input::Key::RightControl;
                case 0x38:
                    return os::Input::Key::RightAlt;
                case 0x1c:
                    return os::Input::Key::KeypadEnter;
                case 0x35:
                    return os::Input::Key::KeypadDivide;
                case 0x47:
                    return os::Input::Key::Home;
                case 0x48:
                    return os::Input::Key::Up;
                case 0x49:
                    return os::Input::Key::PageUp;
                case 0x50:
                    return os::Input::Key::Down;
                case 0x4f:
                    return os::Input::Key::End;
                case 0x51:
                    return os::Input::Key::PageDown;
                case 0x52:
                    return os::Input::Key::Insert;
                case 0x53:
                    return os::Input::Key::Delete;
                case 0x4b:
                    return os::Input::Key::Left;
                case 0x4d:
                    return os::Input::Key::Right;
                default:
                    return os::Input::Key::Unknown;
            }
        }
        constexpr os::Input::Key scanCodes[] = {os::Input::Key::Unknown,
                                                os::Input::Key::Escape,
                                                os::Input::Key::Digit1,
                                                os::Input::Key::Digit2,
                                                os::Input::Key::Digit3,
                                                os::Input::Key::Digit4,
                                                os::Input::Key::Digit5,
                                                os::Input::Key::Digit6,
                                                os::Input::Key::Digit7,
                                                os::Input::Key::Digit8,
                                                os::Input::Key::Digit9,
                                                os::Input::Key::Digit0,
                                                os::Input::Key::Minus,
                                                os::Input::Key::Equal,
                                                os::Input::Key::Backspace,
                                                os::Input::Key::Tab,
                                                os::Input::Key::Q,
                                                os::Input::Key::W,
                                                os::Input::Key::E,
                                                os::Input::Key::R,
                                                os::Input::Key::T,
                                                os::Input::Key::Y,
                                                os::Input::Key::U,
                                                os::Input::Key::I,
                                                os::Input::Key::O,
                                                os::Input::Key::P,
                                                os::Input::Key::LeftBracket,
                                                os::Input::Key::RightBracket,
                                                os::Input::Key::Enter,
                                                os::Input::Key::LeftControl,
                                                os::Input::Key::A,
                                                os::Input::Key::S,
                                                os::Input::Key::D,
                                                os::Input::Key::F,
                                                os::Input::Key::G,
                                                os::Input::Key::H,
                                                os::Input::Key::J,
                                                os::Input::Key::K,
                                                os::Input::Key::L,
                                                os::Input::Key::Semicolon,
                                                os::Input::Key::Apostrophe,
                                                os::Input::Key::Grave,
                                                os::Input::Key::LeftShift,
                                                os::Input::Key::Backslash,
                                                os::Input::Key::Z,
                                                os::Input::Key::X,
                                                os::Input::Key::C,
                                                os::Input::Key::V,
                                                os::Input::Key::B,
                                                os::Input::Key::N,
                                                os::Input::Key::M};
        if (code < sizeof(scanCodes) / sizeof(scanCodes[0])) {
            return scanCodes[code];
        }
        switch (code) {
            case 0x0c:
                return os::Input::Key::Minus;
            case 0x0d:
                return os::Input::Key::Equal;
            case 0x1a:
                return os::Input::Key::LeftBracket;
            case 0x1b:
                return os::Input::Key::RightBracket;
            case 0x36:
                return os::Input::Key::RightShift;
            case 0x38:
                return os::Input::Key::LeftAlt;
            case 0x39:
                return os::Input::Key::Space;
            case 0x3a:
                return os::Input::Key::CapsLock;
            case 0x27:
                return os::Input::Key::Semicolon;
            case 0x28:
                return os::Input::Key::Apostrophe;
            case 0x29:
                return os::Input::Key::Grave;
            case 0x2b:
                return os::Input::Key::Backslash;
            case 0x33:
                return os::Input::Key::Comma;
            case 0x34:
                return os::Input::Key::Period;
            case 0x35:
                return os::Input::Key::Slash;
            case 0x3b:
                return os::Input::Key::F1;
            case 0x3c:
                return os::Input::Key::F2;
            case 0x3d:
                return os::Input::Key::F3;
            case 0x3e:
                return os::Input::Key::F4;
            case 0x3f:
                return os::Input::Key::F5;
            case 0x40:
                return os::Input::Key::F6;
            case 0x41:
                return os::Input::Key::F7;
            case 0x42:
                return os::Input::Key::F8;
            case 0x43:
                return os::Input::Key::F9;
            case 0x44:
                return os::Input::Key::F10;
            case 0x57:
                return os::Input::Key::F11;
            case 0x58:
                return os::Input::Key::F12;
            case 0x45:
                return os::Input::Key::NumLock;
            case 0x46:
                return os::Input::Key::ScrollLock;
            case 0x47:
                return os::Input::Key::Keypad7;
            case 0x48:
                return os::Input::Key::Keypad8;
            case 0x49:
                return os::Input::Key::Keypad9;
            case 0x4a:
                return os::Input::Key::KeypadMinus;
            case 0x4b:
                return os::Input::Key::Keypad4;
            case 0x4c:
                return os::Input::Key::Keypad5;
            case 0x4d:
                return os::Input::Key::Keypad6;
            case 0x4e:
                return os::Input::Key::KeypadPlus;
            case 0x4f:
                return os::Input::Key::Keypad1;
            case 0x50:
                return os::Input::Key::Keypad2;
            case 0x51:
                return os::Input::Key::Keypad3;
            case 0x52:
                return os::Input::Key::Keypad0;
            case 0x53:
                return os::Input::Key::KeypadDecimal;
            case 0x37:
                return os::Input::Key::KeypadMultiply;
            default:
                return os::Input::Key::Unknown;
        }
    }

    void updateModifiers(os::Input::Key key, bool pressed) {
        uint32_t mask = 0;
        if (key == os::Input::Key::LeftShift ||
            key == os::Input::Key::RightShift) {
            mask = os::Input::Modifier::Shift;
        } else if (key == os::Input::Key::LeftControl ||
                   key == os::Input::Key::RightControl) {
            mask = os::Input::Modifier::Control;
        } else if (key == os::Input::Key::LeftAlt ||
                   key == os::Input::Key::RightAlt) {
            mask = os::Input::Modifier::Alt;
        } else if (key == os::Input::Key::CapsLock && pressed) {
            modifiers_ ^= os::Input::Modifier::CapsLock;
            return;
        } else if (key == os::Input::Key::NumLock && pressed) {
            modifiers_ ^= os::Input::Modifier::NumLock;
            return;
        } else if (key == os::Input::Key::ScrollLock && pressed) {
            modifiers_ ^= os::Input::Modifier::ScrollLock;
            return;
        }
        if (pressed) {
            modifiers_ |= mask;
        } else {
            modifiers_ &= ~mask;
        }
    }

    uint32_t modifiers_ = 0;
    bool extended_      = false;
};

class MouseDriver {
   public:
    void init(uint8_t packetSize) { packetSize_ = packetSize; }

    void consume(uint8_t value, InputQueue& queue) {
        if (packetIndex_ == 0 && (value & 0x08U) == 0) {
            return;
        }
        packet_[packetIndex_++] = value;
        if (packetIndex_ != packetSize_) {
            return;
        }
        packetIndex_         = 0;
        const int32_t deltaX = accelerate(static_cast<int8_t>(packet_[1]));
        const int32_t deltaY = -accelerate(static_cast<int8_t>(packet_[2]));
        const int32_t wheel =
            packetSize_ == 4 ? static_cast<int8_t>(packet_[3] << 4) >> 4 : 0;
        const uint32_t buttons = packet_[0] & 0x07U;
        if (deltaX != 0 || deltaY != 0) {
            emit(queue, os::Input::Event::Type::MouseMove, deltaX, deltaY, 0,
                 buttons);
        }
        if (wheel != 0) {
            emit(queue, os::Input::Event::Type::MouseWheel, 0, 0, wheel,
                 buttons);
        }
        if ((buttons & ~buttons_) != 0) {
            emit(queue, os::Input::Event::Type::MouseButtonDown, 0, 0, 0,
                 buttons);
        }
        if ((buttons_ & ~buttons) != 0) {
            emit(queue, os::Input::Event::Type::MouseButtonUp, 0, 0, 0,
                 buttons);
        }
        if (buttons != buttons_) {
            buttons_ = buttons;
        }
    }

   private:
    static int32_t accelerate(int32_t delta) {
        const int32_t magnitude = delta < 0 ? -delta : delta;
        const int32_t scale     = magnitude >= 8 ? 4 : magnitude >= 3 ? 3 : 1;
        return delta * scale;
    }

    static void emit(InputQueue& queue, os::Input::Event::Type type,
                     int32_t deltaX, int32_t deltaY, int32_t wheel,
                     uint32_t buttons) {
        os::wire::InputEvent event{};
        event.type      = type;
        event.timestamp = os::Sys().readMonotonicTime();
        event.mouse     = {deltaX, deltaY, wheel, buttons};
        queue.enqueue(event);
    }

    uint8_t packet_[4]{};
    uint8_t packetIndex_ = 0;
    uint8_t packetSize_  = 3;
    uint32_t buttons_    = 0;
};

class InputService {
   public:
    bool bindInputIrq(uint8_t irq) {
        os::IpcMessage request{}, reply{};
        request.type    = static_cast<uint64_t>(os::DeviceOperation::BindIrq);
        request.length  = 5;
        request.data[0] = irq;
        request.data[1] = irqEndpoint_;
        const int64_t handle =
            services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply);
        if (handle <= 0) return false;
        inputBindings_[irq == 1 ? 0 : 1].reset(handle, true);
        return true;
    }
    bool init(uint64_t registry) {
        requestEndpoint_ = os::detail::EndpointControl().create();
        irqEndpoint_     = os::detail::EndpointControl().create();
        if (requestEndpoint_ == 0 || irqEndpoint_ == 0 || !controller_.init() ||
            !bindInputIrq(1) || !bindInputIrq(12) ||
            !os::detail::RegistryClient(registry).registerName(os::inputServiceName,
                                                        requestEndpoint_)) {
            return false;
        }
        mouse_.init(controller_.mousePacketSize());
        return true;
    }

    [[noreturn]] void run() {
        while (true) {
            os::IpcMessage request{};
            if (os::receiveMessage(
                    requestEndpoint_, request,
                    os::Sys().readMonotonicTime() + 10000000) != 0) {
                notifySubscribers();
                continue;
            }
            os::IpcMessage reply{};
            os::wire::TaskInfo caller{};
            if (os::detail::TaskQuery().query(request.senderPid, caller) ||
                !caller.managedService ||
                (caller.serviceRole != uint32_t(os::ServiceRole::Console) &&
                 caller.serviceRole != uint32_t(os::ServiceRole::Window))) {
                for (uint32_t i = 0; i < request.handleCount; ++i)
                    os::closeHandle(request.handles[i]);
                reply.length = 1;
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::PermissionDenied);
                os::replyMessage(requestEndpoint_, request.replyToken, reply);
                continue;
            }
            if (request.type ==
                static_cast<uint64_t>(os::InputRequest::GetState)) {
                reply.data[0] = 0;
                reply.data[1] = queue_.size(request.senderPid);
                os::wire::InputState state{};
                queue_.snapshot(state);
                os::Runtime::copyBytes(&reply.data[2], &state, sizeof(state));
                reply.length = 2 + sizeof(state) / 8;
            } else if (request.type ==
                       static_cast<uint64_t>(os::InputRequest::GetEvent)) {
                os::wire::InputEvent event{};
                if (!queue_.dequeue(request.senderPid, event)) {
                    reply.data[0] =
                        static_cast<uint64_t>(os::ErrorCode::WouldBlock);
                    reply.length = 1;
                } else {
                    const uint8_t* source =
                        reinterpret_cast<const uint8_t*>(&event);
                    for (uint32_t index = 0; index < sizeof(event); ++index) {
                        reinterpret_cast<uint8_t*>(reply.data)[index] =
                            source[index];
                    }
                    reply.length = sizeof(event) / sizeof(uint64_t);
                }
            } else if (request.type ==
                       static_cast<uint64_t>(os::InputRequest::Subscribe)) {
                const bool subscribed =
                    request.handleCount == 1 &&
                    queue_.subscribe(request.senderPid, request.handles[0]);
                reply.data[0] = subscribed ? 0 : 1;
                reply.length  = 1;
                if (!subscribed && request.handleCount == 1) {
                    os::closeHandle(request.handles[0]);
                }
            } else if (request.type ==
                       static_cast<uint64_t>(os::InputRequest::Unsubscribe)) {
                reply.length = 1;
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                if (request.handleCount == 1) {
                    const uint64_t identity =
                        os::Ipc::Handle(request.handles[0], false).queryIdentity();
                    if (identity && static_cast<int64_t>(identity) > 0) {
                        const uint64_t endpoint = queue_.removeSubscription(
                            request.senderPid, identity);
                        if (!endpoint)
                            reply.data[0] = 0;
                        else if (os::closeHandle(endpoint)) {
                            queue_.unsubscribe(endpoint);
                            reply.data[0] = 0;
                        } else
                            reply.data[0] =
                                static_cast<int64_t>(os::ErrorCode::IoError);
                    }
                }
                for (uint32_t i = 0; i < request.handleCount; ++i)
                    os::closeHandle(request.handles[i]);
            } else {
                reply.data[0] = 1;
                reply.length  = 1;
            }
            os::replyMessage(requestEndpoint_, request.replyToken, reply);
            notifySubscribers();
        }
    }

    [[noreturn]] void runIrq() {
        while (true) {
            waitForInterrupt();
        }
    }

   private:
    void waitForInterrupt() {
        os::IpcMessage notification{};
        if (os::receiveMessage(irqEndpoint_, notification) != 0 ||
            notification.type != irqNotification) {
            return;
        }
        // One IRQ can represent several bytes already buffered by the 8042.
        // Drain the controller so keyboard traffic cannot stall mouse packets.
        for (uint32_t index = 0;; ++index) {
            uint8_t value;
            bool mouseData;
            if (!controller_.readInterruptByte(value, mouseData)) {
                break;
            }
            if (mouseData) {
                mouse_.consume(value, queue_);
            } else {
                keyboard_.consume(value, queue_);
            }
            // Do not leave the controller's output buffer asserted after a
            // burst; an edge-triggered IRQ need not fire again for those bytes.
            if ((index & 63U) == 63U) os::yield();
        }
        notifySubscribers();
    }

    void notifySubscribers() {
        uint64_t endpoints[4]{};
        const uint32_t count = queue_.collectWakeEndpoints(endpoints, 4);
        for (uint32_t index = 0; index < count; ++index) {
            os::IpcMessage notification{};
            notification.type = os::inputEventNotification;
            const int result =
                os::sendMessage(endpoints[index], notification, 0);
            if (result == static_cast<int>(os::ErrorCode::WouldBlock) ||
                result == static_cast<int>(os::ErrorCode::Timeout) ||
                result == static_cast<int>(os::ErrorCode::Interrupted)) {
                queue_.retryNotification(endpoints[index]);
            } else if (result && queue_.unsubscribe(endpoints[index])) {
                os::closeHandle(endpoints[index]);
            }
        }
    }

    uint64_t requestEndpoint_ = 0;
    uint64_t irqEndpoint_     = 0;
    os::Ipc::Handle inputBindings_[2];
    Ps2Controller controller_;
    KeyboardDriver keyboard_;
    MouseDriver mouse_;
    InputQueue queue_;
};

class Device {
   public:
    void init(const os::Device::Pci& function) {
        function_ = function;
        category_ = classify(function);
    }

    const os::Device::Pci& function() const { return function_; }
    DeviceCategory category() const { return category_; }

   private:
    static DeviceCategory classify(const os::Device::Pci& function) {
        if (function.getClassCode() == 0x01) {
            return DeviceCategory::Storage;
        }
        if (function.getClassCode() == 0x02) {
            return DeviceCategory::Network;
        }
        if (function.getClassCode() == 0x03) {
            return DeviceCategory::Display;
        }
        return DeviceCategory::Pci;
    }

    os::Device::Pci function_;
    DeviceCategory category_;
};

class Driver {
   public:
    void create(Device* device) {
        device_ = device;
        state_  = DriverState::Created;
    }

    bool init() {
        if (device_ == nullptr || state_ != DriverState::Created) {
            return false;
        }
        state_ = DriverState::Initialized;
        return true;
    }

    bool start() {
        if (state_ != DriverState::Initialized) {
            return false;
        }
        state_ = DriverState::Running;
        return true;
    }

    bool stop() {
        if (state_ != DriverState::Running) {
            return false;
        }
        state_ = DriverState::Stopping;
        state_ = DriverState::Stopped;
        return true;
    }

    DriverState state() const { return state_; }

   protected:
    Device* device() const { return device_; }

   private:
    Device* device_;
    DriverState state_;
};

class PciDriver : public Driver {
   public:
    bool readConfiguration(uint16_t offset, uint32_t& value) {
        if (!device()) return false;
        os::Device::Pci function = device()->function();
        return function.readConfig(offset, value) == 0;
    }

    bool writeConfiguration(uint16_t offset, uint32_t value) {
        if (!device()) return false;
        os::Device::Pci function = device()->function();
        return function.writeConfig(offset, value) == 0;
    }
};

class PciBus {
   public:
    uint32_t enumerate(Device* devices, uint32_t capacity) {
        os::Device::Pci functions[64]{};
        const int count = os::Device::Pci().enumerate(functions, capacity);
        if (count < 0) {
            return 0;
        }
        for (int index = 0; index < count; ++index) {
            devices[index].init(functions[index]);
        }
        return static_cast<uint32_t>(count);
    }
};

class DeviceManager {
   public:
    bool init() {
        deviceCount_ = bus_.enumerate(devices_, 64);
        driverCount_ = 0;
        for (uint32_t index = 0; index < deviceCount_; ++index) {
            drivers_[driverCount_].create(&devices_[index]);
            if (drivers_[driverCount_].init() &&
                drivers_[driverCount_].start()) {
                ++driverCount_;
            }
        }
        return true;
    }

    uint32_t deviceCount() const { return deviceCount_; }
    uint32_t driverCount() const { return driverCount_; }

    uint32_t categoryCount(DeviceCategory category) const {
        uint32_t count = 0;
        for (uint32_t index = 0; index < deviceCount_; ++index) {
            if (devices_[index].category() == category) {
                ++count;
            }
        }
        return count;
    }

   private:
    PciBus bus_;
    Device devices_[64];
    PciDriver drivers_[64];
    uint32_t deviceCount_;
    uint32_t driverCount_;
};

}  // namespace

class DeviceApplication {
   public:
    static void inputServiceMain(void* input) {
        static_cast<InputService*>(input)->run();
    }
    static void inputIrqMain(void* input) {
        static_cast<InputService*>(input)->runIrq();
    }

    int run() {
        os::Sys().log("[DEVICE] Device Server started in Ring 3\n");
        const uint64_t registry = os::getBootstrapHandle();
        static services::Lifecycle children;
        if (!children.init("device", registry) || !children.bootstrap() ||
            !children.supervise())
            return 1;
        const uint64_t endpoint = os::detail::EndpointControl().create();
        if (registry == 0 || endpoint == 0 ||
            !os::detail::RegistryClient(registry).registerName(os::deviceServiceName,
                                                        endpoint)) {
            os::Sys().log("[DEVICE] registration failed\n");
            return 1;
        }

        DeviceManager manager;
        if (!manager.init()) {
            os::Sys().log("[DEVICE] driver initialization failed\n");
            return 2;
        }
        os::Sys().log(
            "[DEVICE] PCI discovery and driver matching complete\n");
        os::Sys().log("[DEVICE] drivers entered Running state\n");

        constexpr uint64_t inputStackBase    = 0x0000000060000000ULL;
        constexpr uint64_t inputStackSize    = 64 * 1024;
        constexpr uint64_t inputIrqStackBase = inputStackBase + inputStackSize;
        if (!inputService.init(registry) ||
            os::mapMemory(inputStackBase, inputStackSize, 1) !=
                inputStackBase ||
            os::mapMemory(inputIrqStackBase, inputStackSize, 1) !=
                inputIrqStackBase ||
            os::createThread(inputServiceMain, inputStackBase + inputStackSize,
                             &inputService) == 0 ||
            os::createThread(inputIrqMain, inputIrqStackBase + inputStackSize,
                             &inputService) == 0) {
            os::Sys().log(
                "[INPUT] unable to start PS/2 input service\n");
            return 3;
        }
        os::Sys().log(
            "[INPUT] PS/2 keyboard and mouse service online\n");

        while (true) {
            os::IpcMessage request{};
            if (os::receiveMessage(endpoint, request) != 0) {
                continue;
            }
            os::IpcMessage reply{};
            if (request.type ==
                static_cast<uint64_t>(os::DeviceRequest::List)) {
                reply.data[0] = 0;
                reply.data[1] = manager.deviceCount();
                reply.data[2] = manager.driverCount();
                reply.data[3] = manager.categoryCount(DeviceCategory::Storage);
                reply.data[4] = manager.categoryCount(DeviceCategory::Network);
                reply.data[5] = manager.categoryCount(DeviceCategory::Display);
                reply.length  = 6;
            } else if (request.type ==
                       static_cast<uint64_t>(os::DeviceRequest::ListNode)) {
                constexpr const char* nodes[] = {"zero", "null", "tty1", "tty2",
                                                 "tty3", "tty4", "tty5", "tty6",
                                                 "tty7", "ptmx", "pts"};
                const uint32_t index = static_cast<uint32_t>(request.data[0]);
                if (index >= sizeof(nodes) / sizeof(nodes[0])) {
                    reply.data[0] = 1;
                    reply.length  = 1;
                } else {
                    reply.data[0] = 0;
                    reply.data[1] =
                        os::Runtime::stringsEqual(nodes[index], "pts") ? 1 : 0;
                    os::Runtime::copyString(
                        reinterpret_cast<char*>(&reply.data[2]), nodes[index],
                        6 * sizeof(uint64_t));
                    reply.length = 8;
                }
            } else if (request.type ==
                       static_cast<uint64_t>(os::DeviceRequest::ReadNode)) {
                const char* name = reinterpret_cast<const char*>(request.data);
                uint64_t size    = request.data[6];
                const uint64_t capacity =
                    (os::ipcDataWordCount - 2) * sizeof(uint64_t);
                if (!os::Runtime::stringsEqual(name, "zero") || size == 0 ||
                    size > capacity) {
                    reply.data[0] = static_cast<uint64_t>(
                        static_cast<int64_t>(os::ErrorCode::InvalidArgument));
                    reply.length = 1;
                } else {
                    reply.data[0] = 0;
                    reply.data[1] = size;
                    reply.length  = static_cast<uint32_t>(
                        2 + (size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
                }
            } else {
                reply.data[0] = 2;
                reply.length  = 1;
            }
            os::replyMessage(endpoint, request.replyToken, reply);
        }
    }

   private:
    InputService inputService;
};
int main(int, char**) {
    static DeviceApplication application;
    return application.run();
}
