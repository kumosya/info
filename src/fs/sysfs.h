#ifndef _FS_SYSFS_H
#define _FS_SYSFS_H

#include "vfs.h"

namespace vfs {
class SystemFiles {
   public:
    static bool initialize(FileSystem& root);
    static bool lookup(const char* path, FileStatus& status);
    static int directory(const char* path, uint32_t index, DirectoryEntry& entry);
    static int64_t read(uint32_t inode, uint64_t offset, void* buffer, size_t size);
};
}
#endif
