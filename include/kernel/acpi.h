/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 * acpi.h - ACPI table definitions and helpers
 *
 */

/*
 * ACPI 固件表查询。
 */

#ifndef _KERNEL_ACPI_H_
#define _KERNEL_ACPI_H_

#include <cstdint>

class BootInfo;

namespace acpi {

    struct [[gnu::packed]] AcpiSdtHeader {
            char signature[4];
            uint32_t length;
            uint8_t revision;
            uint8_t checksum;
            char oemId[6];
            char oemTableId[8];
            uint32_t oemRevision;
            uint32_t creatorId;
            uint32_t creatorRevision;
    };

    /* 收集并查询固件提供的 ACPI 表，供内核识别硬件布局。 */
    class Tables {
        public:
            explicit Tables(const ::BootInfo* boot) : boot_(boot) {}
            const AcpiSdtHeader* find(const char signature[4]) const;

        private:
            const ::BootInfo* boot_;
    };

}  // namespace acpi

#endif  // _KERNEL_ACPI_H_
