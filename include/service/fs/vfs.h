#pragma once

#include <cstddef>
#include <cstdint>

namespace vfs {

class SectorDevice;

inline constexpr uint16_t modeDirectory = 0x4000;
inline constexpr uint16_t modeRegular   = 0x8000;

struct FileStatus {
    uint32_t inode            = 0;
    uint16_t mode             = 0;
    uint64_t size             = 0;
    uint16_t uid              = 0;
    uint16_t gid              = 0;
    uint16_t links            = 0;
    uint32_t accessTime       = 0;
    uint32_t modificationTime = 0;
    uint32_t changeTime       = 0;
    uint64_t privateData      = 0;
};

struct DirectoryEntry {
    uint32_t inode = 0;
    uint8_t type   = 0;
    char name[256]{};
};

class FileSystem {
   public:
    // Called under the filesystem lock, once per open file description.
    virtual bool retainOpenFile(uint32_t inode) {
        (void)inode;
        return true;
    }
    virtual bool releaseOpenFile(uint32_t inode) {
        (void)inode;
        return true;
    }
    virtual bool statInode(uint32_t inode, FileStatus& status) {
        (void)inode;
        (void)status;
        return false;
    }
    virtual bool mount(SectorDevice& device)                  = 0;
    virtual bool lookup(const char* path, FileStatus& status) = 0;
    virtual int64_t read(const FileStatus& status, uint64_t offset,
                         void* buffer, size_t size)           = 0;
    virtual int64_t write(const FileStatus& status, uint64_t offset,
                          const void* buffer, size_t size) {
        (void)status;
        (void)offset;
        (void)buffer;
        (void)size;
        return -1;
    }
    virtual bool truncate(uint32_t inode) {
        (void)inode;
        return false;
    }
    virtual bool truncate(const FileStatus& status) {
        return truncate(status.inode);
    }
    virtual bool create(const char* path, bool directory) {
        (void)path;
        (void)directory;
        return false;
    }
    virtual bool remove(const char* path) {
        (void)path;
        return false;
    }
    virtual bool link(const char* existing, const char* newPath) {
        (void)existing;
        (void)newPath;
        return false;
    }
    virtual bool rename(const char* existing, const char* newPath) {
        (void)existing;
        (void)newPath;
        return false;
    }
    virtual bool changeMode(const char* path, uint16_t mode) {
        (void)path;
        (void)mode;
        return false;
    }
    virtual bool changeOwner(const char* path, uint16_t uid, uint16_t gid) {
        (void)path;
        (void)uid;
        (void)gid;
        return false;
    }
    virtual bool changeTimes(const char* path, uint32_t accessTime,
                             uint32_t modificationTime) {
        (void)path;
        (void)accessTime;
        (void)modificationTime;
        return false;
    }
    virtual int readDirectory(const char* path, uint32_t index,
                              DirectoryEntry& entry) = 0;
    virtual int readDirectoryBatch(const char* path, uint32_t index,
                                   DirectoryEntry* entries, uint32_t capacity) {
        if (entries == nullptr || capacity == 0) return -1;
        uint32_t count = 0;
        while (count < capacity) {
            const int result =
                readDirectory(path, index + count, entries[count]);
            if (result != 0) return result < 0 ? -1 : static_cast<int>(count);
            ++count;
        }
        return static_cast<int>(count);
    }
    virtual bool sync() { return true; }
    virtual bool readOnly() const = 0;

   protected:
    // Only backends with stable inode identities opt into deferred reclamation.
    bool retainInode(uint32_t inode) {
        for (auto& entry : openFiles_)
            if (entry.inode == inode && entry.references) {
                if (entry.references == UINT32_MAX) return false;
                ++entry.references;
                return true;
            }
        for (auto& entry : openFiles_)
            if (!entry.references) {
                entry = {inode, 1, false, false};
                return true;
            }
        return false;
    }
    bool releaseInode(uint32_t inode) {
        for (auto& entry : openFiles_)
            if (entry.inode == inode && entry.references) {
                if (entry.references == 1 && entry.unlinked) {
                    if (entry.failed) return false;
                    if (!reclaimUnlinked(inode)) {
                        entry.failed = true;
                        return false;
                    }
                }
                if (!--entry.references) entry = {};
                return true;
            }
        return false;
    }
    bool preserveUnlinked(uint32_t inode) {
        for (auto& entry : openFiles_)
            if (entry.inode == inode && entry.references) {
                entry.unlinked = true;
                return true;
            }
        return false;
    }
    bool inodeRetained(uint32_t inode) const {
        for (const auto& entry : openFiles_)
            if (entry.inode == inode && entry.references) return true;
        return false;
    }
    virtual bool reclaimUnlinked(uint32_t inode) {
        (void)inode;
        return false;
    }
    ~FileSystem() = default;

   private:
    struct OpenFile {
        uint32_t inode, references;
        bool unlinked, failed;
    } openFiles_[64]{};
};

class FileSystemFactory {
   public:
    static FileSystem* createFileSystem(const char* type);
    static void destroyFileSystem(FileSystem* filesystem, const char* type);
};

}  // namespace vfs
