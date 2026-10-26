#include "kernel/device.h"
#include "kernel/spinlock.h"
namespace kernel::device {
namespace {
class Ports {
 public:
    static int read(uint16_t port, uint8_t& value) { asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return 0; }
    static int write(uint16_t port, uint8_t value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); return 0; }
};
class Controller {
   public:
    bool init() {
        uint8_t ignored;
        for (uint32_t index = 0; index < 32 && outputReady(); ++index) {
            Ports::read(0x60, ignored);
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
        if (Ports::read(0x64, status) != 0 || (status & 1U) == 0 ||
            Ports::read(0x60, value) != 0) {
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
            if (Ports::read(0x64, status) == 0 && (status & 2U) == 0) {
                return true;
            }
            asm volatile("pause");
        }
        return false;
    }

    bool outputReady() {
        uint8_t status;
        return Ports::read(0x64, status) == 0 && (status & 1U) != 0;
    }

    bool readData(uint8_t& value) {
        for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
            if (outputReady()) {
                return Ports::read(0x60, value) == 0;
            }
            asm volatile("pause");
        }
        return false;
    }

    bool writeCommand(uint8_t value) {
        return inputReady() && Ports::write(0x64, value) == 0;
    }

    bool writeData(uint8_t value) {
        return inputReady() && Ports::write(0x60, value) == 0;
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
Controller controller;
SpinLock controllerLock;
}
int Ps2Access::init() {
    InterruptSpinLockGuard guard(controllerLock);
    return controller.init() ? controller.mousePacketSize() : -11;
}
bool Ps2Access::read(uint8_t& value, bool& mouse) {
    InterruptSpinLockGuard guard(controllerLock);
    return controller.readInterruptByte(value, mouse);
}
}  // namespace kernel::device
