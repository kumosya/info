#include "kernel/device.h"
#include "kernel/pci.h"
#include "kernel/sync.h"
#include "service/common/records.h"

namespace kernel::device {

namespace {
PciBus configurationBus;
uint32_t pciAddress(const os::wire::PciFunctionInfo& function,
                    uint16_t offset) {
    return 0x80000000U | (static_cast<uint32_t>(function.bus) << 16) |
           (static_cast<uint32_t>(function.device) << 11) |
           (static_cast<uint32_t>(function.function) << 8) | (offset & 0xfc);
}

uint32_t readConfigRaw(const os::wire::PciFunctionInfo& function,
                       uint16_t offset) {
    const uint32_t address = pciAddress(function, offset);
    asm volatile("outl %0, %1" : : "a"(address), "Nd"(0xcf8));
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(0xcfc));
    return value;
}

void writeConfigRaw(const os::wire::PciFunctionInfo& function, uint16_t offset,
                    uint32_t value) {
    const uint32_t address = pciAddress(function, offset);
    asm volatile("outl %0, %1" : : "a"(address), "Nd"(0xcf8));
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(0xcfc));
}

bool validFunction(const os::wire::PciFunctionInfo& function) {
    return function.segment == 0 && function.device < 32 &&
           function.function < 8;
}

}  // namespace

PciBus& pciBus() { return configurationBus; }
uint32_t PciBus::enumerate(os::wire::PciFunctionInfo* functions,
                           uint32_t capacity) {
    if (functions == nullptr || capacity == 0) {
        return 0;
    }
    uint32_t count = 0;
    for (uint16_t bus = 0; bus < 256 && count < capacity; ++bus) {
        for (uint8_t device = 0; device < 32 && count < capacity; ++device) {
            os::wire::PciFunctionInfo probe{};
            probe.bus      = static_cast<uint8_t>(bus);
            probe.device   = device;
            probe.function = 0;
            uint32_t identity;
            if (!read(probe, 0, identity) || (identity & 0xffff) == 0xffff) {
                continue;
            }
            uint32_t header;
            read(probe, 0x0c, header);
            const uint8_t functionCount = (header & 0x00800000U) != 0 ? 8 : 1;
            for (uint8_t function = 0;
                 function < functionCount && count < capacity; ++function) {
                probe.function = function;
                if (!read(probe, 0, identity) ||
                    (identity & 0xffff) == 0xffff) {
                    continue;
                }
                uint32_t classValue;
                read(probe, 8, classValue);
                read(probe, 0x0c, header);
                os::wire::PciFunctionInfo& result = functions[count++];
                result                            = probe;
                result.vendorId                   = identity & 0xffff;
                result.deviceId                   = identity >> 16;
                result.classCode                  = classValue >> 24;
                result.subclass                   = classValue >> 16;
                result.programmingInterface       = classValue >> 8;
                result.headerType                 = header >> 16;
            }
        }
    }
    return count;
}

bool PciBus::read(const os::wire::PciFunctionInfo& function, uint16_t offset,
                  uint32_t& value) {
    if (!validFunction(function) || offset >= 256 || (offset & 3) != 0) {
        return false;
    }
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    value = readConfigRaw(function, offset);
    return true;
}

bool PciBus::write(const os::wire::PciFunctionInfo& function, uint16_t offset,
                   uint32_t value) {
    if (!validFunction(function) || offset >= 256 || (offset & 3) != 0) {
        return false;
    }
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    writeConfigRaw(function, offset, value);
    return true;
}

bool PciBus::getMemoryBar(const os::wire::PciFunctionInfo& function,
                          uint8_t barIndex, uint64_t& base, uint64_t& size) {
    if (!validFunction(function) || barIndex >= 6) {
        return false;
    }
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    const uint16_t offset      = static_cast<uint16_t>(0x10 + barIndex * 4);
    const uint32_t originalLow = readConfigRaw(function, offset);
    if ((originalLow & 1) != 0) {
        return false;
    }
    const bool is64Bit = ((originalLow >> 1) & 3) == 2;
    if (is64Bit && barIndex == 5) {
        return false;
    }
    const uint32_t originalHigh =
        is64Bit ? readConfigRaw(function, offset + 4) : 0;
    const uint32_t command = readConfigRaw(function, 4);
    writeConfigRaw(function, 4, (command & 0xffff) & ~3U);
    writeConfigRaw(function, offset, 0xffffffffU);
    if (is64Bit) {
        writeConfigRaw(function, offset + 4, 0xffffffffU);
    }
    const uint32_t maskLow  = readConfigRaw(function, offset);
    const uint32_t maskHigh = is64Bit ? readConfigRaw(function, offset + 4) : 0;
    writeConfigRaw(function, offset, originalLow);
    if (is64Bit) {
        writeConfigRaw(function, offset + 4, originalHigh);
    }
    writeConfigRaw(function, 4, command & 0xffff);

    base =
        (static_cast<uint64_t>(originalHigh) << 32) | (originalLow & ~0xfULL);
    const uint64_t mask =
        (static_cast<uint64_t>(maskHigh) << 32) | (maskLow & ~0xfULL);
    size = is64Bit ? (~mask) + 1
                   : static_cast<uint64_t>(
                         static_cast<uint32_t>(~(maskLow & ~0xfU)) + 1U);
    return base != 0 && size != 0;
}

}  // namespace kernel::device
