#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/fat/filesystem.h"
#include "service/fs/exfat/filesystem.h"
#include "service/fs/iso/filesystem.h"
#include "service/fs/format.h"

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
    if (ext4::Probe().detect(bootData, bootSize)) return Format::Ext4;
    if (ext3::Probe().detect(bootData, bootSize)) return Format::Ext3;
    if (fat::Probe().detect(bootData, bootSize)) return Format::Fat32;
    if (exfat::Probe().detect(bootData, bootSize)) return Format::ExFat;
    if (iso::Probe().detect(isoDescriptor, isoSize)) return Format::Iso9660;
    return Format::Unknown;
}

}  // namespace vfs
