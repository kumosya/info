/* 文件系统对象、文件属性和扇区设备接口。 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace fs {

    class SectorDevice;

    inline constexpr uint16_t modeDirectory = 0x4000;
    inline constexpr uint16_t modeRegular = 0x8000;

    /* 汇总文件类型、大小、权限和时间等查询结果。 */
    struct FileStatus {
            uint32_t inode = 0;
            uint16_t mode = 0;
            uint64_t size = 0;
            uint16_t uid = 0;
            uint16_t gid = 0;
            uint16_t links = 0;
            uint32_t accessTime = 0;
            uint32_t modificationTime = 0;
            uint32_t changeTime = 0;
            uint64_t privateData = 0;
    };

    /* 保存目录遍历返回的名称、类型和对象标识。 */
    struct DirectoryEntry {
            uint32_t inode = 0;
            uint8_t type = 0;
            char name[256]{};
    };

    /* 定义文件系统需要提供的查询和文件操作接口。 */
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

            virtual bool statInode(uint32_t inode, FileStatus& status);
            virtual bool mount(SectorDevice& device) = 0;
            virtual bool lookup(const char* path, FileStatus& status) = 0;
            virtual int64_t read(const FileStatus& status, uint64_t offset,
                                 void* buffer, size_t size) = 0;
            virtual int64_t write(const FileStatus& status, uint64_t offset,
                                  const void* buffer, size_t size);

            virtual bool truncate(uint32_t inode) {
                (void)inode;
                return false;
            }

            virtual bool truncate(const FileStatus& status) {
                return truncate(status.inode);
            }

            virtual bool create(const char* path, bool directory);

            virtual bool remove(const char* path) {
                (void)path;
                return false;
            }

            virtual bool link(const char* existing, const char* newPath);
            virtual bool rename(const char* existing, const char* newPath);
            virtual bool changeMode(const char* path, uint16_t mode);
            virtual bool changeOwner(const char* path, uint16_t uid,
                                     uint16_t gid);
            virtual bool changeTimes(const char* path, uint32_t accessTime,
                                     uint32_t modificationTime);
            virtual int readDirectory(const char* path, uint32_t index,
                                      DirectoryEntry& entry) = 0;
            virtual int readDirectoryBatch(const char* path, uint32_t index,
                                           DirectoryEntry* entries,
                                           uint32_t capacity);

            virtual bool sync() {
                return true;
            }

            virtual bool readOnly() const = 0;

        protected:
            // Only backends with stable inode identities opt into deferred
            // reclamation.
            bool retainInode(uint32_t inode);
            bool releaseInode(uint32_t inode);
            bool preserveUnlinked(uint32_t inode);
            bool inodeRetained(uint32_t inode) const;

            virtual bool reclaimUnlinked(uint32_t inode) {
                (void)inode;
                return false;
            }

        protected:
            // 对象由 FileSystemFactory 按具体类型析构，禁止通过基类指针直接
            // delete。
            ~FileSystem() = default;

        private:
            /* 保存文件系统内部的一次打开状态。 */
            struct OpenFile {
                    uint32_t inode, references;
                    bool unlinked, failed;
            } openFiles_[64]{};
    };

    /* 按探测到的格式创建对应文件系统实例。 */
    class FileSystemFactory {
        public:
            explicit FileSystemFactory(const char* type) : type_(type) {
            }

            ~FileSystemFactory() = default;
            FileSystem* createFileSystem();
            void destroyFileSystem(FileSystem* filesystem);

        private:
            const char* type_;
    };

    /* 以扇区为单位访问块设备，为文件系统提供统一读写入口。 */
    class SectorDevice {
        public:
            virtual bool readSector(uint64_t sector, void* buffer) = 0;
            virtual bool writeSector(uint64_t sector, const void* buffer) = 0;
            virtual bool readSectors(uint64_t firstSector, uint32_t count,
                                     void* buffer);
            virtual bool writeSectors(uint64_t firstSector, uint32_t count,
                                      const void* buffer);
            virtual bool flush() = 0;
            virtual uint64_t sectorCount() const = 0;
            virtual bool readOnly() const = 0;
    };

}  // namespace fs
