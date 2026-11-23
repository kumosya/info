/*
 * 对应文件系统的格式识别和实现。
 */

#ifndef _FS_SYSFS_H
#define _FS_SYSFS_H

#include "service/fs/vfs.h"

namespace vfs {
    /* 生成系统信息文件系统中的节点及其查询结果。 */
    class SystemFiles {
        public:
            static bool init(FileSystem& root);
            static bool lookup(const char* path, FileStatus& status);
            static int directory(const char* path, uint32_t index,
                                 DirectoryEntry& entry);
            static int64_t read(uint32_t inode, uint64_t offset, void* buffer,
                                size_t size);
    };
}  // namespace vfs
#endif
