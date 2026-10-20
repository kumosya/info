#pragma once

#include <mutex>

#include "block.h"
#include "devices.h"
#include "procfs.h"
#include "ext3.h"
#include "ext4.h"
#include "fat32.h"
#include "fstab.h"
#include "iso.h"
#include "os/abi.h"
#include "os/file.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/storage.h"
#include "vfs.h"
#include "sysfs.h"

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
    return prefix[index] == '\0' && (path[index] == '\0' || path[index] == '/');
}

static bool containsText(const char* text, const char* token) {
    if (text == nullptr || token == nullptr || token[0] == '\0') return false;
    for (uint32_t start = 0; text[start] != '\0'; ++start) {
        uint32_t index = 0;
        while (token[index] != '\0' && text[start + index] == token[index]) {
            ++index;
        }
        if (token[index] == '\0') return true;
    }
    return false;
}
};

class BlockDeviceClient final : public vfs::SectorDevice {
   public:
    bool init(uint64_t endpoint, uint32_t deviceId) {
        endpoint_ = endpoint;
        deviceId_ = deviceId;
        request_ = {};
        os::IpcMessage& request = request_;
        request.type    = static_cast<uint64_t>(os::StorageRequest::GetInfo);
        request.data[0] = deviceId_;
        request.length  = 1;
        reply_ = {};
        os::IpcMessage& reply = reply_;
        if (endpoint_ == 0 ||
            os::callService(endpoint_, request, reply) != 0 ||
            static_cast<int64_t>(reply.data[0]) != 0 || reply.data[1] == 0 ||
            reply.data[1] > UINT64_MAX / sectorSize ||
            reply.data[2] != sectorSize ||
            reply.length <
                os::storageIdentityWord + (sizeof(identity_) + 7) / 8) {
            return false;
        }
        os::Runtime::copyBytes(&identity_, &reply.data[os::storageIdentityWord],
                      sizeof(identity_));
        sectorCount_ = reply.data[1];
        readOnly_    = reply.data[5] != 0;
        return true;
    }

    bool readSector(uint64_t sector, void* buffer) override {
        return readSectors(sector, 1, buffer);
    }

    bool writeSector(uint64_t sector, const void* buffer) override {
        return writeSectors(sector, 1, buffer);
    }

    bool readSectors(uint64_t firstSector, uint32_t count,
                     void* buffer) override {
        return transferSectors(firstSector, count, buffer, nullptr, false);
    }

    bool writeSectors(uint64_t firstSector, uint32_t count,
                      const void* buffer) override {
        return !readOnly_ &&
               transferSectors(firstSector, count, nullptr, buffer, true);
    }

    bool flush() override {
        std::lock_guard<std::mutex> guard(transferLock_);
        request_ = {};
        os::IpcMessage& request = request_;
        request.type    = static_cast<uint64_t>(os::StorageRequest::Flush);
        request.data[0] = deviceId_;
        request.length  = 1;
        reply_ = {};
        os::IpcMessage& reply = reply_;
        return os::callService(endpoint_, request, reply) == 0 &&
               static_cast<int64_t>(reply.data[0]) == 0;
    }

    const os::StorageDeviceIdentity& identity() const { return identity_; }
    uint64_t sectorCount() const override { return sectorCount_; }
    bool readOnly() const override { return readOnly_; }

    bool probe() {
        uint8_t sector[sectorSize]{};
        return sectorCount_ != 0 && readSector(0, sector);
    }

