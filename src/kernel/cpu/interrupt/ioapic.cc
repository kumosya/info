/*
 * 根据固件信息管理外部 IRQ 的目标、屏蔽和触发方式。
 */

#include "kernel/acpi.h"
#include "kernel/boot.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/sync.h"

namespace cpu {

    constexpr uint64_t directMapBase  = 0xffff800000000000ULL;
    constexpr uint64_t directMapSize  = 64ULL * 1024 * 1024 * 1024;
    constexpr uint32_t maxIoApics     = 8;
    constexpr uint32_t legacyIrqCount = 16;

    /* ACPI 中断控制器表头，后接变长 CPU 或控制器记录。 */
    struct [[gnu::packed]] IoApicMadt {
            acpi::AcpiSdtHeader header;
            uint32_t localApicAddress;
            uint32_t flags;
    };

    /* MADT 变长条目的公共头，必须先检查长度才能解释后续字段。 */
    struct [[gnu::packed]] IoApicMadtEntryHeader {
            uint8_t type;
            uint8_t length;
    };

    /* 外部中断控制器的寄存器物理地址与全局中断起点。 */
    struct [[gnu::packed]] MadtIoApic {
            IoApicMadtEntryHeader header;
            uint8_t id;
            uint8_t reserved;
            uint32_t address;
            uint32_t globalSystemInterruptBase;
    };

    /* 传统 IRQ 到全局中断号的覆盖规则，含极性与触发方式。 */
    struct [[gnu::packed]] MadtInterruptOverride {
            IoApicMadtEntryHeader header;
            uint8_t bus;
            uint8_t source;
            uint32_t globalSystemInterrupt;
            uint16_t flags;
    };

    /* 内核保存的控制器寄存器窗口、全局中断起点和可用引脚数。 */
    struct IoApic {
            volatile uint32_t* registers;
            uint32_t globalSystemInterruptBase;
            uint32_t redirectionCount;
    };

    /* 一个传统 IRQ 对应的实际全局中断号和固件属性。 */
    struct IrqRoute {
            uint32_t globalSystemInterrupt;
            uint16_t flags;
    };

    static IoApic ioApics[maxIoApics];
    static uint32_t ioApicCount;
    static IrqRoute irqRoutes[legacyIrqCount];
    static task::SpinLock ioApicLock;

    template <typename Type>

    /*
     * 把受支持物理范围内的控制器地址转换为内核直接映射指针，越出直接映射范围时拒绝访问。
     */
    static Type* mapPhysical(uint64_t physicalAddress) {
        if (physicalAddress >= directMapSize) {
            return nullptr;
        }

        return reinterpret_cast<Type*>(directMapBase + physicalAddress);
    }

    /*
     * 通过统一 ACPI 查表入口取得 MADT，后续用它枚举外部中断控制器和路由覆盖。
     */
    static const IoApicMadt* findMadt(const ::BootInfo* bootInfo) {
        return reinterpret_cast<const IoApicMadt*>(
            acpi::Tables(bootInfo).find("APIC"));
    }

    /*
     * 先选择 I/O APIC
     * 寄存器编号，再从数据窗口读取数值；调用者负责串行化这两个步骤。
     */
    static uint32_t readIoApic(const IoApic& ioApic, uint8_t index) {
        ioApic.registers[0] = index;

        return ioApic.registers[4];
    }

    /*
     * 通过寄存器选择器和数据窗口写 I/O APIC，防止把寄存器编号当成线性地址偏移。
     */
    static void writeIoApic(const IoApic& ioApic, uint8_t index,
                            uint32_t value) {
        ioApic.registers[0] = index;
        ioApic.registers[4] = value;
    }

    /*
     * 找到覆盖指定全局中断编号的 I/O
     * APIC，并将后续引脚计算限定在它的路由范围内。
     */
    static IoApic* findIoApic(uint32_t globalSystemInterrupt) {
        for (uint32_t index = 0; index < ioApicCount; ++index) {
            IoApic& ioApic = ioApics[index];

            if (globalSystemInterrupt >= ioApic.globalSystemInterruptBase &&
                globalSystemInterrupt < ioApic.globalSystemInterruptBase +
                                            ioApic.redirectionCount) {
                return &ioApic;
            }
        }

        return nullptr;
    }

    /*
     * 在控制器锁已持有时设置指定全局中断入口的屏蔽位，保留原路由其他属性。
     */
    static void maskGlobalInterruptLocked(uint32_t globalSystemInterrupt) {
        IoApic* ioApic = findIoApic(globalSystemInterrupt);

        if (ioApic == nullptr) {
            return;
        }

        const uint32_t pin =
            globalSystemInterrupt - ioApic->globalSystemInterruptBase;
        const uint8_t lowRegister = static_cast<uint8_t>(0x10 + pin * 2);
        writeIoApic(*ioApic, lowRegister,
                    readIoApic(*ioApic, lowRegister) | (1U << 16));
    }

