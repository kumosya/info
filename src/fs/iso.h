#pragma once

#include <stddef.h>
#include <stdint.h>

#include "block.h"
#include "vfs.h"

class Iso9660FileSystem final : public vfs::FileSystem {
   public:
    bool mount(vfs::SectorDevice& device) override;
    bool lookup(const char* path, vfs::FileStatus& status) override;
    int64_t read(const vfs::FileStatus& status, uint64_t offset, void* buffer,
                 size_t size) override;
    int readDirectory(const char* path, uint32_t index,
                      vfs::DirectoryEntry& entry) override;
    bool readOnly() const override { return true; }

   private:
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
