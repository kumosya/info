#include "os/abi.h"
#include "os/input.h"
#include "os/pci.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/sync.h"

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
    bool enqueue(const os::InputEvent& event) {
        os::SpinLockGuard guard(lock_);
        if (nextSequence_ != firstSequence_ && canCoalesceLast()) {
            os::InputEvent& previous = events_[(nextSequence_ - 1) % capacity];
            if (event.type == os::InputEventType::MouseMove &&
                previous.type == os::InputEventType::MouseMove) {
                previous.mouse.deltaX += event.mouse.deltaX;
                previous.mouse.deltaY += event.mouse.deltaY;
                previous.timestamp = event.timestamp;
                return true;
            }
            if (event.type == os::InputEventType::MouseWheel &&
                previous.type == os::InputEventType::MouseWheel) {
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

    bool dequeue(uint64_t taskId, os::InputEvent& event) {
        os::SpinLockGuard guard(lock_);
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
        os::SpinLockGuard guard(lock_);
        Subscriber* existing = findSubscriber(taskId);
        if (existing != nullptr) {
            return existing->wakeEndpoint == wakeEndpoint;
        }
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.taskId == 0) {
                subscriber = {taskId, nextSequence_, wakeEndpoint, false};
                return true;
            }
        }
        return false;
    }

    uint32_t collectWakeEndpoints(uint64_t* endpoints, uint32_t capacity) {
        if (endpoints == nullptr || capacity == 0) {
            return 0;
        }
        os::SpinLockGuard guard(lock_);
        uint32_t count = 0;
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.taskId == 0 || subscriber.wakeEndpoint == 0 ||
                subscriber.notified) {
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

    void unsubscribe(uint64_t wakeEndpoint) {
        os::SpinLockGuard guard(lock_);
        for (Subscriber& subscriber : subscribers_) {
            if (subscriber.wakeEndpoint == wakeEndpoint) {
                subscriber = {};
                return;
            }
        }
    }

    uint32_t size(uint64_t taskId) {
        os::SpinLockGuard guard(lock_);
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
    os::InputEvent events_[capacity];
    Subscriber subscribers_[4]{};
    uint64_t firstSequence_ = 0;
    uint64_t nextSequence_  = 0;
    os::SpinLock lock_;
};

class Ps2Controller {
   public:
    bool init() {
        uint8_t ignored;
        for (uint32_t index = 0; index < 32 && outputReady(); ++index) {
            os::SystemService::readPort8(0x60, ignored);
        }
        if (!writeCommand(0xad) || !writeCommand(0xa7) || !writeCommand(0x20) ||
            !readData(configuration_)) {
            return false;
        }
        configuration_ =
            static_cast<uint8_t>((configuration_ | 0x03U) & ~0x30U);
        if (!writeCommand(0x60) || !writeData(configuration_) ||
            !writeCommand(0xae) || !writeCommand(0xa8)) {
            return false;
        }
        mousePacketSize_ = enableMouseWheel() ? 4 : 3;
        return sendKeyboard(0xf4) && sendMouse(0xf4);
    }

    bool readInterruptByte(uint8_t& value, bool& mouse) {
        uint8_t status;
        if (os::SystemService::readPort8(0x64, status) != 0 || (status & 1U) == 0 ||
            os::SystemService::readPort8(0x60, value) != 0) {
            return false;
        }
        mouse = (status & 0x20U) != 0;
        return true;
    }

    uint8_t mousePacketSize() const { return mousePacketSize_; }

   private:
    bool inputReady() {
        for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
            uint8_t status;
            if (os::SystemService::readPort8(0x64, status) == 0 && (status & 2U) == 0) {
                return true;
            }
            asm volatile("pause");
        }
        return false;
    }

    bool outputReady() {
        uint8_t status;
        return os::SystemService::readPort8(0x64, status) == 0 && (status & 1U) != 0;
    }

    bool readData(uint8_t& value) {
        for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
            if (outputReady()) {
                return os::SystemService::readPort8(0x60, value) == 0;
            }
            asm volatile("pause");
        }
        return false;
    }

    bool writeCommand(uint8_t value) {
        return inputReady() && os::SystemService::writePort8(0x64, value) == 0;
    }

    bool writeData(uint8_t value) {
        return inputReady() && os::SystemService::writePort8(0x60, value) == 0;
    }

    bool sendKeyboard(uint8_t value) {
        uint8_t acknowledgement;
        return writeData(value) && readData(acknowledgement) &&
               acknowledgement == 0xfa;
    }

    bool sendMouse(uint8_t value) {
        uint8_t acknowledgement;
        return writeCommand(0xd4) && writeData(value) &&
               readData(acknowledgement) && acknowledgement == 0xfa;
    }

    bool setMouseSampleRate(uint8_t rate) {
        return sendMouse(0xf3) && sendMouse(rate);
    }

    bool enableMouseWheel() {
        if (!setMouseSampleRate(200) || !setMouseSampleRate(100) ||
            !setMouseSampleRate(80) || !sendMouse(0xf2)) {
            return false;
        }
        uint8_t deviceId;
        return readData(deviceId) && deviceId == 3 && setMouseSampleRate(200);
    }

    uint8_t configuration_   = 0;
    uint8_t mousePacketSize_ = 3;
};

