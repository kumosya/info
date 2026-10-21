#pragma once

#include <cstddef>
#include <cstdint>

#include "block.h"

namespace fs::ext4 {

using vfs::SectorDevice;

constexpr uint16_t superMagic    = 0xef53;
constexpr uint32_t rootInode     = 2;
constexpr uint16_t modeDirectory = vfs::modeDirectory;
constexpr uint16_t modeRegular   = vfs::modeRegular;

using FileStatus     = vfs::FileStatus;
using DirectoryEntry = vfs::DirectoryEntry;

class Filesystem final : public vfs::FileSystem {
   public:
    bool mount(vfs::SectorDevice& device) override;
    bool lookup(const char* path, FileStatus& status) override;
    bool statInode(uint32_t inode, FileStatus& status) override;
    bool retainOpenFile(uint32_t inode) override { return retainInode(inode); }
    bool releaseOpenFile(uint32_t inode) override { return releaseInode(inode); }
    int64_t read(const FileStatus& status, uint64_t offset, void* buffer,
                 size_t size) override;
    int64_t write(const FileStatus& status, uint64_t offset, const void* buffer,
                  size_t size) override;
    bool truncate(uint32_t inode) override;
    bool create(const char* path, bool directory) override;
    bool remove(const char* path) override;
    bool rename(const char* existing, const char* newPath) override;
    bool changeOwner(const char* path, uint16_t uid, uint16_t gid) override;
    bool changeTimes(const char* path, uint32_t accessTime, uint32_t modificationTime) override;
    bool changeMode(const char* path, uint16_t mode) override;
    int64_t read(uint32_t inode, uint64_t offset, void* buffer, size_t size);
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
                           uint32_t child, const char* name, uint8_t type);
    bool removeDirectoryEntry(const uint8_t* directory, const char* name);
    bool directoryEmpty(const uint8_t* directory);
    bool resolve(const char* path, uint32_t& number, uint8_t* inode);
    bool lookupInDirectory(const uint8_t* directory, const char* name,
                           size_t nameLength, uint32_t& inode);
    uint64_t getDataBlock(const uint8_t* inode, uint32_t logical);
    uint64_t findExtent(const uint8_t* node, uint32_t logical, uint16_t depth,
                        bool root);

    SectorDevice* device_          = nullptr;
    uint64_t blocksCount_          = 0;
    uint32_t inodesCount_          = 0;
    uint32_t firstDataBlock_       = 0;
    uint32_t blockSize_            = 0;
    uint32_t blocksPerGroup_       = 0;
    uint32_t inodesPerGroup_       = 0;
    uint32_t groupCount_           = 0;
    uint16_t inodeSize_            = 0;
    uint16_t descriptorSize_       = 0;
    uint32_t incompatibleFeatures_ = 0;
    uint32_t readOnlyFeatures_     = 0;
    bool writeSupported_           = false;
    uint8_t superblock_[1024]{};
    uint8_t inodeBuffer_[4096]{};
    uint8_t inodeScratch_[2][4096]{};
    uint8_t blockBuffer_[4096]{};
};

}  // namespace fs::ext4
