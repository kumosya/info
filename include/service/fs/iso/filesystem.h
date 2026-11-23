/*
 * 对应文件系统的格式识别和实现。
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "service/fs/block.h"
#include "service/fs/vfs.h"

/* 读取 ISO9660 光盘文件系统的目录和文件数据。 */
class Iso9660FileSystem final : public vfs::FileSystem {
    public:
        bool mount(vfs::SectorDevice& device) override;
        bool lookup(const char* path, vfs::FileStatus& status) override;
        int64_t read(const vfs::FileStatus& status, uint64_t offset,
                     void* buffer, size_t size) override;
        int readDirectory(const char* path, uint32_t index,
                          vfs::DirectoryEntry& entry) override;
        bool readOnly() const override { return true; }

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

        vfs::SectorDevice* device_ = nullptr;
        Node root_{};
};

namespace vfs::iso {
    /* 检查磁盘内容是否符合该文件系统格式，并提供实例创建入口。 */
    class Probe {
        public:
            bool detect(const uint8_t* data, size_t size);
            uint64_t blockOffset(uint32_t block);
    };
}  // namespace vfs::iso
