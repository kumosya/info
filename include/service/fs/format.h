#pragma once

#include <cstddef>
#include <cstdint>

namespace vfs {

enum class Format : uint8_t { Unknown, Ext3, Ext4, Fat32, ExFat, Iso9660 };

class FormatProbe {
   public:
    const char* formatName(Format format);
    Format detectFormat(const uint8_t* bootData, size_t bootSize,
                        const uint8_t* isoDescriptor, size_t isoSize);
};

}  // namespace vfs
