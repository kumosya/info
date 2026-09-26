#pragma once

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

const AcpiSdtHeader* findAcpiTable(const BootInfo* bootInfo,
                                   const char signature[4]);

}  // namespace kernel
