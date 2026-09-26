#pragma once

#include <stddef.h>
#include <stdint.h>

#include "block.h"

namespace fs::ext3 {

constexpr uint16_t superMagic          = 0xef53;
constexpr uint32_t rootInode           = 2;
constexpr uint16_t modeDirectory       = 0x4000;
constexpr uint16_t modeRegular         = 0x8000;
constexpr uint32_t blockCacheSlotCount = 16;

#pragma pack(push, 1)
struct Superblock {
    uint32_t inodesCount;
    uint32_t blocksCount;
    uint32_t reservedBlocksCount;
    uint32_t freeBlocksCount;
    uint32_t freeInodesCount;
    uint32_t firstDataBlock;
    uint32_t logBlockSize;
    uint32_t logFragmentSize;
    uint32_t blocksPerGroup;
    uint32_t fragmentsPerGroup;
    uint32_t inodesPerGroup;
    uint32_t mountTime;
    uint32_t writeTime;
    uint16_t mountCount;
    uint16_t maximumMountCount;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minorRevision;
    uint32_t lastCheck;
    uint32_t checkInterval;
    uint32_t creatorOs;
    uint32_t revision;
    uint16_t defaultReservedUid;
    uint16_t defaultReservedGid;
    uint32_t firstInode;
    uint16_t inodeSize;
    uint16_t blockGroupNumber;
    uint32_t compatibleFeatures;
    uint32_t incompatibleFeatures;
    uint32_t readOnlyCompatibleFeatures;
    uint8_t remainder[1024 - 104];
};

struct GroupDescriptor {
    uint32_t blockBitmap;
    uint32_t inodeBitmap;
    uint32_t inodeTable;
    uint16_t freeBlocksCount;
    uint16_t freeInodesCount;
    uint16_t usedDirectoriesCount;
    uint16_t padding;
    uint32_t reserved[3];
};

struct Inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t accessTime;
    uint32_t changeTime;
    uint32_t modificationTime;
    uint32_t deletionTime;
    uint16_t gid;
    uint16_t linksCount;
    uint32_t sectorsCount;
    uint32_t flags;
    uint32_t osDependent1;
    uint32_t blocks[15];
    uint32_t generation;
    uint32_t fileAcl;
    uint32_t directoryAcl;
    uint32_t fragmentAddress;
    uint8_t osDependent2[12];
};

struct RawDirectoryEntry {
    uint32_t inode;
    uint16_t recordLength;
    uint8_t nameLength;
    uint8_t fileType;
};
#pragma pack(pop)

static_assert(sizeof(Superblock) == 1024);
static_assert(sizeof(GroupDescriptor) == 32);
static_assert(sizeof(Inode) == 128);

struct FileStatus {
    uint32_t inode;
    uint16_t mode;
    uint64_t size;
    uint16_t uid;
    uint16_t gid;
    uint16_t links;
    uint32_t accessTime;
    uint32_t modificationTime;
    uint32_t changeTime;
};

struct DirectoryEntry {
    uint32_t inode;
    uint8_t type;
    char name[48];
};

class Volume {
   public:
    bool mount(SectorDevice& device);
    bool lookup(const char* path, FileStatus& status);
    int64_t read(uint32_t inode, uint64_t offset, void* buffer, size_t size);
    int64_t write(uint32_t inode, uint64_t offset, const void* buffer,
                  size_t size);
    bool truncate(uint32_t inode);
    bool create(const char* path, bool directory);
    bool remove(const char* path);
    bool link(const char* existing, const char* newPath);
    bool changeMode(const char* path, uint16_t mode);
    bool changeOwner(const char* path, uint16_t uid, uint16_t gid);
    bool changeTimes(const char* path, uint32_t accessTime,
                     uint32_t modificationTime);
    int readDirectory(const char* path, uint32_t index, DirectoryEntry& entry);
    int readDirectoryBatch(const char* path, uint32_t index,
                           DirectoryEntry* entries, uint32_t capacity);
    bool sync();

   private:
    bool readBlock(uint32_t block, void* buffer);
    bool writeBlock(uint32_t block, const void* buffer);
    bool readSuperblock();
    bool writeSuperblock();
    bool readGroup(uint32_t group, GroupDescriptor& descriptor);
    bool writeGroup(uint32_t group, const GroupDescriptor& descriptor);
    bool readInode(uint32_t number, Inode& inode);
    bool writeInode(uint32_t number, const Inode& inode);
    uint32_t getDataBlock(const Inode& inode, uint32_t logical);
    uint32_t ensureDataBlock(Inode& inode, uint32_t logical);
    uint32_t allocateBlock(uint32_t preferredGroup = 0);
    void freeBlock(uint32_t block);
    uint32_t allocateInode(bool directory, uint32_t preferredGroup = 0);
    void freeInode(uint32_t inode, bool directory);
    bool clearInodeBlocks(Inode& inode);
    bool lookupInDirectory(const Inode& directory, const char* name,
                           size_t nameLength, uint32_t& inode);
    bool resolve(const char* path, uint32_t& number, Inode* inode = nullptr);
    bool resolveParent(const char* path, uint32_t& parentNumber, Inode& parent,
                       char* name, size_t capacity);
    bool addDirectoryEntry(uint32_t directoryNumber, Inode& directory,
                           uint32_t child, const char* name, uint8_t type);
    bool removeDirectoryEntry(uint32_t directoryNumber, Inode& directory,
                              const char* name);
    bool directoryEmpty(const Inode& directory);
    bool readPointer(uint32_t block, uint32_t index, uint32_t& value);
    bool writePointer(uint32_t block, uint32_t index, uint32_t value);

    SectorDevice* device_ = nullptr;
    Superblock superblock_{};
    uint32_t blockSize_  = 0;
    uint32_t groupCount_ = 0;
    uint8_t blockCache_[blockCacheSlotCount][4096]{};
    uint32_t cachedBlocks_[blockCacheSlotCount]{};
    uint32_t cacheAges_[blockCacheSlotCount]{};
    bool cacheValid_[blockCacheSlotCount]{};
    uint32_t cacheClock_ = 0;
};

}  // namespace fs::ext3
