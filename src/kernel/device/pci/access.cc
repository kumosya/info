/*
 * 串行访问 PCI 配置端口，枚举设备并探测内存窗口。
 */

#include "kernel/device.h"
#include "kernel/pci.h"
#include "kernel/sync.h"
#include "service/common/records.h"

namespace device {

    static PciBus configurationBus;

    /*
     * 将总线、设备、功能和配置偏移编码成传统 PCI 配置地址，并设置访问使能位。
     */
    static uint32_t pciAddress(const services::wire::PciFunctionInfo& function,
                               uint16_t offset) {
        return 0x80000000U | (static_cast<uint32_t>(function.bus) << 16) |
               (static_cast<uint32_t>(function.device) << 11) |
               (static_cast<uint32_t>(function.function) << 8) |
               (offset & 0xfc);
    }

    /*
     * 通过 0xcf8 选择配置地址，再由 0xcfc 读取一个 32
     * 位配置值；调用者负责持总线锁。
     */
    static uint32_t readConfigRaw(const services::wire::PciFunctionInfo& function,
                                  uint16_t offset) {
        const uint32_t address = pciAddress(function, offset);
        asm volatile("outl %0, %1" : : "a"(address), "Nd"(0xcf8));
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(0xcfc));

        return value;
    }

    /*
     * 通过传统配置端口写入一个 32
     * 位值；必须与其他配置访问串行，避免地址选择被改写。
     */
    static void writeConfigRaw(const services::wire::PciFunctionInfo& function,
                               uint16_t offset, uint32_t value) {
        const uint32_t address = pciAddress(function, offset);
        asm volatile("outl %0, %1" : : "a"(address), "Nd"(0xcf8));
        asm volatile("outl %0, %1" : : "a"(value), "Nd"(0xcfc));
    }

    /*
     * 检查当前配置机制支持的段号、设备号和功能号范围。
     */
    static bool validFunction(const services::wire::PciFunctionInfo& function) {
        return function.segment == 0 && function.device < 32 &&
               function.function < 8;
    }

    /*
     * 返回全局 PCI 配置总线对象，使配置端口访问共用一把锁。
     */
    PciBus& pciBus() { return configurationBus; }

    /*
     * 遍历总线与设备，先检测功能零，再按多功能标志枚举其余功能，最多填满调用方数组。
     */
    uint32_t PciBus::enumerate(services::wire::PciFunctionInfo* functions,
                               uint32_t capacity) {
        if (functions == nullptr || capacity == 0) {
            return 0;
        }

        // 对每个设备先探测功能零；厂商号全一表示该位置没有设备，不应继续把寄存器值当有效信息。
        uint32_t count = 0;

        for (uint16_t bus = 0; bus < 256 && count < capacity; ++bus) {
            for (uint8_t device = 0; device < 32 && count < capacity;
                 ++device) {
                services::wire::PciFunctionInfo probe{};
                probe.bus      = static_cast<uint8_t>(bus);
                probe.device   = device;
                probe.function = 0;
                uint32_t identity;

                if (!read(probe, 0, identity) ||
                    (identity & 0xffff) == 0xffff) {
                    continue;
                }

                uint32_t header;
                read(probe, 0x0c, header);

                // 头类型的多功能位决定是否继续检查功能一到七，避免把单功能设备重复枚举。
                const uint8_t functionCount =
                    (header & 0x00800000U) != 0 ? 8 : 1;

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

                    // 把硬件定位信息和设备类别分开填入返回记录，上层随后可按身份与类别选择驱动。
                    services::wire::PciFunctionInfo& result = functions[count++];
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

    /*
     * 检查目标与四字节偏移后，在总线锁内完成配置读取。
     */
    bool PciBus::read(const services::wire::PciFunctionInfo& function,
                      uint16_t offset, uint32_t& value) {
        if (!validFunction(function) || offset >= 256 || (offset & 3) != 0) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        value = readConfigRaw(function, offset);

        return true;
    }

    /*
     * 检查目标与四字节偏移后，在总线锁内完成配置写入；更高层仍负责设备所有权限制。
     */
    bool PciBus::write(const services::wire::PciFunctionInfo& function,
                       uint16_t offset, uint32_t value) {
        if (!validFunction(function) || offset >= 256 || (offset & 3) != 0) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        writeConfigRaw(function, offset, value);

        return true;
    }

    /*
     * 探测内存 BAR 的地址和大小，兼容 32 位与 64
     * 位布局；探测期间临时关闭地址解码，读完掩码后恢复原配置。
     */
    bool PciBus::getMemoryBar(const services::wire::PciFunctionInfo& function,
                              uint8_t barIndex, uint64_t& base,
                              uint64_t& size) {
        if (!validFunction(function) || barIndex >= 6) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        const uint16_t offset = static_cast<uint16_t>(0x10 + barIndex * 4);

        // I/O BAR 不在此接口支持范围；64 位内存 BAR
        // 还会占用下一个槽，因此不能从最后一个槽开始。
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

        // 先保存原配置并临时关闭 I/O
        // 与内存地址解码，避免全一探测值被设备当成实际访问地址。
        const uint32_t command = readConfigRaw(function, 4);
        writeConfigRaw(function, 4, (command & 0xffff) & ~3U);
        writeConfigRaw(function, offset, 0xffffffffU);

        if (is64Bit) {
            writeConfigRaw(function, offset + 4, 0xffffffffU);
        }

        // 写全一后读回硬件实现的地址掩码；读取完成立即还原 BAR 和命令寄存器。
        const uint32_t maskLow = readConfigRaw(function, offset);
        const uint32_t maskHigh =
            is64Bit ? readConfigRaw(function, offset + 4) : 0;
        writeConfigRaw(function, offset, originalLow);

        if (is64Bit) {
            writeConfigRaw(function, offset + 4, originalHigh);
        }

        writeConfigRaw(function, 4, command & 0xffff);

        // 清除 BAR 属性位得到基址，掩码取反加一得到窗口大小；32 位窗口按 32
        // 位宽度计算。
        base = (static_cast<uint64_t>(originalHigh) << 32) |
               (originalLow & ~0xfULL);
        const uint64_t mask =
            (static_cast<uint64_t>(maskHigh) << 32) | (maskLow & ~0xfULL);
        size = is64Bit ? (~mask) + 1
                       : static_cast<uint64_t>(
                             static_cast<uint32_t>(~(maskLow & ~0xfU)) + 1U);

        return base != 0 && size != 0;
    }

}  // namespace device
