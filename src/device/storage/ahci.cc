/* AHCI 基础 DMA 后端：命令表、数据缓冲区和硬件完成状态分开管理。 */
#include "service/device/storage/ahci.h"

#include <os/service.h>
#include <os/task.h>

#include <os/runtime.h>

namespace storage {

    /* AHCI 命令头的固定硬件布局，每个槽占 32 字节。 */
    class AhciCommandHeader {
       public:
        uint16_t flags, entries;
        volatile uint32_t transferred;
        uint32_t tableLow, tableHigh, reserved[4];
    };

    static_assert(sizeof(AhciCommandHeader) == 32);

    /* 所有硬件等待均有期限，不能因设备一直忙而卡住整个存储服务。 */
    bool AhciController::waitClear(volatile uint32_t* address, uint32_t mask,
                                   uint64_t deadline) {
        while (*address & mask) {
            if (os::Sys().readMonotonicTime() >= deadline) {
                return false;
            }

            os::yield();
        }

        return true;
    }

    /* 接管固件所有权、复位控制器，关闭硬件中断，基础版本通过完成寄存器确认命令。
     */
    bool AhciController::init(os::Device::Pci function) {
        if (device_.open(function) || mapping_.open(device_, 5, 0, 4096)) {
            return false;
        }

        registers_ =
            reinterpret_cast<volatile uint32_t*>(mapping_.getAddress());
        const uint32_t count = (registers_[0] & 31) + 1;

        if (0x100 + count * 0x80 > 4096) {
            mapping_.close();

            if (mapping_.open(device_, 5, 0, 8192)) {
                return false;
            }

            registers_ =
                reinterpret_cast<volatile uint32_t*>(mapping_.getAddress());
        }

        if (registers_[9] & 1) {
            registers_[10] |= 2;

            if (!waitClear(registers_ + 10, 0x11,
                           os::Sys().readMonotonicTime() + 2000000000ULL)) {
                return false;
            }
        }

        registers_[1] = (1U << 31) | 1;

        if (!waitClear(registers_ + 1, 1,
                       os::Sys().readMonotonicTime() + 1000000000ULL)) {
            return false;
        }

        registers_[1] = (1U << 31);
        address64_ = (registers_[0] & (1U << 31)) != 0;
        ports_ = registers_[3] & static_cast<uint32_t>((1ULL << count) - 1);

        uint32_t command = 0;

        if (function.readConfig(4, command) ||
            function.writeConfig(4, (command & 0xffff) | 6)) {
            return false;
        }

        __atomic_store_n(&online_, true, __ATOMIC_RELEASE);
        return true;
    }

    /* 命令列表、接收 FIS 和命令表共用一个连续页，数据使用另一个独立连续页。 */
    bool AhciController::preparePort(uint32_t port) {
        volatile uint32_t* registers = registers_ + (0x100 + port * 0x80) / 4;

        if (!(ports_ & (1U << port)) || (registers[10] & 15) != 3 ||
            ((registers[10] >> 8) & 15) != 1) {
            return false;
        }

        const uint64_t deadline = os::Sys().readMonotonicTime() + 1000000000ULL;
        registers[6] &= ~1U;

        if (!waitClear(registers + 6, 1U << 15, deadline)) {
            return false;
        }

        registers[6] &= ~(1U << 4);

        if (!waitClear(registers + 6, 1U << 14, deadline) ||
            tables_[port].create(device_, 4096) ||
            buffers_[port].create(device_, 4096)) {
            return false;
        }

        const uint64_t table = tables_[port].getDeviceAddress();
        const uint64_t buffer = buffers_[port].getDeviceAddress();

        if (!address64_ &&
            (table > UINT32_MAX - 4095 || buffer > UINT32_MAX - 4095)) {
            return false;
        }

        os::Runtime::clearMemory(
            reinterpret_cast<void*>(tables_[port].getVirtualAddress()), 4096);
        registers[0] = table;
        registers[1] = table >> 32;
        registers[2] = table + 1024;
        registers[3] = (table + 1024) >> 32;
        registers[5] = 0;
        registers[4] = UINT32_MAX;
        registers[12] = UINT32_MAX;
        registers[6] |= 0x16;
        registers[6] |= 1;
        // 签名来自设备发回的首个 FIS；接收引擎未启动时，读取它可能仍是复位值。
        while (registers[9] == 0 || registers[9] == UINT32_MAX) {
            if (os::Sys().readMonotonicTime() >= deadline) {
                return false;
            }

            os::yield();
        }

        if (registers[9] != 0x00000101) {
            return false;
        }

        ready_[port] = true;
        return true;
    }

