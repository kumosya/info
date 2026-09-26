#pragma once

#include <stddef.h>
#include <stdint.h>

#include "block.h"

namespace fs::iso9660 {

struct FileStatus {
    uint32_t extent;
    uint32_t size;
    bool directory;
};

struct DirectoryEntry {
    bool directory;
    char name[48];
};

class Volume {
   public:
    bool mount(SectorDevice& device);
    bool lookup(const char* path, FileStatus& status);
    int64_t read(const FileStatus& status, uint64_t offset, void* buffer,
                 size_t size);
    int readDirectory(const char* path, uint32_t index, DirectoryEntry& entry);

   private:
    bool readBlock(uint32_t block, void* buffer);
    bool findInDirectory(const FileStatus& directory, const char* name,
                         size_t nameLength, FileStatus& status);
    bool readRecord(const FileStatus& directory, uint32_t offset,
                    uint8_t* block, const uint8_t*& record,
                    uint32_t& nextOffset);

    SectorDevice* device_ = nullptr;
    FileStatus root_{};
};

}  // namespace fs::iso9660
