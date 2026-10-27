/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 * acpi.h - ACPI table definitions and helpers
 *
 */

#ifndef _KERNEL_ACPI_H_
#define _KERNEL_ACPI_H_

#include <cstdint>

class BootInfo;

namespace kernel::acpi {

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

class Tables {
   public:
    explicit Tables(const ::BootInfo* boot) : boot_(boot) {}
    const AcpiSdtHeader* find(const char signature[4]) const;

   private:
    const ::BootInfo* boot_;
};

}  // namespace kernel::acpi

#endif  // _KERNEL_ACPI_H_
