/* 传统 PCI IDE 控制器；先提供有界 PIO 读写，所有端口访问由内核检查。 */
#pragma once
#include <os/device.h>
#include <os/service.h>
#include <os/sync.h>
#include <os/task.h>

#include "service/device/storage/ata.h"
#include "service/device/storage/block_backend.h"

namespace storage {

    /* 一个控制器拥有两个通道；同一通道的主从盘必须共享锁，不能交错写命令寄存器。
     */
    class IdeController {
            friend class IdeDisk;
            os::Mutex locks[2];
            uint16_t commands[2]{}, controls[2]{};
            os::Device device;

        public:
            /* 兼容模式使用标准端口；原生 PCI 模式使用对应 I/O BAR。 */
            bool init(os::Device::Pci function);

        private:
            /* 写单个任务文件寄存器，数据端口的大批量传输另行处理。 */
            bool write(uint32_t channel, uint32_t offset, uint8_t value);

            /* 选择主从盘后读取备用状态，确保设备有足够时间切换。 */
            bool select(uint32_t channel, uint8_t value);

            /* 忙状态解除后才解释错误位和数据就绪位，每次轮询都有明确期限。 */
            os::Storage::Error wait(uint32_t channel, bool data,
                                    uint64_t deadline);
    };

    /* 一个 ATA 磁盘对应一个块设备；同通道不同磁盘仍由控制器统一串行发命令。 */
    class IdeDisk final : public BlockBackend {
            IdeController* controller_ = nullptr;
            uint32_t channel_ = 0;
            uint32_t slave_ = 0;
            AtaIdentity identity_;
            bool online_ = false;

            /* 等待期限从一次命令开始计算，不能在每个扇区重新延长。 */
            static uint64_t deadline();

            /* 一个逻辑扇区可能有 4096 字节，拆成受限的 256 字端口传输。 */
            bool data(void* buffer, uint32_t bytes, bool write);

            /* 高位寄存器先写、低位寄存器后写，最后发命令；LBA28
             * 不允许静默截断地址。 */
            bool command(uint64_t block, uint32_t count, bool write);

            /* 逐扇区确认完成；失败时只返回已确认的前缀，并停止继续使用故障磁盘。
             */
            TransferResult transfer(uint64_t block, uint32_t count,
                                    void* buffer, size_t bytes, bool write);

        public:
            /* IDENTIFY 失败或 ATAPI 设备不会发布为 ATA
             * 硬盘，避免用错误命令访问光驱。 */
            bool init(IdeController& controller, uint32_t channel,
                      uint32_t slave);

            /* PIO 命令最多批量 128 个逻辑扇区，外层还会按 IPC 容量继续拆分。 */
            BlockInfo info() const override;

            /* 读取的缓冲区和完成数量均以逻辑扇区为单位。 */
            TransferResult read(uint64_t block, uint32_t count, void* buffer,
                                size_t bytes) override;

            /* 写入沿用同一套范围检查和主从盘互斥。 */
            TransferResult write(uint64_t block, uint32_t count,
                                 const void* buffer, size_t bytes) override;

            /* 启用写缓存时必须发送 ATA FLUSH；不支持该命令就明确返回错误。 */
            os::Storage::Error flush() override;

            /* 单盘停用不关闭控制器租约，防止另一通道或同通道的磁盘被一并关闭。
             */
            StopResult stop() override;
    };

}  // namespace storage
