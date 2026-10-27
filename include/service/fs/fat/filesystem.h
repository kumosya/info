#pragma once

#include <cstddef>
#include <cstdint>

#include "service/fs/block.h"
#include "service/fs/vfs.h"

class Fat32FileSystem final : public vfs::FileSystem {
   public:
    bool mount(vfs::SectorDevice& device) override;
    bool lookup(const char* path, vfs::FileStatus& status) override;
    int64_t read(const vfs::FileStatus& status, uint64_t offset, void* buffer,
                 size_t size) override;
    int64_t write(const vfs::FileStatus& status, uint64_t offset,
                  const void* buffer, size_t size) override;
    bool truncate(uint32_t inode) override;
    bool truncate(const vfs::FileStatus& status) override;
    bool create(const char* path, bool directory) override;
    bool remove(const char* path) override;
    bool rename(const char* existing, const char* newPath) override;
    bool changeMode(const char* path, uint16_t mode) override;
    bool sync() override;
    int readDirectory(const char* path, uint32_t index,
                      vfs::DirectoryEntry& entry) override;
    bool readOnly() const override;

   private:
    struct Node {
        uint32_t firstCluster;
        uint64_t size;
        bool directory;
        uint32_t entrySector;
        uint16_t entryOffset;
        uint32_t entryIndex;
        uint8_t longEntries;
    };
    struct Entry {
        char name[256];
        char shortName[11];
        bool directory;
        bool valid;
        bool end;
    };

    bool findEntry(uint32_t directoryCluster, const char* name, Node& status);
    bool readEntry(uint32_t directoryCluster, uint32_t index, Entry& entry,
                   Node* status);
    bool readSector(uint32_t sector, uint8_t* buffer);
    bool writeSector(uint32_t sector, const uint8_t* buffer);
    bool nextCluster(uint32_t cluster, uint32_t& next);
    bool resolve(const char* path, Node& status);
    bool resolveParent(const char* path, Node& parent, char* name,
                       size_t capacity);
    bool findFreeEntry(uint32_t directoryCluster, uint32_t& sector,
                       uint16_t& offset);
    bool findFreeEntries(uint32_t directoryCluster, uint32_t count,
                         uint32_t& index);
    bool releaseChain(uint32_t first);
    bool directoryLocation(uint32_t directoryCluster, uint32_t index,
                           uint32_t& sector, uint16_t& offset);
    bool allocateCluster(uint32_t& cluster);
    bool setFatEntry(uint32_t cluster, uint32_t value);
    bool adjustFreeClusterCount(int32_t delta);
    bool clusterAt(uint32_t first, uint32_t index, uint32_t& cluster);
    bool updateFileEntry(uint64_t cookie, uint32_t firstCluster, uint32_t size);
    static bool encodeShortName(const char* name, char output[11]);

    vfs::SectorDevice* device_ = nullptr;
    uint32_t reservedSectors_  = 0;
    uint32_t fatSectors_       = 0;
    uint32_t firstDataSector_  = 0;
    uint32_t totalSectors_     = 0;
    uint32_t rootCluster_      = 0;
    uint32_t clusterCount_     = 0;
    uint16_t fsInfoSector_     = 0xffff;
    uint16_t backupBootSector_ = 0xffff;
    uint8_t fatCount_          = 0;
    uint8_t sectorsPerCluster_ = 0;
    bool readOnly_             = true;
};

namespace vfs::fat {
class Probe {
   public:
    bool detect(const uint8_t* data, size_t size);
    uint64_t blockOffset(uint32_t cluster, uint32_t sectorsPerCluster,
                         uint32_t firstDataSector, uint32_t bytesPerSector);
};
}  // namespace vfs::fat
