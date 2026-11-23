/*
 * 对应文件系统的格式识别和实现。
 */

#pragma once
#include "service/fs/vfs.h"
namespace vfs {
    /* 生成进程信息文件系统中的目录和文件内容。 */
    class ProcessFiles {
        public:
            static bool lookup(const char* path, uint64_t caller,
                               FileStatus& status);
            static int directory(const char* path, uint64_t cursor,
                                 uint64_t caller, DirectoryEntry& entry);
            static int64_t snapshot(const char* path, uint64_t caller,
                                    char* bytes, size_t capacity);
            static uint64_t nextCursor(const char* path,
                                       const DirectoryEntry& entry,
                                       uint64_t previous);
    };
}  // namespace vfs