class KeyboardDriver {
   public:
    void consume(uint8_t scanCode, InputQueue& queue) {
        if (scanCode == 0xe0) {
            extended_ = true;
            return;
        }
        const bool pressed    = (scanCode & 0x80U) == 0;
        const uint8_t code    = scanCode & 0x7fU;
        const os::KeyCode key = translate(code, extended_);
        extended_             = false;
        if (key == os::KeyCode::Unknown) {
            return;
        }
        updateModifiers(key, pressed);
        os::InputEvent event{};
        event.type =
            pressed ? os::InputEventType::KeyDown : os::InputEventType::KeyUp;
        event.timestamp     = os::getMonotonicTime();
        event.key.keyCode   = key;
        event.key.modifiers = modifiers_;
        event.key.pressed   = pressed;
        queue.enqueue(event);
    }

   private:
    static os::KeyCode translate(uint8_t code, bool extended) {
        if (extended) {
            switch (code) {
                case 0x1d:
                    return os::KeyCode::RightControl;
                case 0x38:
                    return os::KeyCode::RightAlt;
                case 0x1c:
                    return os::KeyCode::KeypadEnter;
                case 0x35:
                    return os::KeyCode::KeypadDivide;
                case 0x47:
                    return os::KeyCode::Home;
                case 0x48:
                    return os::KeyCode::Up;
                case 0x49:
                    return os::KeyCode::PageUp;
                case 0x50:
                    return os::KeyCode::Down;
                case 0x4f:
                    return os::KeyCode::End;
                case 0x51:
                    return os::KeyCode::PageDown;
                case 0x52:
                    return os::KeyCode::Insert;
                case 0x53:
                    return os::KeyCode::Delete;
                case 0x4b:
                    return os::KeyCode::Left;
                case 0x4d:
                    return os::KeyCode::Right;
                default:
                    return os::KeyCode::Unknown;
            }
        }
        constexpr os::KeyCode scanCodes[] = {os::KeyCode::Unknown,
                                             os::KeyCode::Escape,
                                             os::KeyCode::Digit1,
                                             os::KeyCode::Digit2,
                                             os::KeyCode::Digit3,
                                             os::KeyCode::Digit4,
                                             os::KeyCode::Digit5,
                                             os::KeyCode::Digit6,
                                             os::KeyCode::Digit7,
                                             os::KeyCode::Digit8,
                                             os::KeyCode::Digit9,
                                             os::KeyCode::Digit0,
                                             os::KeyCode::Minus,
                                             os::KeyCode::Equal,
                                             os::KeyCode::Backspace,
                                             os::KeyCode::Tab,
                                             os::KeyCode::Q,
                                             os::KeyCode::W,
                                             os::KeyCode::E,
                                             os::KeyCode::R,
                                             os::KeyCode::T,
                                             os::KeyCode::Y,
                                             os::KeyCode::U,
                                             os::KeyCode::I,
                                             os::KeyCode::O,
                                             os::KeyCode::P,
                                             os::KeyCode::LeftBracket,
                                             os::KeyCode::RightBracket,
                                             os::KeyCode::Enter,
                                             os::KeyCode::LeftControl,
                                             os::KeyCode::A,
                                             os::KeyCode::S,
                                             os::KeyCode::D,
                                             os::KeyCode::F,
                                             os::KeyCode::G,
                                             os::KeyCode::H,
                                             os::KeyCode::J,
                                             os::KeyCode::K,
                                             os::KeyCode::L,
                                             os::KeyCode::Semicolon,
                                             os::KeyCode::Apostrophe,
                                             os::KeyCode::Grave,
                                             os::KeyCode::LeftShift,
                                             os::KeyCode::Backslash,
                                             os::KeyCode::Z,
                                             os::KeyCode::X,
                                             os::KeyCode::C,
                                             os::KeyCode::V,
                                             os::KeyCode::B,
                                             os::KeyCode::N,
                                             os::KeyCode::M};
        if (code < sizeof(scanCodes) / sizeof(scanCodes[0])) {
            return scanCodes[code];
        }
        switch (code) {
            case 0x0c:
                return os::KeyCode::Minus;
            case 0x0d:
                return os::KeyCode::Equal;
            case 0x1a:
                return os::KeyCode::LeftBracket;
            case 0x1b:
                return os::KeyCode::RightBracket;
            case 0x36:
                return os::KeyCode::RightShift;
            case 0x38:
                return os::KeyCode::LeftAlt;
            case 0x39:
                return os::KeyCode::Space;
            case 0x3a:
                return os::KeyCode::CapsLock;
            case 0x27:
                return os::KeyCode::Semicolon;
            case 0x28:
                return os::KeyCode::Apostrophe;
            case 0x29:
                return os::KeyCode::Grave;
            case 0x2b:
                return os::KeyCode::Backslash;
            case 0x33:
                return os::KeyCode::Comma;
            case 0x34:
                return os::KeyCode::Period;
            case 0x35:
                return os::KeyCode::Slash;
            case 0x3b:
                return os::KeyCode::F1;
            case 0x3c:
                return os::KeyCode::F2;
            case 0x3d:
                return os::KeyCode::F3;
            case 0x3e:
                return os::KeyCode::F4;
            case 0x3f:
                return os::KeyCode::F5;
            case 0x40:
                return os::KeyCode::F6;
            case 0x41:
                return os::KeyCode::F7;
            case 0x42:
                return os::KeyCode::F8;
            case 0x43:
                return os::KeyCode::F9;
            case 0x44:
                return os::KeyCode::F10;
            case 0x57:
                return os::KeyCode::F11;
            case 0x58:
                return os::KeyCode::F12;
            case 0x45:
                return os::KeyCode::NumLock;
            case 0x46:
                return os::KeyCode::ScrollLock;
            case 0x47:
                return os::KeyCode::Keypad7;
            case 0x48:
                return os::KeyCode::Keypad8;
            case 0x49:
                return os::KeyCode::Keypad9;
            case 0x4a:
                return os::KeyCode::KeypadMinus;
            case 0x4b:
                return os::KeyCode::Keypad4;
            case 0x4c:
                return os::KeyCode::Keypad5;
            case 0x4d:
                return os::KeyCode::Keypad6;
            case 0x4e:
                return os::KeyCode::KeypadPlus;
            case 0x4f:
                return os::KeyCode::Keypad1;
            case 0x50:
                return os::KeyCode::Keypad2;
            case 0x51:
                return os::KeyCode::Keypad3;
            case 0x52:
                return os::KeyCode::Keypad0;
            case 0x53:
                return os::KeyCode::KeypadDecimal;
            case 0x37:
                return os::KeyCode::KeypadMultiply;
            default:
                return os::KeyCode::Unknown;
        }
    }

