#include "format.h"

namespace vfs {

const char* FormatProbe::formatName(Format format) {
    switch (format) {
        case Format::Ext3:
            return "ext3";
        case Format::Ext4:
            return "ext4";
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

Format FormatProbe::detectFormat(const uint8_t* bootData, size_t bootSize,
                    const uint8_t* isoDescriptor, size_t isoSize) {
    if (FormatProbe::probeExt4(bootData, bootSize)) return Format::Ext4;
    if (FormatProbe::probeExt3(bootData, bootSize)) return Format::Ext3;
    if (FormatProbe::probeFat32(bootData, bootSize)) return Format::Fat32;
    if (FormatProbe::probeExFat(bootData, bootSize)) return Format::ExFat;
    if (FormatProbe::probeIso9660(isoDescriptor, isoSize)) return Format::Iso9660;
    return Format::Unknown;
}

}  // namespace vfs
