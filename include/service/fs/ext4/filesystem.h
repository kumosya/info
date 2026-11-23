/* ext4 卷对象与磁盘字段编码。 */

#pragma once
#include <cstddef>
#include <cstdint>

#include "service/fs/vfs.h"

namespace fs::ext4 {

    using fs::SectorDevice;

    constexpr uint16_t superMagic = 0xef53;
    constexpr uint32_t rootInode = 2;
    constexpr uint16_t modeDirectory = fs::modeDirectory;
    constexpr uint16_t modeRegular = fs::modeRegular;

    using FileStatus = fs::FileStatus;
    using DirectoryEntry = fs::DirectoryEntry;

    /* 实现本头文件对应的磁盘文件系统，并管理挂载实例的打开状态。 */
    class Filesystem final : public fs::FileSystem {
        public:
            bool mount(fs::SectorDevice& device) override;
            bool lookup(const char* path, FileStatus& status) override;
            bool statInode(uint32_t inode, FileStatus& status) override;

            bool retainOpenFile(uint32_t inode) override {
                return retainInode(inode);
            }

            bool releaseOpenFile(uint32_t inode) override {
                return releaseInode(inode);
            }

            int64_t read(const FileStatus& status, uint64_t offset,
                         void* buffer, size_t size) override;
            int64_t write(const FileStatus& status, uint64_t offset,
                          const void* buffer, size_t size) override;
            bool truncate(uint32_t inode) override;
            bool create(const char* path, bool directory) override;
            bool remove(const char* path) override;
            bool rename(const char* existing, const char* newPath) override;
            bool changeOwner(const char* path, uint16_t uid,
                             uint16_t gid) override;
            bool changeTimes(const char* path, uint32_t accessTime,
                             uint32_t modificationTime) override;
            bool changeMode(const char* path, uint16_t mode) override;
            int64_t read(uint32_t inode, uint64_t offset, void* buffer,
                         size_t size);
            int readDirectory(const char* path, uint32_t index,
                              DirectoryEntry& entry) override;
            int readDirectoryBatch(const char* path, uint32_t index,
                                   DirectoryEntry* entries, uint32_t capacity);
            bool sync() override;
            bool readOnly() const override;

        private:
            bool reclaimUnlinked(uint32_t inode) override;
            bool readBlock(uint64_t block, void* buffer);
            bool writeBlock(uint64_t block, const void* buffer);
            bool readGroup(uint32_t group, uint8_t* descriptor);
            bool writeGroup(uint32_t group, uint8_t* descriptor);
            bool readInode(uint32_t number, uint8_t* inode);
            bool writeInode(uint32_t number, const uint8_t* inode);
            uint32_t allocateBlock(uint32_t preferredGroup = 0);
            void freeBlock(uint32_t block);
            uint32_t allocateInode(bool directory, uint32_t preferredGroup = 0);
            void freeInode(uint32_t number, bool directory);
            uint64_t ensureDataBlock(uint8_t* inode, uint32_t logical);
            bool clearInodeBlocks(uint8_t* inode);
            bool resolveParent(const char* path, uint32_t& parentNumber,
                               uint8_t* parent, char* name, size_t capacity);
            bool addDirectoryEntry(uint32_t directoryNumber, uint8_t* directory,
                                   uint32_t child, const char* name,
                                   uint8_t type);
            bool removeDirectoryEntry(const uint8_t* directory,
                                      const char* name);
            bool directoryEmpty(const uint8_t* directory);
            bool resolve(const char* path, uint32_t& number, uint8_t* inode);
            bool lookupInDirectory(const uint8_t* directory, const char* name,
                                   size_t nameLength, uint32_t& inode);
            uint64_t getDataBlock(const uint8_t* inode, uint32_t logical);
            uint64_t findExtent(const uint8_t* node, uint32_t logical,
                                uint16_t depth, bool root);

            SectorDevice* device_ = nullptr;
            uint64_t blocksCount_ = 0;
            uint32_t inodesCount_ = 0;
            uint32_t firstDataBlock_ = 0;
            uint32_t blockSize_ = 0;
            uint32_t blocksPerGroup_ = 0;
            uint32_t inodesPerGroup_ = 0;
            uint32_t groupCount_ = 0;
            uint16_t inodeSize_ = 0;
            uint16_t descriptorSize_ = 0;
            uint32_t incompatibleFeatures_ = 0;
            uint32_t readOnlyFeatures_ = 0;
            bool writeSupported_ = false;
            uint8_t superblock_[1024]{};
            uint8_t inodeBuffer_[4096]{};
            uint8_t inodeScratch_[2][4096]{};
            uint8_t blockBuffer_[4096]{};
    };

}  // namespace fs::ext4

// namespace fs::ext4

namespace fs::ext4::detail {

    /* 封装磁盘结构的字节序和字段读写，避免依赖本机结构体布局。 */
    namespace disk_encoding {

        inline uint16_t read16(const uint8_t* data) {
            return static_cast<uint16_t>(data[0]) |
                   (static_cast<uint16_t>(data[1]) << 8);
        }

        uint32_t read32(const uint8_t* data);

        inline uint64_t read64(const uint8_t* data) {
            return static_cast<uint64_t>(read32(data)) |
                   (static_cast<uint64_t>(read32(data + 4)) << 32);
        }

        inline uint64_t read48(const uint8_t* data) {
            return static_cast<uint64_t>(read32(data)) |
                   (static_cast<uint64_t>(read16(data + 4)) << 32);
        }

        inline void write16(uint8_t* data, uint16_t value) {
            data[0] = static_cast<uint8_t>(value);
            data[1] = static_cast<uint8_t>(value >> 8);
        }

        void write32(uint8_t* data, uint32_t value);

        inline uint64_t readFreeBlocks(const uint8_t* superblock) {
            return read32(superblock + 12);
        }

        inline void writeFreeBlocks(uint8_t* superblock, uint64_t count) {
            write32(superblock + 12, static_cast<uint32_t>(count));
        }

        inline uint32_t readFreeInodes(const uint8_t* superblock) {
            return read32(superblock + 16);
        }

        inline void writeFreeInodes(uint8_t* superblock, uint32_t count) {
            write32(superblock + 16, count);
        }

        uint32_t extentLength(const uint8_t* entry);

        void setExtent(uint8_t* entry, uint32_t logical, uint64_t physical,
                       uint16_t length);

        inline uint16_t alignedDirectoryEntry(uint32_t length) {
            return static_cast<uint16_t>((length + 3U) & ~3U);
        }

        bool namesEqual(const char* left, const char* right, size_t length);
    }  // namespace disk_encoding

}  // namespace fs::ext4::detail