    /* 发出一条 ATA 命令；命令完成前不复制读数据，也不复用描述符或数据页。 */
    os::Storage::Error AhciController::command(uint32_t port, uint8_t opcode,
                                               uint64_t block, uint32_t count,
                                               uint32_t bytes, bool write,
                                               bool lba48) {
        using Error = os::Storage::Error;

        if (!online() || port >= 32 || !ready_[port] || bytes > 4096) {
            return Error::DeviceOffline;
        }

        volatile uint32_t* registers = registers_ + (0x100 + port * 0x80) / 4;
        const uint64_t deadline = os::Sys().readMonotonicTime() + 2000000000ULL;

        if (!waitClear(registers + 8, 0x88, deadline) || registers[13] ||
            registers[14]) {
            stopLocked();
            return Error::Timeout;
        }

        uint8_t* memory =
            reinterpret_cast<uint8_t*>(tables_[port].getVirtualAddress());
        AhciCommandHeader* header =
            reinterpret_cast<AhciCommandHeader*>(memory);
        os::Runtime::clearMemory(memory, 32);
        os::Runtime::clearMemory(memory + 1280, 256);
        header->flags = 5 | (write ? (1U << 6) : 0);
        header->entries = bytes ? 1 : 0;
        const uint64_t table = tables_[port].getDeviceAddress() + 1280;
        header->tableLow = table;
        header->tableHigh = table >> 32;

        uint8_t* fis = memory + 1280;
        fis[0] = 0x27;
        fis[1] = 0x80;
        fis[2] = opcode;
        fis[4] = block;
        fis[5] = block >> 8;
        fis[6] = block >> 16;
        fis[7] = 0x40 | (lba48 ? 0 : ((block >> 24) & 15));

        if (lba48) {
            fis[8] = block >> 24;
            fis[9] = block >> 32;
            fis[10] = block >> 40;
            fis[13] = count >> 8;
        }

        fis[12] = count;

        if (bytes) {
            uint32_t* entry = reinterpret_cast<uint32_t*>(memory + 1408);
            const uint64_t data = buffers_[port].getDeviceAddress();
            entry[0] = data;
            entry[1] = data >> 32;
            entry[3] = bytes - 1;
        }

        registers[4] = UINT32_MAX;
        registers[12] = UINT32_MAX;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        registers[14] = 1;

        for (uint32_t spins = 0; registers[14] & 1; ++spins) {
            if (registers[4] & (1U << 30)) {
                stopLocked();
                return Error::Io;
            }

            if (!(spins & 63)) {
                if (os::Sys().readMonotonicTime() >= deadline) {
                    stopLocked();
                    return Error::Timeout;
                }

                os::yield();
            } else {
                asm volatile("pause");
            }
        }

        __atomic_thread_fence(__ATOMIC_ACQUIRE);

        if ((registers[4] & (1U << 30)) || (registers[8] & 0x21) ||
            header->transferred != bytes) {
            stopLocked();
            return Error::Io;
        }

        return Error::Success;
    }

    /* 先由内核确认停止 DMA，再关闭用户句柄；隔离状态仍由内核保留物理页。 */
    StopResult AhciController::stopLocked() {
        if (!online()) {
            return stopped_;
        }

        __atomic_store_n(&online_, false, __ATOMIC_RELEASE);
        const int result = device_.stop();
        stopped_ = result == 0 ? StopResult::Stopped
                   : result == static_cast<int>(os::ErrorCode::IoError)
                       ? StopResult::Quarantined
                       : StopResult::Unconfirmed;
        for (uint32_t port = 0; port < 32; ++port) {
            tables_[port].close();
            buffers_[port].close();
        }

        mapping_.close();
        device_.close();
        registers_ = nullptr;
        return stopped_;
    }

