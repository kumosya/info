#include <mutex>

#include "block.h"
#include "ext3.h"
#include "iso.h"
#include "os/abi.h"
#include "os/file.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/storage.h"

namespace {

constexpr uint32_t descriptorCapacity = 64;
constexpr uint64_t workerStackAddress = 0x0000000050000000ULL;
constexpr uint64_t workerStackSize    = 64 * 1024;
constexpr uint32_t workerThreadCount  = 3;

bool startsWithPath(const char* path, const char* prefix) {
    uint32_t index = 0;
    while (prefix[index] != '\0' && path[index] == prefix[index]) ++index;
    return prefix[index] == '\0' &&
           (path[index] == '\0' || path[index] == '/');
}

bool containsText(const char* text, const char* token) {
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

class BlockDeviceClient final : public fs::SectorDevice {
   public:
    bool initialize(uint64_t endpoint, uint32_t deviceId) {
        endpoint_           = endpoint;
        deviceId_           = deviceId;
        completionEndpoint_ = os::createEndpoint();
        os::IpcMessage request{};
        request.type = static_cast<uint64_t>(os::StorageRequest::GetInfo);
        request.data[0] = deviceId_;
        request.length = 1;
        os::IpcMessage reply{};
        if (endpoint_ == 0 || completionEndpoint_ == 0 ||
            os::callService(endpoint_, request, reply) != 0 ||
            static_cast<int64_t>(reply.data[0]) != 0 || reply.data[1] == 0 ||
            reply.data[2] != sectorSize) {
            return false;
        }
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
        os::IpcMessage request{};
        request.type = static_cast<uint64_t>(os::StorageRequest::Flush);
        request.data[0] = deviceId_;
        request.length = 1;
        os::IpcMessage reply{};
        return os::callService(endpoint_, request, reply) == 0 &&
               static_cast<int64_t>(reply.data[0]) == 0;
    }

    uint64_t sectorCount() const override { return sectorCount_; }
    bool readOnly() const override { return readOnly_; }

    bool probe() {
        uint8_t sector[sectorSize]{};
        return sectorCount_ != 0 && readSector(0, sector);
    }

   private:
    bool transferSectors(uint64_t firstSector, uint32_t count, void* readBuffer,
                         const void* writeBuffer, bool write) {
        if (count == 0 || firstSector >= sectorCount_ ||
            count > sectorCount_ - firstSector ||
            (write ? writeBuffer == nullptr : readBuffer == nullptr)) {
            return false;
        }
        constexpr uint32_t maximumBatch = 8;
        uint32_t transferred            = 0;
        while (transferred < count) {
            const uint32_t batch = count - transferred < maximumBatch
                                       ? count - transferred
                                       : maximumBatch;
            uint32_t submitted   = 0;
            for (; submitted < batch; ++submitted) {
                os::IpcMessage request{};
                request.type = static_cast<uint64_t>(
                    write ? os::StorageRequest::WriteAsync
                          : os::StorageRequest::ReadAsync);
                request.data[0]     = os::encodeStorageBlock(
                    deviceId_, firstSector + transferred + submitted);
                request.data[1]     = submitted;
                request.handleCount = 1;
                request.handles[0]  = completionEndpoint_;
                request.length      = 2;
                if (write) {
                    request.data[2] = sectorSize;
                    const uint8_t* source =
                        static_cast<const uint8_t*>(writeBuffer) +
                        (transferred + submitted) * sectorSize;
                    for (uint32_t index = 0; index < sectorSize; ++index) {
                        reinterpret_cast<uint8_t*>(&request.data[3])[index] =
                            source[index];
                    }
                    request.length = os::ipcDataWordCount;
                }
                if (os::sendMessage(endpoint_, request) != 0) {
                    break;
                }
            }
            bool success = submitted == batch;
            for (uint32_t completed = 0; completed < submitted; ++completed) {
                os::IpcMessage completion{};
                if (os::receiveMessage(completionEndpoint_, completion) != 0 ||
                    completion.type != os::storageCompletionNotification ||
                    completion.data[0] >= batch ||
                    static_cast<int64_t>(completion.data[1]) != 0 ||
                    completion.data[2] != sectorSize) {
                    success = false;
                    continue;
                }
                if (!write) {
                    uint8_t* destination =
                        static_cast<uint8_t*>(readBuffer) +
                        (transferred + completion.data[0]) * sectorSize;
                    for (uint32_t index = 0; index < sectorSize; ++index) {
                        destination[index] = reinterpret_cast<const uint8_t*>(
                            &completion.data[3])[index];
                    }
                }
            }
            if (!success) return false;
            transferred += batch;
        }
        return true;
    }

    static constexpr uint32_t sectorSize = 512;
    uint64_t endpoint_                   = 0;
    uint64_t completionEndpoint_         = 0;
    uint64_t sectorCount_                = 0;
    bool readOnly_                       = false;
    uint32_t deviceId_                   = 0;
};

enum class DescriptorBackend : uint8_t { None, Ext3, Iso9660, Zero };

struct OpenFileDescription {
    uint32_t inode;
    uint64_t offset;
    uint64_t size;
    uint32_t isoExtent;
    uint32_t referenceCount;
    DescriptorBackend backend;
};

struct FileDescriptor {
    uint64_t ownerPid;
    OpenFileDescription* description;
};

class FsServer {
   public:
    bool initialize(uint64_t storageEndpoint, uint64_t deviceEndpoint) {
        os::clearMemory(descriptors_, sizeof(descriptors_));
        os::clearMemory(openDescriptions_, sizeof(openDescriptions_));
        deviceEndpoint_ = deviceEndpoint;
        if (!blockDevice_.initialize(storageEndpoint, 0) ||
            !blockDevice_.probe() || !volume_.mount(blockDevice_)) {
            return false;
        }
        fs::ext3::FileStatus fstab{};
        if (!volume_.lookup("/etc/fstab", fstab) ||
            (fstab.mode & 0xf000U) != fs::ext3::modeRegular ||
            fstab.size == 0 || fstab.size > sizeof(fstabText_) - 1 ||
            volume_.read(fstab.inode, 0, fstabText_, fstab.size) !=
                static_cast<int64_t>(fstab.size)) {
            return false;
        }
        fstabText_[fstab.size] = '\0';
        rootMounted_ = containsText(fstabText_, "/dev/hda1") &&
                       containsText(fstabText_, "ext3");
        cdromMounted_ = containsText(fstabText_, "/dev/cdrom") &&
                        containsText(fstabText_, "/mnt/cdrom") &&
                        containsText(fstabText_, "iso9660") &&
                        cdromDevice_.initialize(storageEndpoint, 1) &&
                        cdromDevice_.probe() && isoVolume_.mount(cdromDevice_);
        return rootMounted_;
    }

    bool handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        const auto operation = static_cast<os::FsRequest>(request.type);
        if (operation == os::FsRequest::ReadAtAsync ||
            operation == os::FsRequest::WriteAtAsync) {
            handleAsync(request, operation == os::FsRequest::WriteAtAsync);
            return false;
        }
        std::lock_guard<std::mutex> guard(lock_);
        const char* path = reinterpret_cast<const char*>(request.data);
        switch (operation) {
            case os::FsRequest::Open:
                reply.data[0] = open(request.senderPid, path);
                break;
            case os::FsRequest::Close:
                reply.data[0] = close(request.senderPid, request.data[0]);
                break;
            case os::FsRequest::Read:
                read(request.senderPid, request.data[0], request.data[1],
                     reply);
                break;
            case os::FsRequest::Write:
                write(request.senderPid, request, reply);
                break;
            case os::FsRequest::Seek:
                seek(request.senderPid, request.data[0],
                     static_cast<int64_t>(request.data[1]), request.data[2],
                     reply);
                break;
            case os::FsRequest::Stat:
                stat(path, reply);
                break;
            case os::FsRequest::Create:
                reply.data[0] = isVirtualPath(path) || !volume_.create(path, false) ? 1 : 0;
                break;
            case os::FsRequest::MakeDirectory:
                reply.data[0] = isVirtualPath(path) || !volume_.create(path, true) ? 1 : 0;
                break;
            case os::FsRequest::Remove:
                reply.data[0] = isVirtualPath(path) || !volume_.remove(path) ? 1 : 0;
                break;
            case os::FsRequest::ReadDirectory:
                readDirectory(path, request.data[7], reply);
                break;
            case os::FsRequest::ReadDirectoryBatch:
                readDirectoryBatch(path, request.data[7], request.data[8],
                                   reply);
                break;
            case os::FsRequest::Truncate:
                reply.data[0] = truncate(path);
                break;
            case os::FsRequest::Link:
                reply.data[0] =
                    isVirtualPath(path) ||
                            isVirtualPath(path + 256) ||
                            !volume_.link(path, path + 256)
                        ? 1
                        : 0;
                break;
            case os::FsRequest::ChangeMode:
                reply.data[0] =
                    isVirtualPath(path) ||
                            !volume_.changeMode(
                                path, static_cast<uint16_t>(request.data[32]))
                        ? 1
                        : 0;
                break;
            case os::FsRequest::ChangeOwner:
                reply.data[0] =
                    isVirtualPath(path) ||
                            request.data[32] > 0xffffU ||
                            request.data[33] > 0xffffU ||
                            !volume_.changeOwner(
                                path, static_cast<uint16_t>(request.data[32]),
                                static_cast<uint16_t>(request.data[33]))
                        ? 1
                        : 0;
                break;
            case os::FsRequest::ChangeTimes:
                reply.data[0] =
                    isVirtualPath(path) || request.data[32] > UINT32_MAX ||
                            request.data[33] > UINT32_MAX ||
                            !volume_.changeTimes(
                                path, static_cast<uint32_t>(request.data[32]),
                                static_cast<uint32_t>(request.data[33]))
                        ? 1
                        : 0;
                break;
            case os::FsRequest::CloneDescriptors:
                cloneDescriptors(request.data[0], request.senderPid, reply);
                break;
            case os::FsRequest::Mount:
                reply.data[0] = mount(request) ? 0 : 1;
                break;
            case os::FsRequest::Unmount:
                reply.data[0] = unmount(request) ? 0 : 1;
                break;
            case os::FsRequest::ListMounts:
                listMount(static_cast<uint32_t>(request.data[0]), reply);
                break;
            case os::FsRequest::Format:
                reply.data[0] = format(request) ? 0 : 1;
                break;
            default:
                reply.data[0] = 1;
                break;
        }
        if (reply.length == 0) reply.length = 1;
        return true;
    }

   private:
    void handleAsync(const os::IpcMessage& request, bool writeOperation) {
        if (request.handleCount != 1) {
            for (uint32_t index = 0; index < request.handleCount; ++index) {
                os::closeHandle(request.handles[index]);
            }
            return;
        }
        const uint64_t completionEndpoint = request.handles[0];
        os::IpcMessage completion{};
        completion.type    = os::fsCompletionNotification;
        completion.data[0] = request.data[3];
        completion.data[1] = static_cast<uint64_t>(
            static_cast<int64_t>(os::ErrorCode::InvalidArgument));
        completion.data[2] = 0;
        completion.length  = 3;
        {
            std::lock_guard<std::mutex> guard(lock_);
            OpenFileDescription* entry =
                getDescriptor(request.senderPid, request.data[0]);
            uint64_t size = request.data[2];
            const uint64_t capacity =
                (os::ipcDataWordCount - 4) * sizeof(uint64_t);
            const uint32_t requiredLength = static_cast<uint32_t>(
                4 + (writeOperation
                         ? (size + sizeof(uint64_t) - 1) / sizeof(uint64_t)
                         : 0));
            if (entry != nullptr && entry->backend == DescriptorBackend::Ext3 &&
                size != 0 && size <= capacity &&
                request.length >= requiredLength) {
                int64_t count;
                if (writeOperation) {
                    count = volume_.write(entry->inode, request.data[1],
                                          &request.data[4], size);
                    const uint64_t end =
                        request.data[1] + (count > 0 ? count : 0);
                    if (end > entry->size) entry->size = end;
                } else {
                    count = volume_.read(entry->inode, request.data[1],
                                         &completion.data[3], size);
                }
                if (count >= 0) {
                    completion.data[1] = 0;
                    completion.data[2] = static_cast<uint64_t>(count);
                    if (!writeOperation) {
                        completion.length = static_cast<uint32_t>(
                            3 +
                            (count + sizeof(uint64_t) - 1) / sizeof(uint64_t));
                    }
                } else {
                    completion.data[1] = static_cast<uint64_t>(
                        static_cast<int64_t>(os::ErrorCode::IoError));
                }
            }
        }
        os::sendMessage(completionEndpoint, completion);
        os::closeHandle(completionEndpoint);
    }

    uint64_t open(uint64_t ownerPid, const char* path) {
        DescriptorBackend backend = DescriptorBackend::Ext3;
        uint32_t inode = 0;
        uint32_t isoExtent = 0;
        uint64_t fileSize = 0;
        if (os::stringsEqual(path, "/dev/zero")) {
            backend = DescriptorBackend::Zero;
            fileSize = ~0ULL;
        } else if (cdromMounted_ && startsWithPath(path, "/mnt/cdrom")) {
            fs::iso9660::FileStatus isoStatus{};
            const char* relative = path + 10;
            if (*relative == '\0') relative = "/";
            if (!isoVolume_.lookup(relative, isoStatus) || isoStatus.directory) {
                return 0;
            }
            backend = DescriptorBackend::Iso9660;
            isoExtent = isoStatus.extent;
            fileSize = isoStatus.size;
        } else {
        fs::ext3::FileStatus status{};
        if (!volume_.lookup(path, status) ||
            (status.mode & 0xf000U) != fs::ext3::modeRegular) {
            return 0;
        }
            inode = status.inode;
            fileSize = status.size;
        }
        uint32_t descriptorIndex = descriptorCapacity;
        uint32_t descriptionIndex = descriptorCapacity;
        for (uint32_t index = 0; index < descriptorCapacity; ++index) {
            if (descriptorIndex == descriptorCapacity &&
                descriptors_[index].description == nullptr) {
                descriptorIndex = index;
            }
            if (descriptionIndex == descriptorCapacity &&
                openDescriptions_[index].backend == DescriptorBackend::None) {
                descriptionIndex = index;
            }
        }
        if (descriptorIndex == descriptorCapacity ||
            descriptionIndex == descriptorCapacity) {
            return 0;
        }
        OpenFileDescription& description =
            openDescriptions_[descriptionIndex];
        description = {inode, 0, fileSize, isoExtent, 1, backend};
        descriptors_[descriptorIndex] = {ownerPid, &description};
        return descriptorIndex + 1;
    }

    uint64_t close(uint64_t ownerPid, uint64_t descriptor) {
        FileDescriptor* entry = getDescriptorRecord(ownerPid, descriptor);
        if (entry == nullptr || entry->description == nullptr) return 1;
        OpenFileDescription* description = entry->description;
        *entry = {};
        if (description->referenceCount != 0) {
            --description->referenceCount;
        }
        if (description->referenceCount == 0) {
            *description = {};
        }
        return 0;
    }

    void cloneDescriptors(uint64_t parentPid, uint64_t childPid,
                          os::IpcMessage& reply) {
        uint32_t sourceIndices[descriptorCapacity]{};
        uint32_t sourceCount = 0;
        uint32_t available = 0;
        for (uint32_t index = 0; index < descriptorCapacity; ++index) {
            if (descriptors_[index].description == nullptr) {
                ++available;
            } else if (descriptors_[index].ownerPid == parentPid) {
                sourceIndices[sourceCount++] = index;
            }
        }
        reply.data[0] = 1;
        reply.data[1] = 0;
        reply.length = 2;
        if (parentPid == 0 || childPid == 0 || parentPid == childPid ||
            sourceCount > available) {
            return;
        }
        uint32_t created[descriptorCapacity]{};
        uint32_t createdCount = 0;
        for (uint32_t source = 0; source < sourceCount; ++source) {
            uint32_t destination = descriptorCapacity;
            for (uint32_t index = 0; index < descriptorCapacity; ++index) {
                if (descriptors_[index].description == nullptr) {
                    destination = index;
                    break;
                }
            }
            if (destination == descriptorCapacity) {
                for (uint32_t index = 0; index < createdCount; ++index) {
                    OpenFileDescription* description =
                        descriptors_[created[index]].description;
                    if (description != nullptr &&
                        description->referenceCount != 0) {
                        --description->referenceCount;
                    }
                    descriptors_[created[index]] = {};
                }
                return;
            }
            descriptors_[destination] = descriptors_[sourceIndices[source]];
            descriptors_[destination].ownerPid = childPid;
            ++descriptors_[destination].description->referenceCount;
            created[createdCount++] = destination;
            reply.data[2 + source * 2] = sourceIndices[source] + 1;
            reply.data[3 + source * 2] = destination + 1;
        }
        reply.data[0] = 0;
        reply.data[1] = sourceCount;
        reply.length = 2 + sourceCount * 2;
    }

    void read(uint64_t ownerPid, uint64_t descriptor, uint64_t requested,
              os::IpcMessage& reply) {
        OpenFileDescription* entry = getDescriptor(ownerPid, descriptor);
        if (entry == nullptr) {
            reply.data[0] = 0;
            return;
        }
        const uint64_t capacity = (os::ipcDataWordCount - 1) * sizeof(uint64_t);
        if (requested > capacity) requested = capacity;
        int64_t count = -1;
        if (entry->backend == DescriptorBackend::Ext3) {
            count = volume_.read(entry->inode, entry->offset, &reply.data[1],
                                 requested);
        } else if (entry->backend == DescriptorBackend::Iso9660) {
            const fs::iso9660::FileStatus status{
                entry->isoExtent, static_cast<uint32_t>(entry->size), false};
            count = isoVolume_.read(status, entry->offset, &reply.data[1],
                                    requested);
        } else if (entry->backend == DescriptorBackend::Zero) {
            os::IpcMessage request{};
            request.type = static_cast<uint64_t>(os::DeviceRequest::ReadNode);
            os::copyString(reinterpret_cast<char*>(request.data), "zero",
                           6 * sizeof(uint64_t));
            request.data[6] = requested;
            request.length = 7;
            os::IpcMessage deviceReply{};
            if (deviceEndpoint_ != 0 &&
                os::callService(deviceEndpoint_, request, deviceReply) == 0 &&
                static_cast<int64_t>(deviceReply.data[0]) == 0) {
                count = static_cast<int64_t>(deviceReply.data[1]);
                for (int64_t index = 0; index < count; ++index) {
                    reinterpret_cast<uint8_t*>(&reply.data[1])[index] =
                        reinterpret_cast<const uint8_t*>(&deviceReply.data[2])[index];
                }
            }
        }
        reply.data[0]       = count < 0 ? 0 : static_cast<uint64_t>(count);
        entry->offset += reply.data[0];
        reply.length = static_cast<uint32_t>(
            1 + (reply.data[0] + sizeof(uint64_t) - 1) / sizeof(uint64_t));
    }

    void write(uint64_t ownerPid, const os::IpcMessage& request,
               os::IpcMessage& reply) {
        OpenFileDescription* entry =
            getDescriptor(ownerPid, request.data[0]);
        if (entry == nullptr || entry->backend != DescriptorBackend::Ext3) {
            reply.data[0] = 0;
            return;
        }
        uint64_t size           = request.data[1];
        const uint64_t capacity = (os::ipcDataWordCount - 2) * sizeof(uint64_t);
        if (size > capacity) size = capacity;
        const int64_t count =
            volume_.write(entry->inode, entry->offset, &request.data[2], size);
        reply.data[0] = count < 0 ? 0 : static_cast<uint64_t>(count);
        entry->offset += reply.data[0];
        if (entry->offset > entry->size) entry->size = entry->offset;
    }

    void seek(uint64_t ownerPid, uint64_t descriptor, int64_t offset,
              uint64_t whence, os::IpcMessage& reply) {
        OpenFileDescription* entry = getDescriptor(ownerPid, descriptor);
        uint64_t base = 0;
        if (entry == nullptr || whence > 2) {
            reply.data[0] = 1;
            return;
        }
        if (whence == 1) base = entry->offset;
        if (whence == 2) base = entry->size;
        uint64_t position = base;
        if (offset < 0) {
            const uint64_t magnitude =
                static_cast<uint64_t>(-(offset + 1)) + 1;
            if (magnitude > base) {
                reply.data[0] = 1;
                return;
            }
            position -= magnitude;
        } else {
            const uint64_t distance = static_cast<uint64_t>(offset);
            if (distance > UINT64_MAX - base) {
                reply.data[0] = 1;
                return;
            }
            position += distance;
        }
        entry->offset = position;
        reply.data[0] = 0;
        reply.data[1] = position;
        reply.length = 2;
    }

    void stat(const char* path, os::IpcMessage& reply) {
        if (os::stringsEqual(path, "/dev") ||
            os::stringsEqual(path, "/dev/pts")) {
            setStat(reply, os::FileType::Directory, 0, 0755);
            return;
        }
        if (startsWithPath(path, "/dev")) {
            setStat(reply, os::FileType::File, 0, 0666);
            return;
        }
        if (cdromMounted_ && startsWithPath(path, "/mnt/cdrom")) {
            fs::iso9660::FileStatus status{};
            const char* relative = path + 10;
            if (*relative == '\0') relative = "/";
            if (!isoVolume_.lookup(relative, status)) {
                reply.data[0] = 1;
            } else {
                setStat(reply, status.directory ? os::FileType::Directory
                                                : os::FileType::File,
                        status.size, status.directory ? 0555 : 0444);
            }
            return;
        }
        fs::ext3::FileStatus status{};
        if (!volume_.lookup(path, status)) {
            reply.data[0] = 1;
            return;
        }
        reply.data[0] = 0;
        reply.data[1] = (status.mode & 0xf000U) == fs::ext3::modeDirectory
                            ? static_cast<uint64_t>(os::FileType::Directory)
                            : static_cast<uint64_t>(os::FileType::File);
        reply.data[2] = status.size;
        reply.data[3] = status.mode & 07777U;
        reply.data[4] = status.inode;
        reply.data[5] = status.links;
        reply.data[6] = status.uid;
        reply.data[7] = status.gid;
        reply.data[8] = status.accessTime;
        reply.data[9] = status.modificationTime;
        reply.data[10] = status.changeTime;
        reply.length  = 11;
    }

    uint64_t truncate(const char* path) {
        fs::ext3::FileStatus status{};
        return volume_.lookup(path, status) &&
                       (status.mode & 0xf000U) == fs::ext3::modeRegular &&
                       volume_.truncate(status.inode)
                   ? 0
                   : 1;
    }

    void readDirectory(const char* path, uint64_t index,
                       os::IpcMessage& reply) {
        if (os::stringsEqual(path, "/dev")) {
            os::IpcMessage request{};
            request.type = static_cast<uint64_t>(os::DeviceRequest::ListNode);
            request.data[0] = index;
            request.length = 1;
            os::IpcMessage deviceReply{};
            if (deviceEndpoint_ == 0 ||
                os::callService(deviceEndpoint_, request, deviceReply) != 0 ||
                deviceReply.data[0] != 0) {
                reply.data[0] = 2;
                return;
            }
            reply.data[0] = 0;
            reply.data[1] = deviceReply.data[1]
                                ? static_cast<uint64_t>(os::FileType::Directory)
                                : static_cast<uint64_t>(os::FileType::File);
            os::copyString(reinterpret_cast<char*>(&reply.data[2]),
                           reinterpret_cast<const char*>(&deviceReply.data[2]),
                           6 * sizeof(uint64_t));
            reply.length = 8;
            return;
        }
        if (cdromMounted_ && startsWithPath(path, "/mnt/cdrom")) {
            fs::iso9660::DirectoryEntry entry{};
            const char* relative = path + 10;
            if (*relative == '\0') relative = "/";
            const int status = isoVolume_.readDirectory(
                relative, static_cast<uint32_t>(index), entry);
            reply.data[0] = status < 0 ? 1 : (status > 0 ? 2 : 0);
            if (status == 0) {
                reply.data[1] = static_cast<uint64_t>(
                    entry.directory ? os::FileType::Directory
                                    : os::FileType::File);
                os::copyString(reinterpret_cast<char*>(&reply.data[2]),
                               entry.name, 6 * sizeof(uint64_t));
                reply.length = 8;
            }
            return;
        }
        fs::ext3::DirectoryEntry entry{};
        const int status =
            volume_.readDirectory(path, static_cast<uint32_t>(index), entry);
        reply.data[0] = status < 0 ? 1 : (status > 0 ? 2 : 0);
        if (status == 0) {
            reply.data[1] = entry.type == 2
                                ? static_cast<uint64_t>(os::FileType::Directory)
                                : static_cast<uint64_t>(os::FileType::File);
            os::copyString(reinterpret_cast<char*>(&reply.data[2]), entry.name,
                           6 * sizeof(uint64_t));
            reply.length = 8;
        }
    }

    void readDirectoryBatch(const char* path, uint64_t index,
                            uint64_t requested,
                            os::IpcMessage& reply) {
        constexpr uint32_t maximumEntries =
            (os::ipcDataWordCount - 2) * sizeof(uint64_t) /
            sizeof(os::DirectoryEntry);
        uint32_t capacity = requested < maximumEntries
                                ? static_cast<uint32_t>(requested)
                                : maximumEntries;
        if (capacity == 0) {
            reply.data[0] = 1;
            return;
        }

        os::DirectoryEntry entries[maximumEntries]{};
        uint32_t count = 0;
        if (!os::stringsEqual(path, "/dev") &&
            !(cdromMounted_ && startsWithPath(path, "/mnt/cdrom"))) {
            fs::ext3::DirectoryEntry extEntries[maximumEntries]{};
            const int result = volume_.readDirectoryBatch(
                path, static_cast<uint32_t>(index), extEntries, capacity);
            if (result < 0) {
                reply.data[0] = 1;
                return;
            }
            count = static_cast<uint32_t>(result);
            for (uint32_t entry = 0; entry < count; ++entry) {
                entries[entry].type =
                    extEntries[entry].type == 2 ? os::FileType::Directory
                                                : os::FileType::File;
                os::copyString(entries[entry].name, extEntries[entry].name,
                               sizeof(entries[entry].name));
            }
        } else {
            for (; count < capacity; ++count) {
                os::IpcMessage single{};
                readDirectory(path, index + count, single);
                if (single.data[0] == 2) break;
                if (single.data[0] != 0) {
                    reply.data[0] = 1;
                    return;
                }
                entries[count].type =
                    static_cast<os::FileType>(single.data[1]);
                os::copyString(entries[count].name,
                               reinterpret_cast<const char*>(&single.data[2]),
                               sizeof(entries[count].name));
            }
        }

        reply.data[0] = 0;
        reply.data[1] = count;
        auto* destination =
            reinterpret_cast<os::DirectoryEntry*>(&reply.data[2]);
        for (uint32_t entry = 0; entry < count; ++entry) {
            destination[entry] = entries[entry];
        }
        reply.length = static_cast<uint32_t>(
            2 + (count * sizeof(os::DirectoryEntry) + sizeof(uint64_t) - 1) /
                    sizeof(uint64_t));
    }

    bool mount(const os::IpcMessage& request) {
        const char* device = reinterpret_cast<const char*>(request.data);
        const char* target = device + 64;
        const char* type   = device + 128;
        if (os::stringsEqual(device, "/dev/hda1") &&
            os::stringsEqual(target, "/") && os::stringsEqual(type, "ext3")) {
            return rootMounted_;
        }
        if (os::stringsEqual(device, "/dev/cdrom") &&
            os::stringsEqual(target, "/mnt/cdrom") &&
            os::stringsEqual(type, "iso9660")) {
            cdromMounted_ = isoVolume_.mount(cdromDevice_);
            return cdromMounted_;
        }
        return false;
    }

    bool unmount(const os::IpcMessage& request) {
        const char* target = reinterpret_cast<const char*>(request.data);
        if (!os::stringsEqual(target, "/mnt/cdrom")) return false;
        for (const OpenFileDescription& entry : openDescriptions_) {
            if (entry.backend == DescriptorBackend::Iso9660) return false;
        }
        cdromMounted_ = false;
        return true;
    }

    void listMount(uint32_t index, os::IpcMessage& reply) const {
        if (index > 1 || (index == 1 && !cdromMounted_)) {
            reply.data[0] = 1;
            return;
        }
        reply.data[0] = 0;
        os::copyString(reinterpret_cast<char*>(&reply.data[1]),
                       index == 0 ? "/dev/hda1" : "/dev/cdrom",
                       8 * sizeof(uint64_t));
        os::copyString(reinterpret_cast<char*>(&reply.data[9]),
                       index == 0 ? "/" : "/mnt/cdrom",
                       8 * sizeof(uint64_t));
        os::copyString(reinterpret_cast<char*>(&reply.data[17]),
                       index == 0 ? "ext3" : "iso9660",
                       2 * sizeof(uint64_t));
        reply.data[19] = index == 0 ? (blockDevice_.readOnly() ? 1 : 0) : 1;
        reply.length   = 20;
    }

    bool format(const os::IpcMessage& request) const {
        const char* device = reinterpret_cast<const char*>(request.data);
        const char* type   = device + 64;
        // The sole exposed partition is the mounted root and must stay busy.
        return !os::stringsEqual(device, "/dev/hda1") &&
               os::stringsEqual(type, "ext3") && false;
    }

    FileDescriptor* getDescriptorRecord(uint64_t ownerPid,
                                        uint64_t descriptor) {
        if (descriptor == 0 || descriptor > descriptorCapacity) return nullptr;
        FileDescriptor& entry = descriptors_[descriptor - 1];
        return entry.description != nullptr && entry.ownerPid == ownerPid
                   ? &entry
                   : nullptr;
    }

    OpenFileDescription* getDescriptor(uint64_t ownerPid,
                                       uint64_t descriptor) {
        FileDescriptor* entry = getDescriptorRecord(ownerPid, descriptor);
        return entry == nullptr ? nullptr : entry->description;
    }

    static bool isVirtualPath(const char* path) {
        return startsWithPath(path, "/dev") ||
               startsWithPath(path, "/mnt/cdrom");
    }

    static void setStat(os::IpcMessage& reply, os::FileType type,
                        uint64_t size, uint32_t mode) {
        reply.data[0] = 0;
        reply.data[1] = static_cast<uint64_t>(type);
        reply.data[2] = size;
        reply.data[3] = mode;
        for (uint32_t index = 4; index <= 10; ++index) reply.data[index] = 0;
        reply.length = 11;
    }

    BlockDeviceClient blockDevice_;
    BlockDeviceClient cdromDevice_;
    fs::ext3::Volume volume_;
    fs::iso9660::Volume isoVolume_;
    FileDescriptor descriptors_[descriptorCapacity]{};
    OpenFileDescription openDescriptions_[descriptorCapacity]{};
    uint64_t deviceEndpoint_ = 0;
    char fstabText_[1024]{};
    bool rootMounted_ = false;
    bool cdromMounted_ = false;
    std::mutex lock_;
};

FsServer fsServer;
uint64_t fsEndpoint;

[[noreturn]] void runFsWorker() {
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(fsEndpoint, request) != 0) continue;
        os::IpcMessage reply{};
        if (fsServer.handle(request, reply)) {
            os::replyMessage(fsEndpoint, reply);
        }
    }
}

}  // namespace

