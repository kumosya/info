/*
 * 文件服务请求与描述符管理。
 */

#pragma once
#include <os/detail/client.h>
#include <os/device.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/storage.h>
#include <os/task.h>

#include <mutex>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/fat/filesystem.h"
#include "service/fs/iso/filesystem.h"
#include "service/fs/mount.h"
#include "service/fs/procfs/filesystem.h"
#include "service/fs/sysfs/filesystem.h"
#include "service/fs/vfs.h"

namespace fs::server {

    constexpr uint32_t descriptorCapacity = 64;
    constexpr uint32_t mountCapacity = 16;
    constexpr uint64_t workerStackAddress = 0x0000000050000000ULL;
    constexpr uint64_t workerStackSize = 64 * 1024;
    constexpr uint32_t workerThreadCount = 3;

    /* 在固定容量内保存路径并处理规范化和拼接。 */
    namespace path_text {

        bool collapseSlashes(char* path, size_t capacity);
        bool startsWithPath(const char* path, const char* prefix);

        bool containsText(const char* text, const char* token);
    }  // namespace path_text

    class BlockDeviceClient final : public fs::SectorDevice {
        public:
            bool init(uint64_t endpoint, uint32_t id);

            bool readSector(uint64_t block, void* buffer) override {
                uint32_t done = 0;
                return usable() &&
                       device_.read(block, 1, buffer, 512, done) == 0;
            }

            bool writeSector(uint64_t block, const void* buffer) override {
                uint32_t done = 0;
                return usable() &&
                       device_.write(block, 1, buffer, 512, done) == 0;
            }

            bool readSectors(uint64_t block, uint32_t count,
                             void* buffer) override;
            bool writeSectors(uint64_t block, uint32_t count,
                              const void* buffer) override;

            bool flush() override {
                return usable() && device_.flush();
            }

            uint64_t sectorCount() const override {
                return usable() ? device_.getBlockCount() : 0;
            }

            bool usable() const {
                return device_.getBlockSize() == 512 &&
                       device_.getMaxTransferBlocks();
            }

            bool readOnly() const override {
                return device_.getReadOnly();
            }

            bool probe();

            const services::wire::StorageDeviceIdentity& identity() const {
                return identity_;
            }

        private:
            os::Storage::Device device_;
            services::wire::StorageDeviceIdentity identity_{};
    };

    enum class DescriptorBackend : uint8_t {
        None,
        Filesystem,
        Zero,
        Null,
        BlockDevice,
        SystemFile,
        ProcessFile,
        DeviceDirectory
    };
    enum class PathBackend : uint8_t { Root = 0, Invalid = 0xff };

    /* 登记一个挂载点与其文件系统实例。 */
    class MountEntry {
        public:
            MountEntry() = default;
            ~MountEntry() = default;

            const MountSpec& getSpecification() const {
                return spec;
            }

            uint32_t getDeviceIndex() const {
                return deviceIndex;
            }

        private:
            friend class FsServer;
            MountSpec spec{};
            uint32_t deviceIndex = services::maximumStorageDevices;
            bool configured = false;
    };

    /* 保存可由多个描述符共享的打开文件位置、访问方式和后端对象。 */
    class OpenFileDescription {
        public:
            OpenFileDescription() = default;
            ~OpenFileDescription() = default;

            const char* getPath() const {
                return path;
            }

            uint64_t getOffset() const {
                return offset;
            }

            uint64_t getSize() const {
                return size;
            }

            uint32_t getReferenceCount() const {
                return referenceCount;
            }

        private:
            friend class FsServer;
            char snapshot[1536]{};
            uint32_t inode;
            uint16_t mode;
            uint16_t uid, gid;
            uint16_t links;
            uint32_t accessTime, modificationTime, changeTime;
            char path[384];
            uint64_t offset;
            uint64_t size;
            uint64_t privateData;
            uint32_t referenceCount;
            fs::FileSystem* filesystem;
            DescriptorBackend backend;
            PathBackend mountIndex;
    };

    /* 记录进程可见描述符对打开文件对象的引用及标志。 */
    class FileDescriptor {
        public:
            FileDescriptor() = default;
            FileDescriptor(uint64_t owner, OpenFileDescription* description,
                           int flags);
            ~FileDescriptor() = default;

            uint64_t getOwner() const {
                return ownerPid;
            }

            int getFlags() const {
                return flags;
            }

        private:
            friend class FsServer;
            uint64_t ownerPid;
            OpenFileDescription* description;
            int flags = O_RDONLY;
            uint64_t mappingIssuer = 0;
            uint64_t mappingToken = 0;
            bool mappingClaimed = false;
    };

    /* 分发文件服务请求，管理挂载、打开文件以及进程描述符关系。 */
    class FsServer {
        public:
            bool init(uint64_t storageEndpoint, uint64_t deviceEndpoint);

            bool handle(const os::IpcMessage& request, os::IpcMessage& reply);
            void collectClosedDescriptions();
            void reapDescriptors();

        private:
            using FileSystemPtr = fs::FileSystem*;
            bool permitted(const os::IpcMessage& request);
            bool removalAccess(const char* path,
                               const services::wire::TaskInfo& caller);
            bool pathAccess(const char* path,
                            const services::wire::TaskInfo& caller,
                            unsigned access, bool parent = false);

