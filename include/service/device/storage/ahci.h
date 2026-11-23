/* SATA AHCI 控制器和 ATA 磁盘；控制器拥有寄存器映射及 DMA 缓冲区。 */
#pragma once
#include <os/device.h>
#include <os/sync.h>

#include "service/device/storage/ata.h"
#include "service/device/storage/block_backend.h"

namespace storage {

    /* 统一管理控制器停机。基础阶段串行发命令，避免错误恢复时与其他端口竞争映射。
     */
    class AhciController {
            friend class AhciDisk;
            os::Device device_;
            os::Device::Mmio mapping_;
            os::Device::Dma tables_[32], buffers_[32];
            os::Mutex lock_;
            volatile uint32_t* registers_ = nullptr;
            uint32_t ports_ = 0;
            bool address64_ = false;
            bool online_ = false;
            bool ready_[32]{};
            StopResult stopped_ = StopResult::Unconfirmed;

            bool waitClear(volatile uint32_t* address, uint32_t mask,
                           uint64_t deadline);
            bool preparePort(uint32_t port);
            os::Storage::Error command(uint32_t port, uint8_t opcode,
                                       uint64_t block, uint32_t count,
                                       uint32_t bytes, bool write, bool lba48);
            StopResult stopLocked();

        public:
            bool init(os::Device::Pci function);

            bool online() const {
                return __atomic_load_n(&online_, __ATOMIC_ACQUIRE);
            }

            uint32_t ports() const {
                return ports_;
            }
    };

    /* 一个 AHCI 端口对应一块 ATA 磁盘，容量和命令支持由 IDENTIFY 决定。 */
    class AhciDisk final : public BlockBackend {
            AhciController* controller_ = nullptr;
            uint32_t port_ = 0;
            AtaIdentity identity_;
            bool initialized_ = false;

            TransferResult transfer(uint64_t first, uint32_t count,
                                    void* buffer, size_t bytes, bool write);

        public:
            bool init(AhciController& controller, uint32_t port);
            BlockInfo info() const override;
            TransferResult read(uint64_t first, uint32_t count, void* buffer,
                                size_t bytes) override;
            TransferResult write(uint64_t first, uint32_t count,
                                 const void* buffer, size_t bytes) override;
            os::Storage::Error flush() override;
            StopResult stop() override;
    };

}  // namespace storage
