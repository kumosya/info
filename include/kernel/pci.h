/*
 * PCI 总线和设备配置。
 */

#pragma once
#include <cstdint>

#include "kernel/sync.h"
#include "service/common/records.h"
#include <os/device.h>

namespace device {
    /* 表示 PCI 总线扫描和配置访问所需的信息。 */
    class PciBus {
        public:
            uint32_t enumerate(os::wire::PciFunctionInfo* functions,
                               uint32_t capacity);
            bool read(const os::wire::PciFunctionInfo& function,
                      uint16_t offset, uint32_t& value);
            bool write(const os::wire::PciFunctionInfo& function,
                       uint16_t offset, uint32_t value);
            bool getMemoryBar(const os::wire::PciFunctionInfo& function,
                              uint8_t barIndex, uint64_t& base, uint64_t& size);

        private:
            task::SpinLock lock_;
    };
    PciBus& pciBus();

    /* 封装 PCI 设备发现、配置读取和设备信息查询。 */
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

}  // namespace device
