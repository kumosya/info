/* 服务对象的成员函数实现。 */
#include "service/fs/ext4/filesystem.h"

namespace fs::ext4::detail {

    /* 按小端字节序读取 32 位字段。 */
    uint32_t disk_encoding::read32(const uint8_t* data) {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }

    /* 按小端字节序写入 32 位字段。 */
    void disk_encoding::write32(uint8_t* data, uint32_t value) {
        for (uint32_t index = 0; index < 4; ++index) {
            data[index] = static_cast<uint8_t>(value >> (index * 8));
        }
    }

    /* 读取文件数据段的块数。 */
    uint32_t disk_encoding::extentLength(const uint8_t* entry) {
        const uint16_t length = read16(entry + 4);
        return length == 0 ? 32768 : (length > 32768 ? length - 32768 : length);
    }

    /* 填写文件数据段的逻辑块和物理块位置。 */
    void disk_encoding::setExtent(uint8_t* entry, uint32_t logical,
                                  uint64_t physical, uint16_t length) {
        write32(entry, logical);
        write16(entry + 4, length);
        write16(entry + 6, static_cast<uint16_t>(physical >> 32));
        write32(entry + 8, static_cast<uint32_t>(physical));
    }

    /* 按目录项中的长度比较文件名。 */
    bool disk_encoding::namesEqual(const char* left, const char* right,
                                   size_t length) {
        for (size_t index = 0; index < length; ++index) {
            if (left[index] != right[index]) {
                return false;
            }
        }

        return true;
    }

}  // namespace fs::ext4::detail
