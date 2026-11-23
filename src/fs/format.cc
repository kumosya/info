#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/fat/filesystem.h"
#include "service/fs/iso/filesystem.h"
#include "service/fs/mount.h"

namespace fs {

    /* 探测对象保存启动扇区和光盘描述块的读取范围。 */
    FormatProbe::FormatProbe(const uint8_t* bootData, size_t bootSize,
                             const uint8_t* isoDescriptor, size_t isoSize)
        : bootData_(bootData),
          bootSize_(bootSize),
          isoDescriptor_(isoDescriptor),
          isoSize_(isoSize) {
    }

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

    Format FormatProbe::detectFormat() {
        const uint8_t* bootData = bootData_;
        const size_t bootSize = bootSize_;
        const uint8_t* isoDescriptor = isoDescriptor_;
        const size_t isoSize = isoSize_;
        if (ext4::Probe(bootData, bootSize).detect()) {
            return Format::Ext4;
        }

        if (ext3::Probe(bootData, bootSize).detect()) {
            return Format::Ext3;
        }

        if (fat::Probe(bootData, bootSize).detect()) {
            return Format::Fat32;
        }

        if (exfat::Probe(bootData, bootSize).detect()) {
            return Format::ExFat;
        }

        if (iso::Probe(isoDescriptor, isoSize).detect()) {
            return Format::Iso9660;
        }

        return Format::Unknown;
    }

}  // namespace fs