    /*
     * 从 MADT 收集 I/O APIC 及传统 IRQ
     * 的路由覆盖，初始化后先屏蔽所有引脚，待驱动明确绑定才启用。
     */
    void initControllers(const BootInfo* bootInfo) {
        // 先按传统 IRQ
        // 与全局中断号一一对应建立默认路由，固件覆盖条目随后修正它。
        for (uint32_t irq = 0; irq < legacyIrqCount; ++irq) {
            irqRoutes[irq].globalSystemInterrupt = irq;
            irqRoutes[irq].flags                 = 0;
        }

        const IoApicMadt* madt = findMadt(bootInfo);

        if (madt == nullptr || madt->header.length < sizeof(IoApicMadt)) {
            device::serialWrite(
                "[INTERRUPT] ACPI MADT unavailable; external IRQ routing "
                "disabled\n");

            return;
        }

        const uint8_t* cursor =
            reinterpret_cast<const uint8_t*>(madt) + sizeof(IoApicMadt);
        const uint8_t* end =
            reinterpret_cast<const uint8_t*>(madt) + madt->header.length;

        // MADT 是变长记录，必须先核对每条记录的长度才能按具体结构读取。
        while (cursor + sizeof(IoApicMadtEntryHeader) <= end) {
            const IoApicMadtEntryHeader* header =
                reinterpret_cast<const IoApicMadtEntryHeader*>(cursor);

            if (header->length < sizeof(IoApicMadtEntryHeader) ||
                cursor + header->length > end) {
                break;
            }

            if (header->type == 1 && header->length >= sizeof(MadtIoApic) &&
                ioApicCount < maxIoApics) {
                const MadtIoApic* entry =
                    reinterpret_cast<const MadtIoApic*>(cursor);

                // 登记控制器覆盖的全局中断起点，并从版本寄存器得到实际引脚数量。
                IoApic& ioApic = ioApics[ioApicCount];
                ioApic.registers =
                    mapPhysical<volatile uint32_t>(entry->address);

                if (ioApic.registers == nullptr) {
                    cursor += header->length;
                    continue;
                }

                ioApic.globalSystemInterruptBase =
                    entry->globalSystemInterruptBase;
                ioApic.redirectionCount =
                    ((readIoApic(ioApic, 1) >> 16) & 0xff) + 1;
                ++ioApicCount;
            } else if (header->type == 2 &&
                       header->length >= sizeof(MadtInterruptOverride)) {
                const MadtInterruptOverride* entry =
                    reinterpret_cast<const MadtInterruptOverride*>(cursor);

                // 传统总线的 IRQ
                // 可能接到不同的全局引脚；同时保留固件指定的电平和极性。
                if (entry->bus == 0 && entry->source < legacyIrqCount) {
                    irqRoutes[entry->source].globalSystemInterrupt =
                        entry->globalSystemInterrupt;
                    irqRoutes[entry->source].flags = entry->flags;
                }
            }

            cursor += header->length;
        }

        {
            // 在驱动安装处理函数前统一屏蔽所有引脚，防止未准备好的设备中断进入内核。
            ::task::InterruptSpinLockGuard guard(ioApicLock);

            for (uint32_t index = 0; index < ioApicCount; ++index) {
                const IoApic& ioApic = ioApics[index];

                for (uint32_t pin = 0; pin < ioApic.redirectionCount; ++pin) {
                    maskGlobalInterruptLocked(ioApic.globalSystemInterruptBase +
                                              pin);
                }
            }
        }

        if (ioApicCount == 0) {
            device::serialWrite(
                "[INTERRUPT] no I/O APIC found; external IRQ routing "
                "disabled\n");

            return;
        }

        device::serialWrite("[INTERRUPT] I/O APIC routing initialized\n");
    }

    /*
     * 把传统 IRQ 按固件映射路由到指定 CPU
     * 和中断向量，同时处理极性与电平触发属性。
     */
    bool setIrq(uint8_t irq, uint8_t vector, uint32_t apicId,
                bool levelTriggered) {
        if (irq >= legacyIrqCount || vector < 32) {
            return false;
        }

        // 选择寄存器和读写数据共用一个窗口，整个路由修改必须串行，不能被另一
        // CPU 插入。
        ::task::InterruptSpinLockGuard guard(ioApicLock);
        const IrqRoute& route = irqRoutes[irq];
        IoApic* ioApic        = findIoApic(route.globalSystemInterrupt);

        if (ioApic == nullptr) {
            return false;
        }

        const uint32_t pin =
            route.globalSystemInterrupt - ioApic->globalSystemInterruptBase;
        const uint8_t lowRegister = static_cast<uint8_t>(0x10 + pin * 2);
        uint32_t low              = vector;

        // 将固件要求与调用者的电平触发要求合并到路由低位中。
        const uint16_t polarity = route.flags & 0x3;
        const uint16_t trigger  = (route.flags >> 2) & 0x3;

        if (polarity == 3 || levelTriggered) {
            low |= 1U << 13;
        }

        if (trigger == 3 || levelTriggered) {
            low |= 1U << 15;
        }

        // 先设置目标 CPU，再写允许触发的低半部，避免中断短暂送往旧目标。
        writeIoApic(*ioApic, static_cast<uint8_t>(lowRegister + 1),
                    apicId << 24);
        writeIoApic(*ioApic, lowRegister, low);

        return true;
    }

    /*
     * 按传统 IRQ 查出实际全局中断并将其屏蔽，非法编号直接忽略。
     */
    void maskIrq(uint8_t irq) {
        if (irq >= legacyIrqCount) {
            return;
        }

        ::task::InterruptSpinLockGuard guard(ioApicLock);
        maskGlobalInterruptLocked(irqRoutes[irq].globalSystemInterrupt);
    }

}  // namespace cpu
