#include "service/common/queries.h"
#include "service/common/records.h"
#pragma once
#include <mutex>

#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/block.h"
#include "service/fs/devices.h"
#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/fat/filesystem.h"
#include "service/fs/fstab.h"
#include "service/fs/iso/filesystem.h"
#include "service/fs/procfs/filesystem.h"
#include "service/fs/sysfs/filesystem.h"
#include "service/fs/vfs.h"
#include <os/device.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/storage.h>
#include <os/task.h>

namespace vfs::server {

constexpr uint32_t descriptorCapacity = 64;
constexpr uint32_t mountCapacity      = 16;
constexpr uint64_t workerStackAddress = 0x0000000050000000ULL;
constexpr uint64_t workerStackSize    = 64 * 1024;
constexpr uint32_t workerThreadCount  = 3;

class PathText {
   public:
    static bool collapseSlashes(char* path, size_t capacity) {
        size_t read = 0, write = 0;
        while (read < capacity && path[read]) {
            const char value = path[read++];
            if (value == '/' && write && path[write - 1] == '/') continue;
            path[write++] = value;
        }
        if (read == capacity) return false;
        path[write] = 0;
        return true;
    }
    static bool startsWithPath(const char* path, const char* prefix) {
        uint32_t index = 0;
        while (prefix[index] != '\0' && path[index] == prefix[index]) ++index;
        return prefix[index] == '\0' &&
               (path[index] == '\0' || path[index] == '/');
    }

    static bool containsText(const char* text, const char* token) {
        if (text == nullptr || token == nullptr || token[0] == '\0')
            return false;
        for (uint32_t start = 0; text[start] != '\0'; ++start) {
            uint32_t index = 0;
            while (token[index] != '\0' &&
                   text[start + index] == token[index]) {
                ++index;
            }
            if (token[index] == '\0') return true;
        }
        return false;
    }
};

class BlockDeviceClient final : public vfs::SectorDevice {
   public:
    bool init(uint64_t endpoint, uint32_t id) {
        if (!os::detail::StorageClient().open(device_, id, endpoint))
            return false;
        identity_.id              = device_.getId();
        identity_.major           = device_.getMajor();
        identity_.minor           = device_.getMinor();
        identity_.parentId        = device_.getParentId();
        identity_.partitionNumber = device_.getPartitionNumber();
        identity_.startBlock      = device_.getStartBlock();
        os::Runtime::copyString(identity_.name, device_.getName(),
                                sizeof(identity_.name));
        os::Runtime::copyString(identity_.partitionUuid,
                                device_.getPartitionUuid(),
                                sizeof(identity_.partitionUuid));
        os::Runtime::copyString(identity_.hardwarePath,
                                device_.getHardwarePath(),
                                sizeof(identity_.hardwarePath));
        return true;
    }
    bool readSector(uint64_t block, void* buffer) override {
        uint32_t done = 0;
        return device_.read(block, 1, buffer, 512, done) == 0;
    }
    bool writeSector(uint64_t block, const void* buffer) override {
        uint32_t done = 0;
        return device_.write(block, 1, buffer, 512, done) == 0;
    }
    bool readSectors(uint64_t block, uint32_t count, void* buffer) override {
        uint32_t done = 0;
        return device_.read(block, count, buffer, uint64_t(count) * 512,
                            done) == 0;
    }
    bool writeSectors(uint64_t block, uint32_t count,
                      const void* buffer) override {
        uint32_t done = 0;
        return device_.write(block, count, buffer, uint64_t(count) * 512,
                             done) == 0;
    }
    bool flush() override { return device_.flush(); }
    uint64_t sectorCount() const override { return device_.getBlockCount(); }
    bool readOnly() const override { return device_.getReadOnly(); }
    bool probe() {
        uint8_t block[512]{};
        uint32_t done = 0;
        return device_.getBlockCount() &&
               device_.read(0, 1, block, sizeof(block), done) == 0;
    }
    const os::wire::StorageDeviceIdentity& identity() const {
        return identity_;
    }

   private:
    os::Storage::Device device_;
    os::wire::StorageDeviceIdentity identity_{};
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

struct MountEntry {
    MountSpec spec{};
    uint32_t deviceIndex = os::maximumStorageDevices;
    bool configured      = false;
};

struct OpenFileDescription {
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
    vfs::FileSystem* filesystem;
    DescriptorBackend backend;
    PathBackend mountIndex;
};

struct FileDescriptor {
    uint64_t ownerPid;
    OpenFileDescription* description;
    int flags              = O_RDONLY;
    uint64_t mappingIssuer = 0;
    uint64_t mappingToken  = 0;
    bool mappingClaimed    = false;
};

class FsServer {
   public:
    bool init(uint64_t storageEndpoint, uint64_t deviceEndpoint);

