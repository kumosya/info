/*
 * 文件系统格式探测。
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace vfs {

    enum class Format : uint8_t { Unknown, Ext3, Ext4, Fat32, ExFat, Iso9660 };

    /* 根据磁盘内容识别文件系统格式，向挂载流程提供探测结果。 */
    class FormatProbe {
        public:
            const char* formatName(Format format);
            Format detectFormat(const uint8_t* bootData, size_t bootSize,
                                const uint8_t* isoDescriptor, size_t isoSize);
    };

}  // namespace vfs
