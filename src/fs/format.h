#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vfs {

enum class Format : uint8_t { Unknown, Ext3, Ext4, Fat32, ExFat, Iso9660 };

class FormatProbe {
 public:
static bool probeExt4(const uint8_t* data, size_t size);
static bool probeExt3(const uint8_t* data, size_t size);
static bool probeFat32(const uint8_t* data, size_t size);
static bool probeExFat(const uint8_t* data, size_t size);
static bool probeIso9660(const uint8_t* data, size_t size);
static uint64_t fatBlockOffset(uint32_t cluster, uint32_t sectorsPerCluster,
                        uint32_t firstDataSector, uint32_t bytesPerSector);
static uint64_t isoBlockOffset(uint32_t block);
static const char* formatName(Format format);
static Format detectFormat(const uint8_t* bootData, size_t bootSize,
                    const uint8_t* isoDescriptor, size_t isoSize);

};

}  // namespace vfs