    void updateModifiers(os::KeyCode key, bool pressed) {
        uint32_t mask = 0;
        if (key == os::KeyCode::LeftShift || key == os::KeyCode::RightShift) {
            mask = os::keyModifierShift;
        } else if (key == os::KeyCode::LeftControl ||
                   key == os::KeyCode::RightControl) {
            mask = os::keyModifierControl;
        } else if (key == os::KeyCode::LeftAlt ||
                   key == os::KeyCode::RightAlt) {
            mask = os::keyModifierAlt;
        } else if (key == os::KeyCode::CapsLock && pressed) {
            modifiers_ ^= os::keyModifierCapsLock;
            return;
        } else if (key == os::KeyCode::NumLock && pressed) {
            modifiers_ ^= os::keyModifierNumLock;
            return;
        } else if (key == os::KeyCode::ScrollLock && pressed) {
            modifiers_ ^= os::keyModifierScrollLock;
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
            emit(queue, os::InputEventType::MouseMove, deltaX, deltaY, 0,
                 buttons);
        }
        if (wheel != 0) {
            emit(queue, os::InputEventType::MouseWheel, 0, 0, wheel, buttons);
        }
        if ((buttons & ~buttons_) != 0) {
            emit(queue, os::InputEventType::MouseButtonDown, 0, 0, 0, buttons);
        }
        if ((buttons_ & ~buttons) != 0) {
            emit(queue, os::InputEventType::MouseButtonUp, 0, 0, 0, buttons);
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

    static void emit(InputQueue& queue, os::InputEventType type, int32_t deltaX,
                     int32_t deltaY, int32_t wheel, uint32_t buttons) {
        os::InputEvent event{};
        event.type      = type;
        event.timestamp = os::getMonotonicTime();
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
    bool init(uint64_t registry) {
        requestEndpoint_ = os::createEndpoint();
        irqEndpoint_     = os::createEndpoint();
        if (requestEndpoint_ == 0 || irqEndpoint_ == 0 ||
            !controller_.init() || os::bindIrq(1, irqEndpoint_) == 0 ||
            os::bindIrq(12, irqEndpoint_) == 0 ||
            !os::registerService(registry, os::inputServiceName,
                                 requestEndpoint_)) {
            return false;
        }
        mouse_.init(controller_.mousePacketSize());
        return true;
    }

    [[noreturn]] void run() {
        while (true) {
            os::IpcMessage request{};
            if (os::receiveMessage(requestEndpoint_, request) != 0) {
                continue;
            }
            os::IpcMessage reply{};
            if (request.type ==
                static_cast<uint64_t>(os::InputRequest::GetState)) {
                reply.data[0] = 0;
                reply.data[1] = queue_.size(request.senderPid);
                reply.length  = 2;
            } else if (request.type ==
                       static_cast<uint64_t>(os::InputRequest::GetEvent)) {
                os::InputEvent event{};
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
            } else {
                reply.data[0] = 1;
                reply.length  = 1;
            }
            os::replyMessage(requestEndpoint_, reply);
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
            if (os::sendMessage(endpoints[index], notification) != 0) {
                queue_.unsubscribe(endpoints[index]);
                os::closeHandle(endpoints[index]);
            }
        }
    }

    uint64_t requestEndpoint_ = 0;
    uint64_t irqEndpoint_     = 0;
    Ps2Controller controller_;
    KeyboardDriver keyboard_;
    MouseDriver mouse_;
    InputQueue queue_;
};

InputService inputService;

void inputServiceMain() { inputService.run(); }

void inputIrqMain() { inputService.runIrq(); }

class Device {
   public:
    void init(const os::PciFunctionInfo& function) {
        function_ = function;
        category_ = classify(function);
    }

    const os::PciFunctionInfo& function() const { return function_; }
    DeviceCategory category() const { return category_; }

   private:
    static DeviceCategory classify(const os::PciFunctionInfo& function) {
        if (function.classCode == 0x01) {
            return DeviceCategory::Storage;
        }
        if (function.classCode == 0x02) {
            return DeviceCategory::Network;
        }
        if (function.classCode == 0x03) {
            return DeviceCategory::Display;
        }
        return DeviceCategory::Pci;
    }

    os::PciFunctionInfo function_;
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
        return device() != nullptr &&
               os::readPciConfig(device()->function(), offset, value) == 0;
    }

    bool writeConfiguration(uint16_t offset, uint32_t value) {
        return device() != nullptr &&
               os::writePciConfig(device()->function(), offset, value) == 0;
    }

    uint64_t mapBar(uint8_t barIndex, uint64_t address, uint64_t size) {
        return device() == nullptr ? 0
                                   : os::mapDeviceMmio(device()->function(),
                                                       barIndex, address, size);
    }
};

class PciBus {
   public:
    uint32_t enumerate(Device* devices, uint32_t capacity) {
        os::PciFunctionInfo functions[64]{};
        const int count = os::enumeratePci(functions, capacity);
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

int main(int, char**) {
    os::log("[DEVICE] Device Server started in Ring 0\n");
    const uint64_t registry = os::getBootstrapHandle();
    const uint64_t endpoint = os::createEndpoint();
    if (registry == 0 || endpoint == 0 ||
        !os::registerService(registry, os::deviceServiceName, endpoint)) {
        os::log("[DEVICE] registration failed\n");
        return 1;
    }

    DeviceManager manager;
    if (!manager.init()) {
        os::log("[DEVICE] driver initialization failed\n");
        return 2;
    }
    os::log("[DEVICE] PCI discovery and driver matching complete\n");
    os::log("[DEVICE] drivers entered Running state\n");

    constexpr uint64_t inputStackBase    = 0x0000000060000000ULL;
    constexpr uint64_t inputStackSize    = 64 * 1024;
    constexpr uint64_t inputIrqStackBase = inputStackBase + inputStackSize;
    if (!inputService.init(registry) ||
        os::mapMemory(inputStackBase, inputStackSize, 1) != inputStackBase ||
        os::mapMemory(inputIrqStackBase, inputStackSize, 1) !=
            inputIrqStackBase ||
        os::createThread(inputServiceMain, inputStackBase + inputStackSize) ==
            0 ||
        os::createThread(inputIrqMain, inputIrqStackBase + inputStackSize) ==
            0) {
        os::log("[INPUT] unable to start PS/2 input service\n");
        return 3;
    }
    os::log("[INPUT] PS/2 keyboard and mouse service online\n");

    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(endpoint, request) != 0) {
            continue;
        }
        os::IpcMessage reply{};
        if (request.type == static_cast<uint64_t>(os::DeviceRequest::List)) {
            reply.data[0] = 0;
            reply.data[1] = manager.deviceCount();
            reply.data[2] = manager.driverCount();
            reply.data[3] = manager.categoryCount(DeviceCategory::Storage);
            reply.data[4] = manager.categoryCount(DeviceCategory::Network);
            reply.data[5] = manager.categoryCount(DeviceCategory::Display);
            reply.length  = 6;
        } else if (request.type ==
                   static_cast<uint64_t>(os::DeviceRequest::ListNode)) {
            constexpr const char* nodes[] = {
                "zero", "null",
                "tty1", "tty2", "tty3", "tty4", "tty5", "tty6", "tty7",
                "ptmx", "pts"};
            const uint32_t index = static_cast<uint32_t>(request.data[0]);
            if (index >= sizeof(nodes) / sizeof(nodes[0])) {
                reply.data[0] = 1;
                reply.length = 1;
            } else {
                reply.data[0] = 0;
                reply.data[1] = os::stringsEqual(nodes[index], "pts") ? 1 : 0;
                os::copyString(reinterpret_cast<char*>(&reply.data[2]),
                               nodes[index], 6 * sizeof(uint64_t));
                reply.length = 8;
            }
        } else if (request.type ==
                   static_cast<uint64_t>(os::DeviceRequest::ReadNode)) {
            const char* name = reinterpret_cast<const char*>(request.data);
            uint64_t size = request.data[6];
            const uint64_t capacity =
                (os::ipcDataWordCount - 2) * sizeof(uint64_t);
            if (!os::stringsEqual(name, "zero") || size == 0 ||
                size > capacity) {
                reply.data[0] = static_cast<uint64_t>(
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument));
                reply.length = 1;
            } else {
                reply.data[0] = 0;
                reply.data[1] = size;
                reply.length = static_cast<uint32_t>(
                    2 + (size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
            }
        } else {
            reply.data[0] = 2;
            reply.length  = 1;
        }
        os::replyMessage(endpoint, reply);
    }
}
