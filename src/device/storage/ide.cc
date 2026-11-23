/* 驱动实现放在源文件，头文件只公开类型和接口。 */
#include "service/device/storage/ide.h"

#include <os/runtime.h>

namespace storage {

    /* 根据 PCI 模式选定端口，屏蔽设备中断，再复位两个 ATA 通道。 */
    bool IdeController::init(os::Device::Pci function) {
        if (device.open(function)) {
            return false;
        }

        for (uint32_t channel = 0; channel < 2; ++channel) {
            commands[channel] = channel ? 0x170 : 0x1f0;
            controls[channel] = channel ? 0x376 : 0x3f6;

            if (function.getProgrammingInterface() & (1U << (channel * 2))) {
                uint32_t commandBar = 0, controlBar = 0;

                if (function.readConfig(0x10 + channel * 8, commandBar) ||
                    function.readConfig(0x14 + channel * 8, controlBar)) {
                    return false;
                }

                commands[channel] = commandBar & ~3U;
                controls[channel] = (controlBar & ~3U) + 2;
            }

            // 关闭中断后做软件复位，PIO 完成由状态轮询确认，不遗留无人处理的
            // IRQ。
            uint8_t control = 6;

            if (device.writePort(controls[channel], 1, &control)) {
                return false;
            }

            os::sleep(1);
            control = 2;

            if (device.writePort(controls[channel], 1, &control)) {
                return false;
            }

            os::sleep(2);
        }

        return true;
    }

    /* 只写当前控制器通道内的一个寄存器。 */
    bool IdeController::write(uint32_t channel, uint32_t offset,
                              uint8_t value) {
        return device.writePort(commands[channel] + offset, 1, &value) == 0;
    }

    /* 切换主从盘后留出状态稳定时间。 */
    bool IdeController::select(uint32_t channel, uint8_t value) {
        if (!write(channel, 6, value)) {
            return false;
        }

        uint8_t ignored = 0;

        for (unsigned index = 0; index < 4; ++index) {
            if (device.readPort(controls[channel], 1, &ignored)) {
                return false;
            }
        }

        return true;
    }

    /* 先等 BSY 清零，再检查错误和 DRQ；设备不响应也必须在期限内返回。 */
    os::Storage::Error IdeController::wait(uint32_t channel, bool data,
                                           uint64_t deadline) {
        using Error = os::Storage::Error;

        for (uint32_t iteration = 0;; ++iteration) {
            uint8_t status = 0;

            if (device.readPort(commands[channel] + 7, 1, &status)) {
                return Error::Io;
            }

            if (!status || status == 0xff) {
                return Error::DeviceNotFound;
            }

            if (!(status & 0x80)) {
                if (status & 0x21) {
                    return Error::Io;
                }

                if (data ? (status & 8) != 0 : (status & 8) == 0) {
                    return Error::Success;
                }
            }

            if (os::Sys().readMonotonicTime() >= deadline) {
                return Error::Timeout;
            }

            if (!(iteration & 15)) {
                os::yield();
            }
        }
    }

    /* 同一命令的全部扇区共用一个截止时间。 */
    uint64_t IdeDisk::deadline() {
        return os::Sys().readMonotonicTime() + 2000000000ULL;
    }

    /* 按内核允许的 256 字批次传输，兼容大于 512 字节的逻辑扇区。 */
    bool IdeDisk::data(void* buffer, uint32_t bytes, bool write) {
        uint8_t* cursor = static_cast<uint8_t*>(buffer);

        for (uint32_t offset = 0; offset < bytes; offset += 512) {
            const uint32_t words =
                (bytes - offset < 512 ? bytes - offset : 512) / 2;
            const int result = write ? controller_->device.writePort(
                                           controller_->commands[channel_], 2,
                                           cursor + offset, words)
                                     : controller_->device.readPort(
                                           controller_->commands[channel_], 2,
                                           cursor + offset, words);
            if (result) {
                return false;
            }
        }

        return true;
    }

    /* LBA48 先写高半部，再写低半部；所有参数就绪后才发出读写命令。 */
    bool IdeDisk::command(uint64_t block, uint32_t count, bool write) {
        IdeController& bus = *controller_;
        const uint8_t head = 0xe0 | (slave_ << 4) |
                             (identity_.hasLba48() ? 0 : ((block >> 24) & 15));
        if (!bus.select(channel_, head)) {
            return false;
        }

        if (identity_.hasLba48() && (!bus.write(channel_, 2, count >> 8) ||
                                     !bus.write(channel_, 3, block >> 24) ||
                                     !bus.write(channel_, 4, block >> 32) ||
                                     !bus.write(channel_, 5, block >> 40))) {
            return false;
        }

        return bus.write(channel_, 2, count) && bus.write(channel_, 3, block) &&
               bus.write(channel_, 4, block >> 8) &&
               bus.write(channel_, 5, block >> 16) &&
               bus.write(channel_, 7,
                         identity_.hasLba48() ? (write ? 0x34 : 0x24)
                                              : (write ? 0x30 : 0x20));
    }