extern "C" int userMain() {
    os::log("[FS] FS Server started in Ring 3\n");
    const uint64_t registry = os::getBootstrapHandle();
    fsEndpoint              = os::createEndpoint();
    if (registry == 0 || fsEndpoint == 0 ||
        !os::registerService(registry, os::fsServiceName, fsEndpoint)) {
        os::log("[FS] registration failed\n");
        return 1;
    }
    const uint64_t storage =
        os::lookupService(registry, os::storageServiceName, 5000);
    const uint64_t device =
        os::lookupService(registry, os::deviceServiceName, 5000);
    if (!fsServer.initialize(storage, device)) {
        os::log("[FS] ext3 initialization failed\n");
        return 2;
    }
    if (os::mapMemory(workerStackAddress, workerStackSize * workerThreadCount,
                      1) != workerStackAddress) {
        os::log("[FS] worker stack allocation failed\n");
        return 3;
    }
    for (uint32_t index = 0; index < workerThreadCount; ++index) {
        const uint64_t stackTop =
            workerStackAddress + workerStackSize * (index + 1);
        if (os::createThread(runFsWorker, stackTop) == 0) {
            os::log("[FS] unable to start request worker\n");
            return 4;
        }
    }
    os::log("[FS] storage IPC probe complete\n");
    os::log("[FS] fstab mounted ext3 at / and ISO9660 at /mnt/cdrom\n");
    runFsWorker();
}
