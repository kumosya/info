/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 * acpi.h - ACPI table definitions and helpers
 *
 */

#ifndef _KERNEL_ACPI_H_
#define _KERNEL_ACPI_H_

#include <stdint.h>

namespace kernel {

struct BootInfo;

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

namespace acpi {
const AcpiSdtHeader* findAcpiTable(const BootInfo* bootInfo,
                                   const char signature[4]);

}  // namespace acpi

}  // namespace kernel

#endif  // _KERNEL_ACPI_H_
