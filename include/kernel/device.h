/*
 * 设备资源访问和回收。
 */

#include "service/common/records.h"
#pragma once

#include <cstdint>

#include "kernel/sync.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace task {
    class Task;
    class Endpoint;
    class SpinLock;
}  // namespace task
class BootInfo;

namespace device {

    void initSerial();
    void serialWrite(char value);
    void serialWrite(const char* value);
    void serialWriteHex(uint64_t value);
    void serialWriteDecimal(uint64_t value);
    void emergencyWrite(const char* value);
    void emergencyHex(uint64_t value);

    void initDevices(const ::BootInfo* bootInfo);
    /* 保存启动映像的内存位置，向存储服务提供只读访问。 */
    class BootImage {
        public:
            void init(const ::BootInfo& boot);
            int call(const os::IpcMessage& request, os::IpcMessage& reply);

        private:
            uint8_t* bytes = nullptr;
            uint64_t size  = 0;
            task::SpinLock lock;
    };
    BootImage& bootImage();

    class ResourceDomain;

    /* 登记显示缓冲区在任务中的映射，便于退出时撤销。 */
    class FrameMapping {
        public:
            static FrameMapping* create(task::Task& task, uint64_t address,
                                        os::wire::DisplayInfo& info);
            bool query(task::Task& task, os::wire::DisplayInfo& info);
            void destroy();

        private:
            task::Task* owner_{};
            uint64_t address_{}, size_{}, generation_{};
    };

    uint64_t reserveDisplayAddress(uint64_t size);
    bool describeDisplay(uint64_t address, os::wire::DisplayInfo& info);
    bool mapFramebuffer(task::Task& task, uint64_t userAddress,
                        os::wire::DisplayInfo& info,
                        uint64_t* generation = nullptr);

    int initPs2();
    bool readPs2(uint8_t& value, bool& mouse);

    /* 记录设备寄存器的物理区间及映射状态。 */
    class MmioRegion {
        public:
            static MmioRegion* map(task::Task& task,
                                   const os::wire::PciFunctionInfo& function,
                                   uint8_t barIndex, uint64_t userAddress,
                                   uint64_t size, uint64_t offset = 0);
            uint64_t address() const;
            uint64_t size() const;
            void destroy();

        private:
            friend class ResourceDomain;
            friend class ResourceDomains;
            ResourceDomain* domain_ = nullptr;
            MmioRegion* next_       = nullptr;
            bool mapped_ = false, closed_ = false;
            uint64_t physicalAddress_;
            uint64_t virtualAddress_;
            uint64_t size_;
    };

    /* 记录设备可直接访问的内存及其任务、设备归属。 */
    class DmaBuffer {
        public:
            static DmaBuffer* create(task::Task& task, ResourceDomain& domain,
                                     uint64_t userAddress,
                                     os::wire::DmaBufferInfo& info,
                                     uint64_t size      = 4096,
                                     uint64_t alignment = 4096);
            void destroy();

        private:
            friend class ResourceDomain;
            friend class ResourceDomains;
            ResourceDomain* domain_ = nullptr;
            DmaBuffer* next_        = nullptr;
            bool mapped_ = false, closed_ = false;
            uint64_t physicalAddress_;
            uint64_t virtualAddress_;
            uint64_t size_;
    };

    /* 把任务持有的设备映射、DMA 等资源归为一个可统一回收的集合。 */
    class ResourceDomain {
        public:
            /* 汇总设备资源占用数量，供状态查询使用。 */
            class Statistics {
                    friend class ResourceDomains;

                public:
                    uint32_t getActive() const { return active; }
                    uint32_t getPending() const { return pending; }
                    uint32_t getStopped() const { return stopped; }
                    uint32_t getQuarantined() const { return quarantined; }
                    uint32_t getPages() const { return pages; }

                private:
                    uint32_t active = 0, pending = 0, stopped = 0,
                             quarantined = 0, pages = 0;
            };
            bool active(task::Task& owner);
            bool allowsIrq(task::Task& owner, uint8_t irq);
            bool attach(MmioRegion& region);
            bool attach(DmaBuffer& buffer);
            void closeLease();
            int stop(task::Task& owner);

        private:
            friend class ResourceDomains;
            friend class MmioRegion;
            friend class DmaBuffer;
            enum class State : uint8_t {
                Free,
                Active,
                Pending,
                Stopping,
                Stopped,
                Quarantined
            };
            State state_       = State::Free;
            task::Task* owner_ = nullptr;
            os::wire::PciFunctionInfo function_{};
            uint32_t references_ = 0, dmaPages_ = 0;
            bool exiting_ = false, addressSpaceGone_ = false;
            uint64_t controlMapping_ = 0, controlSize_ = 0;
            uint64_t addressGeneration_ = 0;
            volatile uint8_t* status_   = nullptr;
            uint64_t barBases_[6]{}, barSizes_[6]{};
            MmioRegion* mmio_ = nullptr;
            DmaBuffer* dma_   = nullptr;
            void drop();
            void unmap(uint64_t address, uint64_t size, bool& mapped,
                       uint64_t physical);
            bool reset();
    };

    /* 管理各任务的设备资源，在任务退出或更换地址空间时按顺序清理。 */
    class ResourceDomains {
        public:
            using Statistics = ResourceDomain::Statistics;
            void init();
            bool owned(task::Task& owner);
            Statistics statistics();
            ResourceDomain* claim(task::Task& owner,
                                  const os::wire::PciFunctionInfo& function);
            ResourceDomain* find(task::Task& owner,
                                 const os::wire::PciFunctionInfo& function);
            bool writeConfiguration(uint64_t owner,
                                    const os::wire::PciFunctionInfo& function,
                                    uint16_t offset, uint32_t value);
            void ownerExiting(task::Task& owner);
            void addressSpaceReleased(task::Task& owner);

        private:
            friend class ResourceDomain;
            using State = ResourceDomain::State;
            ResourceDomain domains[32]{};
            task::SpinLock domainLock;
            task::Thread* worker = nullptr;
            void work();
    };
    ResourceDomains& resourceDomains();

    /* 把硬件中断关联到通知端点，并管理屏蔽、确认和关闭。 */
    class InterruptBinding {
        public:
            static InterruptBinding* create(task::Task& task, uint8_t irq,
                                            task::Endpoint* endpoint,
                                            bool levelTriggered = false);
            uint8_t irq() const;
            task::Endpoint* endpoint() const;
            bool requiresAcknowledgement() const { return levelTriggered_; }
            bool acknowledge(task::Task& task) const;
            void destroy();

        private:
            uint8_t irq_;
            task::Endpoint* endpoint_;
            task::Task* owner_;
            uint32_t apicId_;
            bool levelTriggered_;
    };

}  // namespace device
