/*
 * 对应文件系统的格式识别和实现。
 */

#ifndef _FS_SYSFS_H
#define _FS_SYSFS_H

#include "service/fs/vfs.h"

namespace fs::sysfs {

    /* 生成系统信息文件系统中的节点及其查询结果。 */
    class SystemFiles {
        public:
            SystemFiles() = default;
            ~SystemFiles() = default;
            bool init(FileSystem& root);
            bool lookup(const char* path, FileStatus& status);
            int directory(const char* path, uint32_t index,
                          DirectoryEntry& entry);
            int64_t read(uint32_t inode, uint64_t offset, void* buffer,
                         size_t size);

        private:
            char version_[64]{};
            char hostname_[64]{};
    };
}  // namespace fs::sysfs
#endif
