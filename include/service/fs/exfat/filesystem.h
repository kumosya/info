/*
 * 对应文件系统的格式识别和实现。
 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace vfs::exfat {
    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            bool detect(const uint8_t* data, size_t size);
    };
}  // namespace vfs::exfat