    /* 识别磁盘后才发布容量；失败端口不会成为一个看似可读写的块设备。 */
    bool AhciDisk::init(AhciController& controller, uint32_t port) {
        controller_ = &controller;
        port_ = port;
        os::Mutex::Guard guard(controller.lock_);

        if (port >= 32 || !controller.online() ||
            !controller.preparePort(port) ||
            controller.command(port, 0xec, 0, 0, 512, false, true) !=
                os::Storage::Error::Success) {
            return false;
        }

        const uint16_t* words = reinterpret_cast<const uint16_t*>(
            controller.buffers_[port].getVirtualAddress());
        initialized_ = identity_.decode(words);
        return initialized_;
    }

    /* 最大传输长度来自本驱动实际分配的 DMA 数据页大小。 */
    BlockInfo AhciDisk::info() const {
        return {identity_.getBlockCount(),
                identity_.getLogicalBlockSize(),
                4096 / identity_.getLogicalBlockSize(),
                initialized_ && controller_->online(),
                false,
                !identity_.isCacheKnown()         ? CacheMode::Unknown
                : identity_.isWriteCacheEnabled() ? CacheMode::WriteBack
                                                  : CacheMode::WriteThrough,
                identity_.supportsFlush()};
    }

    /* 控制器锁覆盖缓冲区拷贝和命令完成，错误恢复不会撤销另一个线程正在访问的映射。
     */
    TransferResult AhciDisk::transfer(uint64_t first, uint32_t count,
                                      void* buffer, size_t bytes, bool write) {
        using Error = os::Storage::Error;
        os::Mutex::Guard guard(controller_->lock_);
        const Error validation =
            info().validateTransfer(first, count, buffer, bytes, write);
        if (validation != Error::Success) {
            return {validation, 0};
        }

        const uint32_t size = count * identity_.getLogicalBlockSize();
        void* data = reinterpret_cast<void*>(
            controller_->buffers_[port_].getVirtualAddress());
        if (write) {
            os::Runtime::copyBytes(data, buffer, size);
        }

        const uint8_t opcode = identity_.hasLba48() ? (write ? 0x35 : 0x25)
                                                    : (write ? 0xca : 0xc8);
        const Error result = controller_->command(
            port_, opcode, first, count, size, write, identity_.hasLba48());
        if (result == Error::Success && !write) {
            os::Runtime::copyBytes(buffer, data, size);
        }

        return {result, result == Error::Success ? count : 0};
    }

    /* 同步入口共用命令提交和完成处理。 */
    TransferResult AhciDisk::read(uint64_t first, uint32_t count, void* buffer,
                                  size_t bytes) {
        return transfer(first, count, buffer, bytes, false);
    }

    /* 写入成功只表示命令已完成，持久化仍需后续 flush。 */
    TransferResult AhciDisk::write(uint64_t first, uint32_t count,
                                   const void* buffer, size_t bytes) {
        return transfer(first, count, const_cast<void*>(buffer), bytes, true);
    }

    /* 缓存状态未知时保留错误，不能直接报告数据已落盘。 */
    os::Storage::Error AhciDisk::flush() {
        using Error = os::Storage::Error;
        os::Mutex::Guard guard(controller_->lock_);

        switch (info().getFlushAction()) {
            case FlushAction::Complete:
                return Error::Success;
            case FlushAction::Offline:
                return Error::DeviceOffline;
            case FlushAction::Unknown:
                return Error::CapabilityUnknown;
            case FlushAction::Unsupported:
                return Error::NotSupported;
            case FlushAction::Command:
                return controller_->command(
                    port_, identity_.usesExtendedFlush() ? 0xea : 0xe7, 0, 0, 0,
                    false, identity_.hasLba48());
        }

        return Error::Io;
    }

    /* 基础恢复策略停止整个控制器；后续多队列恢复再细化为端口级隔离。 */
    StopResult AhciDisk::stop() {
        os::Mutex::Guard guard(controller_->lock_);
        return controller_->stopLocked();
    }

}  // namespace storage
