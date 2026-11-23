/* 服务对象的成员函数实现。 */
#include "service/fs/vfs.h"

namespace fs {

    /* 通过 inode 查询文件属性。 */
    bool FileSystem::statInode(uint32_t inode, FileStatus& status) {
        (void)inode;
        (void)status;
        return false;
    }

    /* 提供文件系统的默认写入入口。 */
    int64_t FileSystem::write(const FileStatus& status, uint64_t offset,
                              const void* buffer, size_t size) {
        (void)status;
        (void)offset;
        (void)buffer;
        (void)size;
        return -1;
    }

    /* 提供文件和目录的默认创建入口。 */
    bool FileSystem::create(const char* path, bool directory) {
        (void)path;
        (void)directory;
        return false;
    }

    /* 提供文件链接的默认操作入口。 */
    bool FileSystem::link(const char* existing, const char* newPath) {
        (void)existing;
        (void)newPath;
        return false;
    }

    /* 提供文件重命名的默认操作入口。 */
    bool FileSystem::rename(const char* existing, const char* newPath) {
        (void)existing;
        (void)newPath;
        return false;
    }

    /* 提供文件权限修改的默认入口。 */
    bool FileSystem::changeMode(const char* path, uint16_t mode) {
        (void)path;
        (void)mode;
        return false;
    }

    /* 提供所属用户和组修改的默认入口。 */
    bool FileSystem::changeOwner(const char* path, uint16_t uid, uint16_t gid) {
        (void)path;
        (void)uid;
        (void)gid;
        return false;
    }

    /* 提供文件时间修改的默认入口。 */
    bool FileSystem::changeTimes(const char* path, uint32_t accessTime,
                                 uint32_t modificationTime) {
        (void)path;
        (void)accessTime;
        (void)modificationTime;
        return false;
    }

    /* 以单项遍历接口组合批量目录读取。 */
    int FileSystem::readDirectoryBatch(const char* path, uint32_t index,
                                       DirectoryEntry* entries,
                                       uint32_t capacity) {
        if (entries == nullptr || capacity == 0) {
            return -1;
        }

        uint32_t count = 0;
        while (count < capacity) {
            const int result =
                readDirectory(path, index + count, entries[count]);
            if (result != 0) {
                return result < 0 ? -1 : static_cast<int>(count);
            }

            ++count;
        }

        return static_cast<int>(count);
    }

    /* 增加打开文件对应 inode 的引用。 */
    bool FileSystem::retainInode(uint32_t inode) {
        for (OpenFile& entry : openFiles_) {
            if (entry.inode == inode && entry.references) {
                if (entry.references == UINT32_MAX) {
                    return false;
                }

                ++entry.references;
                return true;
            }
        }

        for (OpenFile& entry : openFiles_) {
            if (!entry.references) {
                entry = {inode, 1, false, false};
                return true;
            }
        }

        return false;
    }

    /* 减少 inode 引用并回收已删除文件。 */
    bool FileSystem::releaseInode(uint32_t inode) {
        for (OpenFile& entry : openFiles_) {
            if (entry.inode == inode && entry.references) {
                if (entry.references == 1 && entry.unlinked) {
                    if (entry.failed) {
                        return false;
                    }

                    if (!reclaimUnlinked(inode)) {
                        entry.failed = true;
                        return false;
                    }
                }

                if (!--entry.references) {
                    entry = {};
                }

                return true;
            }
        }

        return false;
    }

    /* 登记已删除但仍被打开的文件。 */
    bool FileSystem::preserveUnlinked(uint32_t inode) {
        for (OpenFile& entry : openFiles_) {
            if (entry.inode == inode && entry.references) {
                entry.unlinked = true;
                return true;
            }
        }

        return false;
    }

    /* 查询 inode 是否仍有打开引用。 */
    bool FileSystem::inodeRetained(uint32_t inode) const {
        for (const OpenFile& entry : openFiles_) {
            if (entry.inode == inode && entry.references) {
                return true;
            }
        }

        return false;
    }

}  // namespace fs
