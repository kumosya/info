#pragma once

#include <stdint.h>

#include "os/abi.h"

namespace kernel {

class Process;
struct Endpoint;
struct BootInfo;

void initDeviceSubsystem(const BootInfo* bootInfo);

class PortAccess {
   public:
    static bool read8(const Process& process, uint16_t port, uint8_t& value);
    static bool write8(const Process& process, uint16_t port, uint8_t value);
};

class DisplayAccess {
   public:
    static bool mapFramebuffer(Process& process, uint64_t userAddress,
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
    static MmioRegion* map(Process& process,
                           const os::PciFunctionInfo& function,
                           uint8_t barIndex, uint64_t userAddress,
                           uint64_t size);

   private:
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};

class DmaBuffer {
   public:
    static DmaBuffer* create(Process& process, uint64_t userAddress,
                             os::DmaBufferInfo& info);

   private:
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};

class InterruptBinding {
   public:
    static InterruptBinding* create(Process& process, uint8_t irq,
                                    Endpoint* endpoint);
    uint8_t irq() const;
    Endpoint* endpoint() const;

   private:
    uint8_t irq_;
    Endpoint* endpoint_;
};

}  // namespace kernel