   private:
    bool transferSectors(uint64_t firstSector, uint32_t count, void* readBuffer,
                         const void* writeBuffer, bool write) {
        std::lock_guard<std::mutex> guard(transferLock_);
        if (count == 0 || firstSector >= sectorCount_ ||
            count > sectorCount_ - firstSector ||
            (write ? writeBuffer == nullptr : readBuffer == nullptr)) {
            return false;
        }
        uint32_t transferred = 0;
        while (transferred < count) {
            const uint32_t batch = count - transferred < os::maximumStorageTransferBlocks
                ? count - transferred : os::maximumStorageTransferBlocks;
            const uint32_t bytes = batch * sectorSize;
            request_ = {};
            reply_ = {};
            os::IpcMessage& request = request_;
            os::IpcMessage& reply = reply_;
            request.type = static_cast<uint64_t>(write ? os::StorageRequest::WriteRange
                                                       : os::StorageRequest::ReadRange);
            request.data[0] = os::StorageProtocol::encodeStorageBlock(deviceId_, firstSector + transferred);
            request.data[1] = batch;
            request.length = 2;
            if (write) {
                os::Runtime::copyBytes(&request.data[2],
                    static_cast<const uint8_t*>(writeBuffer) + transferred * sectorSize, bytes);
                request.length += bytes / sizeof(uint64_t);
            }
            if (os::callService(endpoint_, request, reply) != 0 ||
                reply.length < 2 || reply.data[0] != 0 || reply.data[1] != bytes ||
                (!write && reply.length < 2 + bytes / sizeof(uint64_t))) return false;
            if (!write) os::Runtime::copyBytes(static_cast<uint8_t*>(readBuffer) + transferred * sectorSize,
                                     &reply.data[2], bytes);
            transferred += batch;
        }
        return true;
    }

    static constexpr uint32_t sectorSize = 512;
    std::mutex transferLock_;
    // All I/O is serialized; keep IPC payloads off the limited worker stack.
    os::IpcMessage request_{};
    os::IpcMessage reply_{};
    os::StorageDeviceIdentity identity_{};
    uint64_t endpoint_           = 0;
    uint64_t sectorCount_        = 0;
    bool readOnly_               = false;
    uint32_t deviceId_           = 0;
};

enum class DescriptorBackend : uint8_t {
    None,
    Filesystem,
    Zero,
    Null,
    BlockDevice,
    SystemFile,
    ProcessFile
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
    int flags = O_RDONLY;
    uint64_t mappingIssuer = 0;
    uint64_t mappingToken = 0;
};

class FsServer {
   public:
    bool init(uint64_t storageEndpoint, uint64_t deviceEndpoint);

    bool handle(const os::IpcMessage& request, os::IpcMessage& reply);

   private:
    using FileSystemPtr = vfs::FileSystem*;
    bool permitted(const os::IpcMessage& request);
    bool pathAccess(const char* path, const os::TaskInfo& caller, unsigned access, bool parent = false);

    void handleAsync(const os::IpcMessage& request, bool writeOperation);

    uint64_t open(uint64_t ownerPid, const char* path, int flags);
    void reapDescriptors();

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
    void descriptorStat(uint64_t ownerPid, uint64_t descriptor, os::IpcMessage& reply);
    void transferAt(const os::IpcMessage& request, os::IpcMessage& reply, bool writing);

    void stat(const char* path, os::IpcMessage& reply, uint64_t caller = 0);
    int64_t readDevice(uint32_t index, uint64_t offset, void* buffer,
                       size_t size);

    uint64_t truncate(const char* path);

    void readDirectory(const char* path, uint64_t index, os::IpcMessage& reply, uint64_t caller = 0);

    void readDirectoryBatch(const char* path, uint64_t index,
                            uint64_t requested, os::IpcMessage& reply, uint64_t caller = 0);

    bool mount(const os::IpcMessage& request);

    bool unmount(const os::IpcMessage& request);

    void listMount(uint32_t index, os::IpcMessage& reply) const;

    bool format(const os::IpcMessage& request) const;

    FileDescriptor* getDescriptorRecord(uint64_t ownerPid, uint64_t descriptor);

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

    bool create(const char* path, bool directory);

    bool remove(const char* path);

    bool link(const char* existing, const char* newPath);
    bool rename(const char* existing, const char* newPath);

    bool changeMode(const char* path, uint16_t mode);

    bool changeOwner(const char* path, uint16_t uid, uint16_t gid);

    bool changeTimes(const char* path, uint32_t accessTime,
                     uint32_t modificationTime);

    static void setStat(os::IpcMessage& reply, os::FileType type, uint64_t size,
                        uint32_t mode);

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