            void handleAsync(const os::IpcMessage& request,
                             bool writeOperation);

            uint64_t open(uint64_t ownerPid, const char* path, int flags);
            uint64_t openProcessObject(uint64_t caller, uint64_t object,
                                       int flags, const char* path);
            int processObjectDirectory(const char* path, uint64_t index,
                                       uint64_t caller,
                                       fs::DirectoryEntry& entry);

            uint64_t close(uint64_t ownerPid, uint64_t descriptor);

            void cloneDescriptors(uint64_t parentPid, uint64_t childPid,
                                  os::IpcMessage& reply);
            void delegateMapping(const os::IpcMessage& request,
                                 os::IpcMessage& reply);
            void claimMapping(const os::IpcMessage& request,
                              os::IpcMessage& reply);
            uint64_t nextMappingToken_ = 1;

            void read(uint64_t ownerPid, uint64_t descriptor,
                      uint64_t requested, os::IpcMessage& reply);

            void write(uint64_t ownerPid, const os::IpcMessage& request,
                       os::IpcMessage& reply);

            void seek(uint64_t ownerPid, uint64_t descriptor, int64_t offset,
                      uint64_t whence, os::IpcMessage& reply);
            void descriptorStat(uint64_t ownerPid, uint64_t descriptor,
                                os::IpcMessage& reply);
            void transferAt(const os::IpcMessage& request,
                            os::IpcMessage& reply, bool writing);
            void readBuffer(const os::IpcMessage& request,
                            os::IpcMessage& reply);

            void stat(const char* path, os::IpcMessage& reply,
                      uint64_t caller = 0);
            int64_t readDevice(uint32_t index, uint64_t offset, void* buffer,
                               size_t size);

            uint64_t truncate(const char* path);
            int syncAll();

            void readOpenedDirectory(uint64_t owner, uint64_t descriptor,
                                     uint64_t cursor, uint64_t capacity,
                                     os::IpcMessage& reply);
            void readDirectory(const char* path, uint64_t index,
                               os::IpcMessage& reply, uint64_t caller = 0);

            void readDirectoryBatch(const char* path, uint64_t index,
                                    uint64_t requested, os::IpcMessage& reply,
                                    uint64_t caller = 0,
                                    bool filesystemLocked = false);

            bool mount(const os::IpcMessage& request);

            bool unmount(const os::IpcMessage& request);

            void listMount(uint32_t index, os::IpcMessage& reply) const;

            bool format(const os::IpcMessage& request) const;

            FileDescriptor* getDescriptorRecord(uint64_t ownerPid,
                                                uint64_t descriptor);
            void releaseMapping(const os::IpcMessage& request,
                                os::IpcMessage& reply);
            void listMappings(const os::IpcMessage& request,
                              os::IpcMessage& reply);
            void cancelMapping(const os::IpcMessage& request,
                               os::IpcMessage& reply);

            OpenFileDescription* getDescriptor(uint64_t ownerPid,
                                               uint64_t descriptor);

            OpenFileDescription* acquireDescription(uint64_t ownerPid,
                                                    uint64_t descriptor);

            void releaseDescription(OpenFileDescription* description);

            std::mutex& descriptionMutex(
                const OpenFileDescription* description);

            PathBackend pathBackend(const char* path) const;

            const char* relativePath(const char* path,
                                     PathBackend backend) const;

            bool loadMountTable();
            bool attachMount(uint32_t index);
            int deviceDirectory(const char* path, uint32_t index,
                                fs::DirectoryEntry& entry) const;

            const MountEntry* mountEntry(PathBackend backend) const;

            fs::FileSystem* fileSystem(PathBackend backend) const;

            std::mutex& fileSystemLock(PathBackend backend);

            BlockDeviceClient* blockDevice(PathBackend backend);

            bool readOnly(PathBackend backend) const;

            bool create(const char* path, bool directory, uint16_t mode = 0755,
                        uint32_t uid = UINT32_MAX, uint32_t gid = UINT32_MAX);

            bool remove(const char* path, bool directoryOnly = false,
                        bool fileOnly = false);

            bool link(const char* existing, const char* newPath);
            bool rename(const char* existing, const char* newPath,
                        bool directoryOnly = false);

            bool changeMode(const char* path, uint16_t mode);

            bool changeOwner(const char* path, uint16_t uid, uint16_t gid);

            bool changeTimes(const char* path, uint32_t accessTime,
                             uint32_t modificationTime);

            static void setStat(os::IpcMessage& reply, os::File::Type type,
                                uint64_t size, uint32_t mode);

            BlockDeviceClient devices_[services::maximumStorageDevices];
            DeviceCatalog catalog_;
            fs::sysfs::SystemFiles systemFiles_;
            fs::FileSystem* fileSystems_[mountCapacity]{};
            FileDescriptor descriptors_[descriptorCapacity]{};
            OpenFileDescription openDescriptions_[descriptorCapacity]{};
            uint64_t deviceEndpoint_ = 0;
            char fstabText_[4096]{};
            bool mounted_[mountCapacity]{};
            MountEntry mountTable_[mountCapacity]{};
            std::mutex descriptorLock_;
            std::mutex descriptionLocks_[descriptorCapacity];
            std::mutex fileSystemLocks_[mountCapacity];
            std::mutex mountOperationLock_;
            mutable std::mutex mountStateLock_;
    };

}  // namespace fs::server
