/* 服务对象的成员函数实现。 */
#include "service/fs/server.h"

namespace fs::server {

    /* 合并路径中的连续分隔符。 */
    bool path_text::collapseSlashes(char* path, size_t capacity) {
        size_t read = 0, write = 0;
        while (read < capacity && path[read]) {
            const char value = path[read++];
            if (value == '/' && write && path[write - 1] == '/') {
                continue;
            }

            path[write++] = value;
        }

        if (read == capacity) {
            return false;
        }

        path[write] = 0;
        return true;
    }

    /* 按完整目录层级比较路径前缀。 */
    bool path_text::startsWithPath(const char* path, const char* prefix) {
        uint32_t index = 0;
        while (prefix[index] != '\0' && path[index] == prefix[index]) {
            ++index;
        }

        return prefix[index] == '\0' &&
               (path[index] == '\0' || path[index] == '/');
    }

    /* 查找文字中是否包含指定内容。 */
    bool path_text::containsText(const char* text, const char* token) {
        if (text == nullptr || token == nullptr || token[0] == '\0') {
            return false;
        }

        for (uint32_t start = 0; text[start] != '\0'; ++start) {
            uint32_t index = 0;
            while (token[index] != '\0' &&
                   text[start + index] == token[index]) {
                ++index;
            }

            if (token[index] == '\0') {
                return true;
            }
        }

        return false;
    }

    /* 绑定块设备并读取其传输参数。 */
    bool BlockDeviceClient::init(uint64_t endpoint, uint32_t id) {
        if (!os::detail::StorageClient().open(device_, id, endpoint)) {
            return false;
        }

        identity_.id = device_.getId();
        identity_.major = device_.getMajor();
        identity_.minor = device_.getMinor();
        identity_.parentId = device_.getParentId();
        identity_.partitionNumber = device_.getPartitionNumber();
        identity_.startBlock = device_.getStartBlock();
        os::Runtime::copyString(identity_.name, device_.getName(),
                                sizeof(identity_.name));
        os::Runtime::copyString(identity_.partitionUuid,
                                device_.getPartitionUuid(),
                                sizeof(identity_.partitionUuid));
        os::Runtime::copyString(identity_.hardwarePath,
                                device_.getHardwarePath(),
                                sizeof(identity_.hardwarePath));
        return true;
    }

    /* 分批读取连续扇区。 */
    bool BlockDeviceClient::readSectors(uint64_t block, uint32_t count,
                                        void* buffer) {
        uint32_t done = 0;
        return usable() && device_.read(block, count, buffer,
                                        uint64_t(count) * 512, done) == 0;
    }

    /* 分批写入连续扇区。 */
    bool BlockDeviceClient::writeSectors(uint64_t block, uint32_t count,
                                         const void* buffer) {
        uint32_t done = 0;
        return usable() && device_.write(block, count, buffer,
                                         uint64_t(count) * 512, done) == 0;
    }

    /* 读取块设备的身份与分区信息。 */
    bool BlockDeviceClient::probe() {
        uint8_t block[512]{};
        uint32_t done = 0;
        return usable() && device_.getBlockCount() &&
               device_.read(0, 1, block, sizeof(block), done) == 0;
    }

}  // namespace fs::server
