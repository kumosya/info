#pragma once

#include <stdint.h>

#include "os/abi.h"

namespace kernel {

class Task;
class Endpoint;
struct BootInfo;

namespace device {
void init(const BootInfo* bootInfo);

class DisplayAccess {
   public:
    static bool mapFramebuffer(Task& task, uint64_t userAddress,
                               os::DisplayInfo& info);
};

class PciAccess {
   public:
    static uint32_t enumerate(os::PciFunctionInfo* functions,
                              uint32_t capacity);
    static bool read(const os::PciFunctionInfo& function, uint16_t offset,
                     uint32_t& value);
    static bool write(const os::PciFunctionInfo& function, uint16_t offset,
                      uint32_t value);
    static bool getMemoryBar(const os::PciFunctionInfo& function,
                             uint8_t barIndex, uint64_t& base, uint64_t& size);
};

class MmioRegion {
   public:
    static MmioRegion* map(Task& task, const os::PciFunctionInfo& function,
                           uint8_t barIndex, uint64_t userAddress,
                           uint64_t size);

   private:
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};

class DmaBuffer {
   public:
    static DmaBuffer* create(Task& task, uint64_t userAddress,
                             os::DmaBufferInfo& info);

   private:
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};

class InterruptBinding {
   public:
    static InterruptBinding* create(Task& task, uint8_t irq,
                                    Endpoint* endpoint, bool levelTriggered = false);
    uint8_t irq() const;
    Endpoint* endpoint() const;
    bool requiresAcknowledgement() const { return levelTriggered_; }
    bool acknowledge(Task& task) const;

   private:
    uint8_t irq_;
    Endpoint* endpoint_;
    Task* owner_;
    uint32_t apicId_;
    bool levelTriggered_;
};
}  // namespace device

}  // namespace kernel