    /* 通道锁覆盖命令到完成的全过程，防止另一块磁盘改写共享任务文件寄存器。 */
    TransferResult IdeDisk::transfer(uint64_t block, uint32_t count,
                                     void* buffer, size_t bytes, bool write) {
        using Error = os::Storage::Error;
        os::Mutex::Guard guard(controller_->locks[channel_]);
        Error error =
            info().validateTransfer(block, count, buffer, bytes, write);
        if (error != Error::Success) {
            return {error, 0};
        }

        const uint64_t end = deadline();

        if (!command(block, count, write)) {
            __atomic_store_n(&online_, false, __ATOMIC_RELEASE);
            return {Error::Io, 0};
        }

        uint32_t completed = 0;

        for (uint32_t index = 0; index < count; ++index) {
            error = controller_->wait(channel_, true, end);

            if (error != Error::Success ||
                !data(static_cast<uint8_t*>(buffer) +
                          size_t(index) * identity_.getLogicalBlockSize(),
                      identity_.getLogicalBlockSize(), write)) {
                __atomic_store_n(&online_, false, __ATOMIC_RELEASE);
                return {error == Error::Success ? Error::Io : error, completed};
            }

            // 写完数据并不等于磁盘已接受；下一次 DRQ
            // 或最终完成后才能确认这一扇区。
            if (!write) {
                ++completed;
            }
        }

        error = controller_->wait(channel_, false, end);

        if (error != Error::Success) {
            __atomic_store_n(&online_, false, __ATOMIC_RELEASE);
            return {error, completed};
        }

        return {Error::Success, count};
    }

    /* 只把通过 IDENTIFY 的 ATA 硬盘发布为块设备，ATAPI 设备留给独立驱动。 */
    bool IdeDisk::init(IdeController& controller, uint32_t channel,
                       uint32_t slave) {
        controller_ = &controller;
        channel_ = channel;
        slave_ = slave;
        os::Mutex::Guard guard(controller.locks[channel]);

        if (!controller.select(channel, 0xa0 | (slave << 4)) ||
            !controller.write(channel, 2, 0) ||
            !controller.write(channel, 3, 0) ||
            !controller.write(channel, 4, 0) ||
            !controller.write(channel, 5, 0) ||
            !controller.write(channel, 7, 0xec) ||
            controller.wait(channel, true, deadline()) !=
                os::Storage::Error::Success) {
            return false;
        }

        uint16_t words[256]{};

        if (!data(words, sizeof(words), false) || !identity_.decode(words) ||
            controller.wait(channel, false, deadline()) !=
                os::Storage::Error::Success) {
            return false;
        }

        online_ = true;
        return true;
    }

    /* 返回真实几何和缓存能力，未知缓存状态不能冒充已关闭写缓存。 */
    BlockInfo IdeDisk::info() const {
        return {identity_.getBlockCount(),
                identity_.getLogicalBlockSize(),
                128,
                __atomic_load_n(&online_, __ATOMIC_ACQUIRE),
                false,
                !identity_.isCacheKnown()         ? CacheMode::Unknown
                : identity_.isWriteCacheEnabled() ? CacheMode::WriteBack
                                                  : CacheMode::WriteThrough,
                identity_.supportsFlush()};
    }

    /* 读取和写入共用范围检查、通道锁和超时处理。 */
    TransferResult IdeDisk::read(uint64_t block, uint32_t count, void* buffer,
                                 size_t bytes) {
        return transfer(block, count, buffer, bytes, false);
    }

    /* 只有整条命令完成后才确认写入数量。 */
    TransferResult IdeDisk::write(uint64_t block, uint32_t count,
                                  const void* buffer, size_t bytes) {
        return transfer(block, count, const_cast<void*>(buffer), bytes, true);
    }

    /* 使用设备声明支持的 FLUSH 命令；不能用 PIO 传输结束代替缓存落盘。 */
    os::Storage::Error IdeDisk::flush() {
        using Error = os::Storage::Error;
        os::Mutex::Guard guard(controller_->locks[channel_]);

        if (!online_) {
            return Error::DeviceOffline;
        }

        if (identity_.isCacheKnown() && !identity_.isWriteCacheEnabled()) {
            return Error::Success;
        }

        if (!identity_.supportsFlush()) {
            return Error::NotSupported;
        }

        if (!controller_->select(channel_, 0xe0 | (slave_ << 4)) ||
            !controller_->write(channel_, 7,
                                identity_.usesExtendedFlush() ? 0xea : 0xe7)) {
            return Error::Io;
        }

        return controller_->wait(channel_, false, deadline());
    }

    /* 停用单盘不释放共享控制器，以免影响同一控制器上的其他磁盘。 */
    StopResult IdeDisk::stop() {
        os::Mutex::Guard guard(controller_->locks[channel_]);
        __atomic_store_n(&online_, false, __ATOMIC_RELEASE);
        return StopResult::Stopped;
    }

}  // namespace storage
