#include "format.h"

namespace fs {

const char* formatName(Format format) {
    switch (format) {
        case Format::Ext2:
            return "ext2";
        case Format::Ext3:
            return "ext3";
        case Format::Fat32:
            return "fat32";
        case Format::ExFat:
            return "exfat";
        case Format::Iso9660:
            return "iso9660";
        default:
            return "unknown";
    }
}

Format detectFormat(const uint8_t* bootData, size_t bootSize,
                    const uint8_t* isoDescriptor, size_t isoSize) {
    if (probeExt3(bootData, bootSize)) return Format::Ext3;
    if (probeExt2(bootData, bootSize)) return Format::Ext2;
    if (probeFat32(bootData, bootSize)) return Format::Fat32;
    if (probeExFat(bootData, bootSize)) return Format::ExFat;
    if (probeIso9660(isoDescriptor, isoSize)) return Format::Iso9660;
    return Format::Unknown;
}

}  // namespace fs
