#ifndef _OS_PCI_H
#define _OS_PCI_H

#include <os/abi.h>

namespace os {

enum class PciOperation : uint32_t { Enumerate = 1, ReadConfig, WriteConfig };

struct PciRequest {
    PciOperation operation;
    uint32_t capacity;
    PciFunctionInfo function;
    uint16_t offset;
    uint16_t reserved;
    uint32_t value;
    PciFunctionInfo* functions;
};

static_assert(sizeof(PciFunctionInfo) == 16);

int enumeratePci(PciFunctionInfo* functions, uint32_t capacity);
int readPciConfig(const PciFunctionInfo& function, uint16_t offset,
                  uint32_t& value);
int writePciConfig(const PciFunctionInfo& function, uint16_t offset,
                   uint32_t value);

}  // namespace os

#endif
