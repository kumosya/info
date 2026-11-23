/*
 * 对应文件系统的格式识别和实现。
 */

#pragma once
#include <cstddef>
#include <cstdint>

#include "service/fs/vfs.h"

namespace fs::iso {

    /* 读取 ISO9660 光盘文件系统的目录和文件数据。 */
    class Filesystem final : public fs::FileSystem {
        public:
            bool mount(fs::SectorDevice& device) override;
            bool lookup(const char* path, fs::FileStatus& status) override;
            int64_t read(const fs::FileStatus& status, uint64_t offset,
                         void* buffer, size_t size) override;
            int readDirectory(const char* path, uint32_t index,
                              fs::DirectoryEntry& entry) override;

            bool readOnly() const override {
                return true;
            }

        private:
            /* 保存文件或目录在所属文件系统中的定位信息。 */
            struct Node {
                    uint32_t extent;
                    uint32_t size;
                    bool directory;
            };

            bool readBlock(uint32_t block, void* buffer);
            bool findInDirectory(const Node& directory, const char* name,
                                 size_t nameLength, Node& status);
            enum class RecordResult { Entry, End, Error };
            RecordResult readRecord(const Node& directory, uint32_t offset,
                                    uint8_t* block, const uint8_t*& record,
                                    uint32_t& nextOffset);
            bool resolve(const char* path, Node& status);

            fs::SectorDevice* device_ = nullptr;
            Node root_{};
    };
}  // namespace fs::iso

