#pragma once

#include <cstdint>

#include "vfs.h"

namespace vfs {

class SectorDevice {
   public:
    virtual bool readSector(uint64_t sector, void* buffer)        = 0;
    virtual bool writeSector(uint64_t sector, const void* buffer) = 0;
    virtual bool readSectors(uint64_t firstSector, uint32_t count,
                             void* buffer) {
        for (uint32_t index = 0; index < count; ++index) {
            if (!readSector(firstSector + index,
                            static_cast<uint8_t*>(buffer) + index * 512)) {
                return false;
            }
        }
        return true;
    }
    virtual bool writeSectors(uint64_t firstSector, uint32_t count,
                              const void* buffer) {
        for (uint32_t index = 0; index < count; ++index) {
            if (!writeSector(
                    firstSector + index,
                    static_cast<const uint8_t*>(buffer) + index * 512)) {
                return false;
            }
        }
        return true;
    }
    virtual bool flush()                 = 0;
    virtual uint64_t sectorCount() const = 0;
    virtual bool readOnly() const        = 0;
};

}  // namespace vfs
