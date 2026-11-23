/*
 * 验证 ACPI 入口和表校验和，按签名查找固件表。
 */

#include "kernel/acpi.h"

#include "kernel/boot.h"

namespace acpi {

    constexpr uint64_t directMapBase = 0xffff800000000000ULL;
    constexpr uint64_t directMapSize = 64ULL * 1024 * 1024 * 1024;

    /* 固件提供的 ACPI 根入口，同时容纳旧 RSDT 和新 XSDT 地址。 */
    struct [[gnu::packed]] Rsdp {
            char signature[8];
            uint8_t checksum;
            char oemId[6];
            uint8_t revision;
            uint32_t rsdtAddress;
            uint32_t length;
            uint64_t xsdtAddress;
            uint8_t extendedChecksum;
            uint8_t reserved[3];
    };

    /*
     * 按固定长度比较签名字节，不要求固件签名以空字符结尾。
     */
    static bool bytesEqual(const char* left, const char* right,
                           uint32_t length) {
        for (uint32_t index = 0; index < length; ++index) {
            if (left[index] != right[index]) {
                return false;
            }
        }

        return true;
    }

    /*
     * 将指定范围内字节按八位累加，和为零表示 ACPI 校验通过。
     */
    static bool checksumValid(const void* address, uint32_t length) {
        const uint8_t* bytes = static_cast<const uint8_t*>(address);
        uint8_t sum          = 0;

        for (uint32_t index = 0; index < length; ++index) {
            sum = static_cast<uint8_t>(sum + bytes[index]);
        }

        return sum == 0;
    }

    /*
     * 逐字节组合小端整数，兼容固件表中可能未对齐的地址字段。
     */
    static uint64_t readLittleEndian(const uint8_t* address, uint32_t size) {
        uint64_t value = 0;

        for (uint32_t index = 0; index < size; ++index) {
            value |= static_cast<uint64_t>(address[index]) << (index * 8);
        }

        return value;
    }

    template <typename Type>

    /*
     * 将可直接访问的物理地址转换成只读内核指针，超出已建立直接映射的范围时失败。
     */
    static const Type* mapPhysical(uint64_t physicalAddress) {
        if (physicalAddress >= directMapSize) {
            return nullptr;
        }

        return reinterpret_cast<const Type*>(directMapBase + physicalAddress);
    }

    /*
     * 检查 ACPI 表指针、最小与最大长度以及整表校验和，防止按损坏表头遍历内存。
     */
    static bool tableValid(const AcpiSdtHeader* table) {
        return table != nullptr && table->length >= sizeof(AcpiSdtHeader) &&
               table->length <= 1024 * 1024 &&
               checksumValid(table, table->length);
    }

    /*
     * 验证 RSDT 或 XSDT
     * 后，按相应地址宽度枚举子表，返回签名与校验均通过的匹配表。
     */
    static const AcpiSdtHeader* findInRoot(uint64_t rootPhysicalAddress,
                                           bool extended,
                                           const char signature[4]) {
        const AcpiSdtHeader* root =
            mapPhysical<AcpiSdtHeader>(rootPhysicalAddress);

        if (!tableValid(root) ||
            !bytesEqual(root->signature, extended ? "XSDT" : "RSDT", 4)) {
            return nullptr;
        }

        // RSDT 保存四字节物理地址，XSDT
        // 保存八字节；条目数由已经验证的根表长度计算。
        const uint32_t entrySize = extended ? 8 : 4;
        const uint32_t entryCount =
            (root->length - sizeof(AcpiSdtHeader)) / entrySize;
        const uint8_t* entries =
            reinterpret_cast<const uint8_t*>(root) + sizeof(AcpiSdtHeader);

        // 每个子表都单独验证长度和校验和，只有有效且签名匹配的表才能交给调用者。
        for (uint32_t index = 0; index < entryCount; ++index) {
            const uint64_t physicalAddress =
                readLittleEndian(entries + index * entrySize, entrySize);
            const AcpiSdtHeader* table =
                mapPhysical<AcpiSdtHeader>(physicalAddress);

            if (tableValid(table) &&
                bytesEqual(table->signature, signature, 4)) {
                return table;
            }
        }

        return nullptr;
    }

    /*
     * 从引导提供的 RSDP 出发查表，先验证入口；优先使用有效 XSDT，找不到时回退到
     * RSDT。
     */
    const AcpiSdtHeader* Tables::find(const char signature[4]) const {
        const BootInfo* bootInfo = boot_;

        if (bootInfo == nullptr || bootInfo->getAcpiRoot() == nullptr) {
            return nullptr;
        }

        // 先验证所有 ACPI 版本都有的前二十字节，不能直接信任扩展地址。
        const Rsdp* rsdp = static_cast<const Rsdp*>(bootInfo->getAcpiRoot());

        if (!bytesEqual(rsdp->signature, "RSD PTR ", 8) ||
            !checksumValid(rsdp, 20)) {
            return nullptr;
        }

        // 新版本还要核对扩展长度和校验和；优先查 XSDT，失败后保留旧 RSDT
        // 的兼容路径。
        if (rsdp->revision >= 2 && rsdp->length >= sizeof(Rsdp) &&
            rsdp->length <= 4096 && checksumValid(rsdp, rsdp->length) &&
            rsdp->xsdtAddress != 0) {
            const AcpiSdtHeader* table =
                findInRoot(rsdp->xsdtAddress, true, signature);

            if (table != nullptr) {
                return table;
            }
        }

        return findInRoot(rsdp->rsdtAddress, false, signature);
    }

}  // namespace acpi
