#include "kernel/acpi.h"

#include "kernel/boot.h"

namespace kernel::acpi {

namespace {

constexpr uint64_t directMapBase = 0xffff800000000000ULL;
constexpr uint64_t directMapSize = 64ULL * 1024 * 1024 * 1024;

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

bool bytesEqual(const char* left, const char* right, uint32_t length) {
    for (uint32_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool checksumValid(const void* address, uint32_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(address);
    uint8_t sum          = 0;
    for (uint32_t index = 0; index < length; ++index) {
        sum = static_cast<uint8_t>(sum + bytes[index]);
    }
    return sum == 0;
}

uint64_t readLittleEndian(const uint8_t* address, uint32_t size) {
    uint64_t value = 0;
    for (uint32_t index = 0; index < size; ++index) {
        value |= static_cast<uint64_t>(address[index]) << (index * 8);
    }
    return value;
}

template <typename Type>
const Type* mapPhysical(uint64_t physicalAddress) {
    if (physicalAddress >= directMapSize) {
        return nullptr;
    }
    return reinterpret_cast<const Type*>(directMapBase + physicalAddress);
}

bool tableValid(const AcpiSdtHeader* table) {
    return table != nullptr && table->length >= sizeof(AcpiSdtHeader) &&
           table->length <= 1024 * 1024 && checksumValid(table, table->length);
}

const AcpiSdtHeader* findInRoot(uint64_t rootPhysicalAddress, bool extended,
                                const char signature[4]) {
    const AcpiSdtHeader* root = mapPhysical<AcpiSdtHeader>(rootPhysicalAddress);
    if (!tableValid(root) ||
        !bytesEqual(root->signature, extended ? "XSDT" : "RSDT", 4)) {
        return nullptr;
    }
    const uint32_t entrySize = extended ? 8 : 4;
    const uint32_t entryCount =
        (root->length - sizeof(AcpiSdtHeader)) / entrySize;
    const uint8_t* entries =
        reinterpret_cast<const uint8_t*>(root) + sizeof(AcpiSdtHeader);
    for (uint32_t index = 0; index < entryCount; ++index) {
        const uint64_t physicalAddress =
            readLittleEndian(entries + index * entrySize, entrySize);
        const AcpiSdtHeader* table =
            mapPhysical<AcpiSdtHeader>(physicalAddress);
        if (tableValid(table) && bytesEqual(table->signature, signature, 4)) {
            return table;
        }
    }
    return nullptr;
}

}  // namespace

const AcpiSdtHeader* Tables::find(const char signature[4]) const {
    const BootInfo* bootInfo = boot_;
    if (bootInfo == nullptr || bootInfo->getAcpiRoot() == nullptr) {
        return nullptr;
    }
    const Rsdp* rsdp = static_cast<const Rsdp*>(bootInfo->getAcpiRoot());
    if (!bytesEqual(rsdp->signature, "RSD PTR ", 8) ||
        !checksumValid(rsdp, 20)) {
        return nullptr;
    }
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

}  // namespace kernel::acpi
