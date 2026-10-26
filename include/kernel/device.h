#include "service/common/records.h"
#pragma once

#include <cstdint>

#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>

namespace kernel {

class Task;
class Endpoint;
struct BootInfo;

namespace device {
void init(const BootInfo* bootInfo);
class ResourceDomain;

class FrameMapping {
   public:
    static FrameMapping* create(Task& task,uint64_t address,os::wire::DisplayInfo& info);
    bool query(Task& task,os::wire::DisplayInfo& info);
    void destroy();
   private:
    Task* owner_{};
    uint64_t address_{},size_{},generation_{};
};

class DisplayAccess {
   public:
    static uint64_t reserveAddress(uint64_t size);
    static bool describe(uint64_t address,os::wire::DisplayInfo& info);
    static bool mapFramebuffer(Task& task, uint64_t userAddress,
                               os::wire::DisplayInfo& info,uint64_t* generation=nullptr);
};

class PciAccess {
   public:
    static uint32_t enumerate(os::wire::PciFunctionInfo* functions,
                              uint32_t capacity);
    static bool read(const os::wire::PciFunctionInfo& function, uint16_t offset,
                     uint32_t& value);
    static bool write(const os::wire::PciFunctionInfo& function, uint16_t offset,
                      uint32_t value);
    static bool getMemoryBar(const os::wire::PciFunctionInfo& function,
                             uint8_t barIndex, uint64_t& base, uint64_t& size);
};
class Ps2Access {
   public:
    static int init();
    static bool read(uint8_t& value, bool& mouse);
};

class MmioRegion {
   public:
    static MmioRegion* map(Task& task, const os::wire::PciFunctionInfo& function,
                           uint8_t barIndex, uint64_t userAddress,
                           uint64_t size,uint64_t offset=0);
    uint64_t address() const;
    uint64_t size() const;
    void destroy();

   private:
    friend class ResourceDomain;
    ResourceDomain* domain_ = nullptr;
    MmioRegion* next_ = nullptr;
    bool mapped_ = false, closed_ = false;
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};

class DmaBuffer {
   public:
    static DmaBuffer* create(Task& task, ResourceDomain& domain, uint64_t userAddress,
                             os::wire::DmaBufferInfo& info,uint64_t size=4096,uint64_t alignment=4096);
    void destroy();

   private:
    friend class ResourceDomain;
    ResourceDomain* domain_ = nullptr;
    DmaBuffer* next_ = nullptr;
    bool mapped_ = false, closed_ = false;
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};

class ResourceDomain {
   public:
    struct Statistics { uint32_t active = 0, pending = 0, stopped = 0, quarantined = 0, pages = 0; };
    static void init();
    static bool owned(Task& owner);
    static Statistics statistics();
    static ResourceDomain* claim(Task& owner, const os::wire::PciFunctionInfo& function);
    static ResourceDomain* find(Task& owner, const os::wire::PciFunctionInfo& function);
    static bool writeConfiguration(uint64_t owner, const os::wire::PciFunctionInfo& function, uint16_t offset, uint32_t value);
    static void ownerExiting(Task& owner);
    static void addressSpaceReleased(Task& owner);
    bool active(Task& owner);
    bool allowsIrq(Task& owner,uint8_t irq);
    bool attach(MmioRegion& region);
    bool attach(DmaBuffer& buffer);
    void closeLease();
    int stop(Task& owner);
   private:
    friend class MmioRegion;
    friend class DmaBuffer;
    enum class State : uint8_t { Free, Active, Pending, Stopping, Stopped, Quarantined };
    State state_ = State::Free;
    Task* owner_ = nullptr;
    os::wire::PciFunctionInfo function_{};
    uint32_t references_ = 0, dmaPages_ = 0;
    bool exiting_ = false, addressSpaceGone_ = false;
    uint64_t controlMapping_ = 0, controlSize_ = 0;
    uint64_t addressGeneration_ = 0;
    volatile uint8_t* status_ = nullptr;
    uint64_t barBases_[6]{}, barSizes_[6]{};
    MmioRegion* mmio_ = nullptr;
    DmaBuffer* dma_ = nullptr;
    void drop();
    void unmap(uint64_t address, uint64_t size, bool& mapped,uint64_t physical);
    bool reset();
    static void work(void*);
};

class InterruptBinding {
   public:
    static InterruptBinding* create(Task& task, uint8_t irq,
                                    Endpoint* endpoint, bool levelTriggered = false);
    uint8_t irq() const;
    Endpoint* endpoint() const;
    bool requiresAcknowledgement() const { return levelTriggered_; }
    bool acknowledge(Task& task) const;
    void destroy();

   private:
    uint8_t irq_;
    Endpoint* endpoint_;
    Task* owner_;
    uint32_t apicId_;
    bool levelTriggered_;
};
}  // namespace device

}  // namespace kernel
