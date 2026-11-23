/* 进程文件视图与进程对象路径。 */

#pragma once
#include <cstdint>

#include "service/fs/vfs.h"

namespace fs::procfs {

    /* 生成进程信息文件系统中的目录和文件内容。 */
    class ProcessFiles {
        private:
            uint64_t caller_;

        public:
            explicit ProcessFiles(uint64_t caller = 0) : caller_(caller) {
            }

            ~ProcessFiles() = default;
            bool lookup(const char* path, FileStatus& status);
            int directory(const char* path, uint64_t cursor,
                          DirectoryEntry& entry);
            int64_t snapshot(const char* path, char* bytes, size_t capacity);
            uint64_t nextCursor(const char* path, const DirectoryEntry& entry,
                                uint64_t previous);
    };

    /* 解析进程对象路径中的任务编号和对象类别。 */
    class ProcessObjectPath {
        private:
            static bool prefix(const char*& cursor, const char* text);
            static bool number(const char*& cursor, uint64_t& value);

        public:
            enum Kind { Other, Directory, File, Denied };

            // Object numbers are FS-native descriptors, not POSIX FD numbers.
            // The authenticated caller can reopen only its own retained regular
            // objects.
            ProcessObjectPath(const char* path, uint64_t caller)
                : path_(path), caller_(caller) {
            }

            ~ProcessObjectPath() = default;
            Kind parse(uint64_t& object);

        private:
            const char* path_;
            uint64_t caller_;
    };
}  // namespace fs::procfs