    bool handle(const os::IpcMessage& request, os::IpcMessage& reply);
    void collectClosedDescriptions();
    void reapDescriptors();

   private:
    using FileSystemPtr = vfs::FileSystem*;
    bool permitted(const os::IpcMessage& request);
    bool pathAccess(const char* path, const os::wire::TaskInfo& caller,
                    unsigned access, bool parent = false);

    void handleAsync(const os::IpcMessage& request, bool writeOperation);

    uint64_t open(uint64_t ownerPid, const char* path, int flags);
    uint64_t openProcessObject(uint64_t caller, uint64_t object, int flags,
                               const char* path);
    int processObjectDirectory(const char* path, uint64_t index,
                               uint64_t caller, vfs::DirectoryEntry& entry);

    uint64_t close(uint64_t ownerPid, uint64_t descriptor);

    void cloneDescriptors(uint64_t parentPid, uint64_t childPid,
                          os::IpcMessage& reply);
    void delegateMapping(const os::IpcMessage& request, os::IpcMessage& reply);
    void claimMapping(const os::IpcMessage& request, os::IpcMessage& reply);
    uint64_t nextMappingToken_ = 1;

    void read(uint64_t ownerPid, uint64_t descriptor, uint64_t requested,
              os::IpcMessage& reply);

    void write(uint64_t ownerPid, const os::IpcMessage& request,
               os::IpcMessage& reply);

    void seek(uint64_t ownerPid, uint64_t descriptor, int64_t offset,
              uint64_t whence, os::IpcMessage& reply);
    void descriptorStat(uint64_t ownerPid, uint64_t descriptor,
                        os::IpcMessage& reply);
    void transferAt(const os::IpcMessage& request, os::IpcMessage& reply,
                    bool writing);
    void readBuffer(const os::IpcMessage& request, os::IpcMessage& reply);

    void stat(const char* path, os::IpcMessage& reply, uint64_t caller = 0);
    int64_t readDevice(uint32_t index, uint64_t offset, void* buffer,
                       size_t size);

    uint64_t truncate(const char* path);

    void readOpenedDirectory(uint64_t owner, uint64_t descriptor,
                             uint64_t cursor, uint64_t capacity,
                             os::IpcMessage& reply);
    void readDirectory(const char* path, uint64_t index, os::IpcMessage& reply,
                       uint64_t caller = 0);

    void readDirectoryBatch(const char* path, uint64_t index,
                            uint64_t requested, os::IpcMessage& reply,
                            uint64_t caller = 0, bool filesystemLocked = false);

    bool mount(const os::IpcMessage& request);

    bool unmount(const os::IpcMessage& request);

    void listMount(uint32_t index, os::IpcMessage& reply) const;

    bool format(const os::IpcMessage& request) const;

    FileDescriptor* getDescriptorRecord(uint64_t ownerPid, uint64_t descriptor);
    void releaseMapping(const os::IpcMessage& request, os::IpcMessage& reply);
    void listMappings(const os::IpcMessage& request, os::IpcMessage& reply);
    void cancelMapping(const os::IpcMessage& request, os::IpcMessage& reply);

    OpenFileDescription* getDescriptor(uint64_t ownerPid, uint64_t descriptor);

    OpenFileDescription* acquireDescription(uint64_t ownerPid,
                                            uint64_t descriptor);

    void releaseDescription(OpenFileDescription* description);

    std::mutex& descriptionMutex(const OpenFileDescription* description);

    PathBackend pathBackend(const char* path) const;

    const char* relativePath(const char* path, PathBackend backend) const;

    bool loadMountTable();
    bool attachMount(uint32_t index);
    int deviceDirectory(const char* path, uint32_t index,
                        vfs::DirectoryEntry& entry) const;

    const MountEntry* mountEntry(PathBackend backend) const;

    vfs::FileSystem* fileSystem(PathBackend backend) const;

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

    BlockDeviceClient devices_[os::maximumStorageDevices];
    DeviceCatalog catalog_;
    vfs::FileSystem* fileSystems_[mountCapacity]{};
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

}  // namespace vfs::server
