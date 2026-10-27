#pragma once
#include <cstdint>

#include "kernel/sync.h"
#include "service/common/records.h"
#include <os/device.h>

namespace kernel::device {
class PciBus {
   public:
    uint32_t enumerate(os::wire::PciFunctionInfo* functions, uint32_t capacity);
    bool read(const os::wire::PciFunctionInfo& function, uint16_t offset,
              uint32_t& value);
    bool write(const os::wire::PciFunctionInfo& function, uint16_t offset,
               uint32_t value);
    bool getMemoryBar(const os::wire::PciFunctionInfo& function,
                      uint8_t barIndex, uint64_t& base, uint64_t& size);

   private:
    kernel::task::SpinLock lock_;
};
PciBus& pciBus();

class Pci {
   public:
    Pci(PciBus& bus, const os::wire::PciFunctionInfo& function)
        : bus_(bus), function_(function) {}
    bool read(uint16_t offset, uint32_t& value) {
        return bus_.read(function_, offset, value);
    }
    bool write(uint16_t offset, uint32_t value) {
        return bus_.write(function_, offset, value);
    }
    bool getMemoryBar(uint8_t index, uint64_t& base, uint64_t& size) {
        return bus_.getMemoryBar(function_, index, base, size);
    }

   private:
    PciBus& bus_;
    os::wire::PciFunctionInfo function_;
};

}  // namespace kernel::device
