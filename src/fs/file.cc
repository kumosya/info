#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/procfs/process_object.h"
#include "service/fs/server.h"
#include <os/task.h>

namespace vfs::server {
void FsServer::readBuffer(const os::IpcMessage& request,os::IpcMessage& reply) {
    struct Handles {
        const os::IpcMessage& message;
        ~Handles(){for(unsigned i=0;i<message.handleCount && i<4;++i)os::closeHandle(message.handles[i]);}
    } handles{request};
    constexpr uint64_t capacity=65536,base=0x3c00000000ULL;
    reply.length=2;reply.data[1]=0x4653425546464552ULL;reply.data[0]=int64_t(os::ErrorCode::InvalidArgument);
    if(request.length!=3 || request.handleCount!=1 || !request.data[2] || request.data[2]>capacity ||
       (request.data[1]!=UINT64_MAX && request.data[1]>INT64_MAX-request.data[2]))return;
    if(!permitted(request)){reply.data[0]=int64_t(os::ErrorCode::PermissionDenied);return;}
    auto* entry=acquireDescription(request.senderPid,request.data[0]);
    if(!entry){reply.data[0]=int64_t(os::ErrorCode::InvalidHandle);return;}
    struct Description {FsServer* server;OpenFileDescription* entry;~Description(){server->releaseDescription(entry);}} description{this,entry};
    if(!entry->filesystem || (entry->mode&0xf000U)!=vfs::modeRegular){reply.data[0]=int64_t(os::ErrorCode::NotSupported);return;}
    static uint32_t slots=0;
    unsigned slot=16;
    for(unsigned i=0;i<16;++i)if(!(__atomic_fetch_or(&slots,1U<<i,__ATOMIC_ACQ_REL)&(1U<<i))){slot=i;break;}
    if(slot==16){reply.data[0]=int64_t(os::ErrorCode::NotSupported);return;}
    const uint64_t address=base+slot*capacity;
    struct Mapping {uint32_t* slots;unsigned slot;uint64_t address=0;~Mapping(){if(address)os::detail::MemoryControl().unmapTarget(address,65536);__atomic_fetch_and(slots,~(1U<<slot),__ATOMIC_RELEASE);}} mapping{&slots,slot};
    if(os::detail::MemoryControl().mapPages(request.handles[0],address,capacity,0,1)!=address){reply.data[0]=int64_t(os::ErrorCode::InvalidHandle);return;}
    mapping.address=address;
    std::lock_guard<std::mutex> guard(descriptionMutex(entry));
    const bool positioned=request.data[1]!=UINT64_MAX;
    const auto offset=positioned?request.data[1]:entry->offset;
    const vfs::FileStatus status{entry->inode,entry->mode,entry->size,entry->uid,entry->gid,1,0,0,0,entry->privateData};
    std::lock_guard<std::mutex> filesystem(fileSystemLock(entry->mountIndex));
    const auto count=entry->filesystem->read(status,offset,reinterpret_cast<void*>(address),request.data[2]);
    if(count>=0 && uint64_t(count)<=request.data[2]) {
        if(!positioned)entry->offset+=uint64_t(count);
        reply.data[0]=uint64_t(count);
    }else reply.data[0]=int64_t(os::ErrorCode::IoError);
}

void FsServer::delegateMapping(const os::IpcMessage& request,
                               os::IpcMessage& reply) {
    reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied);
    reply.length  = 3;
    if (request.length != 1) return;
    std::lock_guard<std::mutex> guard(descriptorLock_);
    auto* source = getDescriptorRecord(request.senderPid, request.data[0]);
    if (!source || (source->flags & O_ACCMODE) == O_WRONLY ||
        source->description->backend != DescriptorBackend::Filesystem)
        return;
    for (uint32_t i = 0; i < descriptorCapacity; ++i)
        if (!descriptors_[i].description) {
            if (!nextMappingToken_) return;
            auto& target         = descriptors_[i];
            target               = *source;
            target.mappingIssuer = request.senderPid;
            target.mappingToken  = nextMappingToken_++;
            ++target.description->referenceCount;
            reply.data[0] = 0;
            reply.data[1] = target.mappingToken;
            reply.data[2] = i + 1;
            return;
        }
    reply.data[0] = static_cast<int64_t>(os::ErrorCode::OutOfMemory);
}

void FsServer::claimMapping(const os::IpcMessage& request,
                            os::IpcMessage& reply) {
    reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied);
    reply.length  = 3;
    os::wire::TaskInfo caller{};
    if (request.length != 2 ||
        os::detail::TaskQuery().query(request.senderPid, caller) ||
        caller.serviceRole != static_cast<uint32_t>(os::ServiceRole::Memory))
        return;
    std::lock_guard<std::mutex> guard(descriptorLock_);
    for (uint32_t i = 0; i < descriptorCapacity; ++i) {
        auto& entry = descriptors_[i];
        if (!entry.description || !entry.mappingToken ||
            entry.mappingToken != request.data[0] ||
            entry.mappingIssuer != request.data[1] ||
            entry.ownerPid != entry.mappingIssuer)
            continue;
        entry.ownerPid       = 0;
        entry.mappingClaimed = true;
        reply.data[0]        = 0;
        reply.data[1]        = i + 1;
        reply.data[2]        = entry.flags;
        reply.data[3]        = entry.mappingToken;
        reply.length         = 4;
        return;
    }
}

void FsServer::releaseMapping(const os::IpcMessage& request,
                              os::IpcMessage& reply) {
    reply.data[0] = 0;
    reply.length  = 1;
    if (request.length != 1 || !request.data[0]) {
        reply.data[0] = static_cast<uint64_t>(-1LL);
        return;
    }
    std::lock_guard<std::mutex> guard(descriptorLock_);
    for (auto& entry : descriptors_)
        if (entry.description && entry.mappingClaimed &&
            entry.mappingToken == request.data[0]) {
            auto* description = entry.description;
            entry             = {};
            if (description->referenceCount) --description->referenceCount;
            if (!description->referenceCount && !description->filesystem)
                *description = {};
            return;
        }
}
void FsServer::listMappings(const os::IpcMessage& request,
                            os::IpcMessage& reply) {
    reply.data[0] = 0;
    reply.length  = 5;
    if (request.length != 1 || request.data[0] > descriptorCapacity) {
        reply.data[0] = static_cast<uint64_t>(-1LL);
        return;
    }
    std::lock_guard<std::mutex> guard(descriptorLock_);
    for (uint32_t i = request.data[0]; i < descriptorCapacity; ++i) {
        const auto& entry = descriptors_[i];
        if (!entry.description || !entry.mappingClaimed) continue;
        reply.data[1] = entry.mappingToken;
        reply.data[2] = i + 1;
        reply.data[3] = entry.mappingIssuer;
        reply.data[4] = i + 1;
        return;
    }
    reply.data[1] = 0;
}
void FsServer::cancelMapping(const os::IpcMessage& request,
                             os::IpcMessage& reply) {
    reply.data[0] = 0;
    reply.length  = 1;
    if (request.length != 1 || !request.data[0]) {
        reply.data[0] = static_cast<uint64_t>(-1LL);
        return;
    }
    std::lock_guard<std::mutex> guard(descriptorLock_);
    for (auto& entry : descriptors_)
        if (entry.description && !entry.mappingClaimed &&
            entry.mappingIssuer == request.senderPid &&
            entry.mappingToken == request.data[0]) {
            auto* description = entry.description;
            entry             = {};
            if (description->referenceCount) --description->referenceCount;
            if (!description->referenceCount && !description->filesystem)
                *description = {};
            return;
        }
}

void FsServer::handleAsync(const os::IpcMessage& request, bool writeOperation) {
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
    OpenFileDescription* entry =
        acquireDescription(request.senderPid, request.data[0]);
    if (entry != nullptr) {
        uint64_t size           = request.data[2];
        const uint64_t capacity = (os::ipcDataWordCount - 4) * sizeof(uint64_t);
        const uint32_t requiredLength = static_cast<uint32_t>(
            4 + (writeOperation
                     ? (size + sizeof(uint64_t) - 1) / sizeof(uint64_t)
                     : 0));
        if (!writeOperation && size && size <= capacity &&
            request.length >= 4 &&
            (entry->backend == DescriptorBackend::ProcessFile ||
             entry->backend == DescriptorBackend::SystemFile)) {
            int64_t count = 0;
            if (entry->backend == DescriptorBackend::ProcessFile) {
                const uint64_t offset = request.data[1];
                if (offset < entry->size) {
                    if (size > entry->size - offset)
                        size = entry->size - offset;
                    os::Runtime::copyBytes(&completion.data[3],
                                           entry->snapshot + offset, size);
                    count = size;
                }
            } else
                count = vfs::SystemFiles::read(entry->inode, request.data[1],
                                               &completion.data[3], size);
            completion.data[1] =
                count < 0 ? static_cast<int64_t>(os::ErrorCode::IoError) : 0;
            completion.data[2] = count < 0 ? 0 : count;
            if (count > 0) completion.length = 3 + (count + 7) / 8;
        } else if (entry->filesystem != nullptr && size != 0 &&
                   size <= capacity && request.length >= requiredLength &&
                   (!writeOperation || !readOnly(entry->mountIndex))) {
            const vfs::FileStatus status{
                entry->inode,      entry->mode, entry->size, 0, 0, 0, 0, 0, 0,
                entry->privateData};
            int64_t count;
            {
                std::lock_guard<std::mutex> guard(
                    fileSystemLock(entry->mountIndex));
                count =
                    writeOperation
                        ? entry->filesystem->write(status, request.data[1],
                                                   &request.data[4], size)
                        : entry->filesystem->read(status, request.data[1],
                                                  &completion.data[3], size);
            }
            if (writeOperation) {
                const uint64_t end = request.data[1] + (count > 0 ? count : 0);
                std::lock_guard<std::mutex> g(descriptionMutex(entry));
                if (end > entry->size) entry->size = end;
            }
            if (count >= 0) {
                completion.data[1] = 0;
                completion.data[2] = static_cast<uint64_t>(count);
                if (!writeOperation) {
                    completion.length = static_cast<uint32_t>(
                        3 + (count + sizeof(uint64_t) - 1) / sizeof(uint64_t));
                }
            } else {
                completion.data[1] = static_cast<uint64_t>(
                    static_cast<int64_t>(os::ErrorCode::IoError));
            }
        }
        releaseDescription(entry);
    }
    os::sendMessage(completionEndpoint, completion);
    os::closeHandle(completionEndpoint);
}

uint64_t FsServer::open(uint64_t ownerPid, const char* path, int flags) {
    reapDescriptors();
    uint64_t object       = 0;
    const auto objectKind = ProcessObjectPath::parse(path, ownerPid, object);
    if (objectKind == ProcessObjectPath::Denied) return 0;
    if (objectKind == ProcessObjectPath::File)
        return openProcessObject(ownerPid, object, flags, path);
    DescriptorBackend backend = DescriptorBackend::Filesystem;
    uint32_t inode            = 0;
    uint64_t fileSize         = 0;
    const int deviceIndex     = catalog_.resolve(path);
    vfs::FileStatus virtualStatus{};
    char processSnapshot[1536]{};
    if (PathText::startsWithPath(path, "/sys")) {
        if (!vfs::SystemFiles::lookup(path, virtualStatus) ||
            (flags & O_ACCMODE) != O_RDONLY || (flags & (O_TRUNC | O_CREAT)))
            return 0;
        backend  = DescriptorBackend::SystemFile;
        inode    = virtualStatus.inode;
        fileSize = virtualStatus.size;
    } else if (PathText::startsWithPath(path, "/proc")) {
        if (!vfs::ProcessFiles::lookup(path, ownerPid, virtualStatus) ||
            (flags & O_ACCMODE) != O_RDONLY || (flags & (O_TRUNC | O_CREAT)))
            return 0;
        backend = DescriptorBackend::ProcessFile;
        inode   = virtualStatus.inode;
        if ((virtualStatus.mode & 0xf000) == vfs::modeRegular) {
            const auto count = vfs::ProcessFiles::snapshot(
                path, ownerPid, processSnapshot, sizeof(processSnapshot));
            if (count < 0) return 0;
            fileSize = static_cast<uint64_t>(count);
        }
    } else if (os::Runtime::stringsEqual(path, "/dev/zero")) {
        backend  = DescriptorBackend::Zero;
        fileSize = ~0ULL;
    } else if (os::Runtime::stringsEqual(path, "/dev/null")) {
        backend = DescriptorBackend::Null;
    } else if (deviceIndex >= 0) {
        backend  = DescriptorBackend::BlockDevice;
        fileSize = devices_[deviceIndex].sectorCount() * 512;
    } else if (PathText::startsWithPath(path, "/dev") &&
               (flags & O_DIRECTORY)) {
        os::IpcMessage status{};
        stat(path, status, ownerPid);
        if (status.data[0] ||
            status.data[1] !=
                static_cast<uint64_t>(os::File::Type::Directory) ||
            (flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC)))
            return 0;
        backend             = DescriptorBackend::DeviceDirectory;
        virtualStatus.mode  = vfs::modeDirectory | 0755;
        virtualStatus.links = 2;
    } else {
        const PathBackend pathType = pathBackend(path);
        if (pathType == PathBackend::Invalid) return 0;
        vfs::FileStatus status{};
        vfs::FileSystem* filesystem = fileSystem(pathType);
        if (filesystem == nullptr) return 0;
        std::lock_guard<std::mutex> guard(fileSystemLock(pathType));
        if (!mounted_[static_cast<uint32_t>(pathType)] ||
            !filesystem->lookup(relativePath(path, pathType), status)) {
            return 0;
        }
        backend  = DescriptorBackend::Filesystem;
        inode    = status.inode;
        fileSize = status.size;
        std::lock_guard<std::mutex> descriptorGuard(descriptorLock_);
        uint32_t descriptorIndex  = descriptorCapacity;
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
        if (!filesystem->retainOpenFile(inode)) return 0;
        OpenFileDescription& description = openDescriptions_[descriptionIndex];
        description.inode                = inode;
        description.mode                 = status.mode;
        description.uid                  = status.uid;
        description.gid                  = status.gid;
        description.links                = status.links;
        description.accessTime           = status.accessTime;
        description.modificationTime     = status.modificationTime;
        description.changeTime           = status.changeTime;
        os::Runtime::copyString(description.path, path,
                                sizeof(description.path));
        description.offset            = 0;
        description.size              = fileSize;
        description.privateData       = status.privateData;
        description.referenceCount    = 1;
        description.filesystem        = filesystem;
        description.backend           = backend;
        description.mountIndex        = pathType;
        descriptors_[descriptorIndex] = {ownerPid, &description, flags};
        return descriptorIndex + 1;
    }
    std::lock_guard<std::mutex> guard(descriptorLock_);
    uint32_t descriptorIndex  = descriptorCapacity;
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
    OpenFileDescription& description = openDescriptions_[descriptionIndex];
    os::Runtime::copyString(description.path, path, sizeof(description.path));
    description.mode  = virtualStatus.mode;
    description.uid   = virtualStatus.uid;
    description.gid   = virtualStatus.gid;
    description.links = virtualStatus.links;
    if (backend == DescriptorBackend::ProcessFile)
        os::Runtime::copyBytes(description.snapshot, processSnapshot, fileSize);
    description.inode          = inode;
    description.offset         = 0;
    description.size           = fileSize;
    description.privateData    = 0;
    description.referenceCount = 1;
    description.filesystem     = nullptr;
    description.backend        = backend;
    description.privateData =
        deviceIndex >= 0 ? static_cast<uint64_t>(deviceIndex) : 0;
    description.mountIndex        = PathBackend::Invalid;
    descriptors_[descriptorIndex] = {ownerPid, &description, flags};
    return descriptorIndex + 1;
}

uint64_t FsServer::close(uint64_t ownerPid, uint64_t descriptor) {
    std::lock_guard<std::mutex> guard(descriptorLock_);
    FileDescriptor* entry = getDescriptorRecord(ownerPid, descriptor);
    if (entry == nullptr || entry->description == nullptr) return 1;
    // Mapping leases outlive an MM process and are released after its
    // checkpoint commits.
    if (entry->mappingClaimed) return 0;
    OpenFileDescription* description = entry->description;
    *entry                           = {};
    if (description->referenceCount != 0) {
        --description->referenceCount;
    }
    if (description->referenceCount == 0 && !description->filesystem) {
        *description = {};
    }
    return 0;
}

void FsServer::reapDescriptors() {
    {
        std::lock_guard<std::mutex> guard(descriptorLock_);
        for (auto& entry : descriptors_) {
            if (!entry.description) continue;
            if (entry.mappingClaimed) continue;
            os::wire::TaskInfo info{};
            const int64_t result =
                os::detail::TaskQuery().query(entry.ownerPid, info);
            if (result ? result != static_cast<int>(os::ErrorCode::NotFound)
                       : info.state != 2)
                continue;
            OpenFileDescription* description = entry.description;
            entry                            = {};
            if (description->referenceCount) --description->referenceCount;
            if (!description->referenceCount && !description->filesystem)
                *description = {};
        }
    }
    collectClosedDescriptions();
}

void FsServer::collectClosedDescriptions() {
    // Never acquire a filesystem lock while holding descriptorLock_.
    for (uint32_t index = 0; index < mountCapacity; ++index) {
        const auto backend = static_cast<PathBackend>(index);
        std::lock_guard<std::mutex> filesystem(fileSystemLock(backend));
        std::lock_guard<std::mutex> descriptors(descriptorLock_);
        for (auto& description : openDescriptions_) {
            if (!description.filesystem || description.referenceCount ||
                description.mountIndex != backend)
                continue;
            if (description.filesystem->releaseOpenFile(description.inode))
                description = {};
        }
    }
}

void FsServer::descriptorStat(uint64_t ownerPid, uint64_t descriptor,
                              os::IpcMessage& reply) {
    OpenFileDescription* entry = acquireDescription(ownerPid, descriptor);
    if (!entry) {
        reply.data[0] = 1;
        return;
    }
    std::lock_guard<std::mutex> guard(descriptionMutex(entry));
    if (entry->filesystem && entry->path[0]) {
        std::lock_guard<std::mutex> filesystem(
            fileSystemLock(entry->mountIndex));
        vfs::FileStatus status{};
        if (entry->filesystem->statInode(entry->inode, status) ||
            (entry->filesystem->lookup(
                 relativePath(entry->path, entry->mountIndex), status) &&
             status.inode == entry->inode)) {
            entry->mode             = status.mode;
            entry->uid              = status.uid;
            entry->gid              = status.gid;
            entry->size             = status.size;
            entry->links            = status.links;
            entry->accessTime       = status.accessTime;
            entry->modificationTime = status.modificationTime;
            entry->changeTime       = status.changeTime;
        }
    }
    uint32_t mode = entry->mode;
    if (!mode)
        mode = entry->backend == DescriptorBackend::BlockDevice  ? 0060660
               : entry->backend == DescriptorBackend::SystemFile ? 0100444
                                                                 : 0020666;
    setStat(reply,
            (mode & 0170000) == 0040000 ? os::File::Type::Directory
                                        : os::File::Type::File,
            entry->size, mode);
    reply.data[4]  = entry->inode;
    reply.data[6]  = entry->uid;
    reply.data[7]  = entry->gid;
    reply.data[5]  = entry->filesystem ? entry->links
                     : entry->links    ? entry->links
                                       : 1;
    reply.data[8]  = entry->accessTime;
    reply.data[9]  = entry->modificationTime;
    reply.data[10] = entry->changeTime;
    if (entry->mountIndex != PathBackend::Invalid) {
        const auto* device = catalog_.get(
            mountTable_[static_cast<uint32_t>(entry->mountIndex)].deviceIndex);
        if (device)
            reply.data[11] = os::Storage::Number(device->identity.major,
                                                 device->identity.minor)
                                 .getValue();
    }
    releaseDescription(entry);
}

void FsServer::transferAt(const os::IpcMessage& request, os::IpcMessage& reply,
                          bool writing) {
    OpenFileDescription* entry =
        acquireDescription(request.senderPid, request.data[0]);
    if (!entry) {
        reply.data[0] = static_cast<uint64_t>(-2LL);
        return;
    }
    std::lock_guard<std::mutex> guard(descriptionMutex(entry));
    const uint64_t size = request.data[2], offset = request.data[1];
    const uint64_t maximum = (os::ipcDataWordCount - 3) * 8;
    int64_t result         = -11;
    if (!writing && size <= maximum &&
        entry->backend == DescriptorBackend::ProcessFile) {
        result = offset >= entry->size
                     ? 0
                     : static_cast<int64_t>(entry->size - offset < size
                                                ? entry->size - offset
                                                : size);
        if (result)
            os::Runtime::copyBytes(&reply.data[1], entry->snapshot + offset,
                                   result);
    } else if (!writing && size <= maximum &&
               entry->backend == DescriptorBackend::SystemFile) {
        result =
            vfs::SystemFiles::read(entry->inode, offset, &reply.data[1], size);
    }
    if (entry->filesystem && size <= maximum &&
        (!writing || request.length >= 3 + (size + 7) / 8) &&
        (!writing || !readOnly(entry->mountIndex))) {
        const vfs::FileStatus status{entry->inode,
                                     entry->mode,
                                     entry->size,
                                     entry->uid,
                                     entry->gid,
                                     1,
                                     0,
                                     0,
                                     0,
                                     entry->privateData};
        std::lock_guard<std::mutex> filesystem(
            fileSystemLock(entry->mountIndex));
        result = writing ? entry->filesystem->write(status, offset,
                                                    &request.data[3], size)
                         : entry->filesystem->read(status, offset,
                                                   &reply.data[1], size);
        if (writing && result > 0 && offset + result > entry->size)
            entry->size = offset + result;
    }
    reply.data[0] = static_cast<uint64_t>(result);
    reply.length  = 1 + (!writing && result > 0 ? (result + 7) / 8 : 0);
    releaseDescription(entry);
}

void FsServer::cloneDescriptors(uint64_t parentPid, uint64_t childPid,
                                os::IpcMessage& reply) {
    std::lock_guard<std::mutex> guard(descriptorLock_);
    uint32_t sourceIndices[descriptorCapacity]{};
    uint32_t sourceCount = 0;
    uint32_t available   = 0;
    for (uint32_t index = 0; index < descriptorCapacity; ++index) {
        if (descriptors_[index].description == nullptr) {
            ++available;
        } else if (descriptors_[index].ownerPid == parentPid) {
            sourceIndices[sourceCount++] = index;
        }
    }
    reply.data[0] = 1;
    reply.data[1] = 0;
    reply.length  = 2;
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
        created[createdCount++]    = destination;
        reply.data[2 + source * 2] = sourceIndices[source] + 1;
        reply.data[3 + source * 2] = destination + 1;
    }
    reply.data[0] = 0;
    reply.data[1] = sourceCount;
    reply.length  = 2 + sourceCount * 2;
}

void FsServer::read(uint64_t ownerPid, uint64_t descriptor, uint64_t requested,
                    os::IpcMessage& reply) {
    OpenFileDescription* entry = acquireDescription(ownerPid, descriptor);
    if (entry == nullptr) {
        reply.data[0] = static_cast<uint64_t>(-2LL);
        return;
    }
    const uint64_t capacity = (os::ipcDataWordCount - 1) * sizeof(uint64_t);
    if (requested > capacity) requested = capacity;
    int64_t count = -1;
    std::lock_guard<std::mutex> descriptionGuard(descriptionMutex(entry));
    if (entry->filesystem != nullptr) {
        const vfs::FileStatus status{
            entry->inode,      entry->mode, entry->size, 0, 0, 0, 0, 0, 0,
            entry->privateData};
        std::lock_guard<std::mutex> filesystemGuard(
            fileSystemLock(entry->mountIndex));
        count = entry->filesystem->read(status, entry->offset, &reply.data[1],
                                        requested);
    } else if (entry->backend == DescriptorBackend::SystemFile) {
        count = vfs::SystemFiles::read(entry->inode, entry->offset,
                                       &reply.data[1], requested);
    } else if (entry->backend == DescriptorBackend::ProcessFile) {
        count =
            entry->offset >= entry->size
                ? 0
                : static_cast<int64_t>(entry->size - entry->offset < requested
                                           ? entry->size - entry->offset
                                           : requested);
        if (count)
            os::Runtime::copyBytes(&reply.data[1],
                                   entry->snapshot + entry->offset, count);
    } else if (entry->backend == DescriptorBackend::BlockDevice) {
        count = readDevice(static_cast<uint32_t>(entry->privateData),
                           entry->offset, &reply.data[1], requested);
    } else if (entry->backend == DescriptorBackend::Zero) {
        os::Runtime::clearMemory(&reply.data[1],
                                 static_cast<size_t>(requested));
        count = static_cast<int64_t>(requested);
    } else if (entry->backend == DescriptorBackend::Null) {
        count = 0;
    }
    reply.data[0] =
        count < 0 ? static_cast<uint64_t>(-11LL) : static_cast<uint64_t>(count);
    if (count < 0) {
        reply.length = 1;
        releaseDescription(entry);
        return;
    }
    entry->offset += static_cast<uint64_t>(count);
    reply.length = static_cast<uint32_t>(
        1 + (reply.data[0] + sizeof(uint64_t) - 1) / sizeof(uint64_t));
    releaseDescription(entry);
}

int64_t FsServer::readDevice(uint32_t index, uint64_t offset, void* buffer,
                             size_t size) {
    if (index >= catalog_.count()) return -1;
    BlockDeviceClient& device = devices_[index];
    const uint64_t total      = device.sectorCount() * 512;
    if (offset >= total) return offset == total ? 0 : -1;
    if (size > total - offset) size = static_cast<size_t>(total - offset);
    size_t copied = 0;
    uint8_t sector[512];
    while (copied < size) {
        const uint64_t position = offset + copied;
        const uint32_t within   = static_cast<uint32_t>(position % 512);
        size_t chunk            = 512 - within;
        if (chunk > size - copied) chunk = size - copied;
        if (!device.readSector(position / 512, sector)) return -1;
        os::Runtime::copyBytes(static_cast<uint8_t*>(buffer) + copied,
                               sector + within, chunk);
        copied += chunk;
    }
    return static_cast<int64_t>(copied);
}

void FsServer::write(uint64_t ownerPid, const os::IpcMessage& request,
                     os::IpcMessage& reply) {
    OpenFileDescription* entry = acquireDescription(ownerPid, request.data[0]);
    if (entry != nullptr && entry->backend == DescriptorBackend::Null) {
        const uint64_t capacity = (os::ipcDataWordCount - 2) * sizeof(uint64_t);
        reply.data[0] = request.data[1] < capacity ? request.data[1] : capacity;
        releaseDescription(entry);
        return;
    }
    if (entry == nullptr || entry->filesystem == nullptr ||
        readOnly(entry->mountIndex)) {
        reply.data[0] = 0;
        if (entry != nullptr) releaseDescription(entry);
        return;
    }
    uint64_t size           = request.data[1];
    const uint64_t capacity = (os::ipcDataWordCount - 2) * sizeof(uint64_t);
    if (size > capacity) size = capacity;
    int64_t count;
    {
        std::lock_guard<std::mutex> descriptionGuard(descriptionMutex(entry));
        const vfs::FileStatus status{
            entry->inode,      entry->mode, entry->size, 0, 0, 0, 0, 0, 0,
            entry->privateData};
        std::lock_guard<std::mutex> filesystemGuard(
            fileSystemLock(entry->mountIndex));
        count         = entry->filesystem->write(status, entry->offset,
                                                 &request.data[2], size);
        reply.data[0] = count < 0 ? static_cast<uint64_t>(-11LL)
                                  : static_cast<uint64_t>(count);
        if (count < 0) {
            releaseDescription(entry);
            return;
        }
        entry->offset += reply.data[0];
        if (entry->offset > entry->size) entry->size = entry->offset;
    }
    releaseDescription(entry);
}

void FsServer::seek(uint64_t ownerPid, uint64_t descriptor, int64_t offset,
                    uint64_t whence, os::IpcMessage& reply) {
    OpenFileDescription* entry = acquireDescription(ownerPid, descriptor);
    if (entry == nullptr || whence > 2) {
        reply.data[0] = 1;
        if (entry != nullptr) releaseDescription(entry);
        return;
    }
    uint64_t position = 0;
    bool valid        = true;
    {
        std::lock_guard<std::mutex> g(descriptionMutex(entry));
        uint64_t base = 0;
        if (whence == 1) base = entry->offset;
        if (whence == 2) base = entry->size;
        position = base;
        if (offset < 0) {
            const uint64_t magnitude = static_cast<uint64_t>(-(offset + 1)) + 1;
            if (magnitude > base) {
                valid = false;
            } else {
                position -= magnitude;
            }
        } else {
            const uint64_t distance = static_cast<uint64_t>(offset);
            if (distance > UINT64_MAX - base) {
                valid = false;
            } else {
                position += distance;
            }
        }
        if (valid) entry->offset = position;
    }
    reply.data[0] = valid ? 0 : 1;
    reply.data[1] = position;
    reply.length  = 2;
    releaseDescription(entry);
}

void FsServer::stat(const char* path, os::IpcMessage& reply, uint64_t caller) {
    uint64_t object       = 0;
    const auto objectKind = ProcessObjectPath::parse(path, caller, object);
    if (objectKind == ProcessObjectPath::Denied) {
        reply.data[0] = 1;
        return;
    }
    if (objectKind == ProcessObjectPath::File) {
        descriptorStat(caller, object, reply);
        return;
    }
    if (PathText::startsWithPath(path, "/proc")) {
        vfs::FileStatus status{};
        if (!vfs::ProcessFiles::lookup(path, caller, status)) {
            reply.data[0] = 1;
            return;
        }
        setStat(reply,
                (status.mode & 0xf000) == vfs::modeDirectory
                    ? os::File::Type::Directory
                    : os::File::Type::File,
                status.size, status.mode);
        reply.data[4]  = status.inode;
        reply.data[5]  = status.links;
        reply.data[6]  = status.uid;
        reply.data[7]  = status.gid;
        reply.data[11] = os::Storage::Number(0, 3).getValue();
        return;
    }
    if (PathText::startsWithPath(path, "/sys")) {
        vfs::FileStatus status{};
        if (!vfs::SystemFiles::lookup(path, status)) {
            reply.data[0] = 1;
            return;
        }
        setStat(reply,
                (status.mode & 0xf000) == vfs::modeDirectory
                    ? os::File::Type::Directory
                    : os::File::Type::File,
                status.size, status.mode);
        reply.data[4]  = status.inode;
        reply.data[5]  = status.links;
        reply.data[11] = os::Storage::Number(0, 2).getValue();
        return;
    }
    if (PathText::startsWithPath(path, "/dev")) {
        vfs::DirectoryEntry probe{};
        if (deviceDirectory(path, 0, probe) >= 0) {
            setStat(reply, os::File::Type::Directory, 0, 0755);
            return;
        }
        const int index = catalog_.resolve(path);
        if (index >= 0) {
            const DeviceRecord& device =
                *catalog_.get(static_cast<uint32_t>(index));
            setStat(reply, os::File::Type::File,
                    devices_[index].sectorCount() * 512, 0x6000U | 0660);
            reply.data[4] = os::Storage::Number(device.identity.major,
                                                device.identity.minor)
                                .getValue();
            reply.data[11] = os::Storage::Number(0, 1).getValue();
            reply.data[12] = reply.data[4];
            reply.length   = 13;
            return;
        }
        const char* node = path + 5;
        const bool tty   = node[0] == 't' && node[1] == 't' && node[2] == 'y' &&
                         node[3] >= '1' && node[3] <= '7' && node[4] == '\0';
        if (os::Runtime::stringsEqual(path, "/dev/zero") ||
            os::Runtime::stringsEqual(path, "/dev/null") ||
            os::Runtime::stringsEqual(path, "/dev/ptmx") || tty) {
            setStat(reply, os::File::Type::File, 0, 0x2000U | 0666);
            reply.data[12] =
                tty ? os::Storage::Number(4, node[3] - '0').getValue()
                : os::Runtime::stringsEqual(path, "/dev/ptmx")
                    ? os::Storage::Number(5, 2).getValue()
                    : os::Storage::Number(
                          1,
                          os::Runtime::stringsEqual(path, "/dev/null") ? 3 : 5)
                          .getValue();
            return;
        }
        reply.data[0] = 1;
        return;
    }
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr) {
        reply.data[0] = 1;
        return;
    }
    vfs::FileStatus status{};
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    if (!mounted_[static_cast<uint32_t>(backend)] ||
        !filesystem->lookup(relativePath(path, backend), status)) {
        reply.data[0] = 1;
        return;
    }
    const bool directory       = (status.mode & 0xf000U) == vfs::modeDirectory;
    const uint32_t permissions = status.mode & 07777U;
    setStat(reply, directory ? os::File::Type::Directory : os::File::Type::File,
            status.size, (status.mode & 0xf000U) | permissions);
    reply.data[4]           = status.inode;
    reply.data[5]           = status.links;
    reply.data[6]           = status.uid;
    reply.data[7]           = status.gid;
    reply.data[8]           = status.accessTime;
    reply.data[9]           = status.modificationTime;
    reply.data[10]          = status.changeTime;
    const MountEntry& mount = mountTable_[static_cast<uint32_t>(backend)];
    const auto& identity    = catalog_.get(mount.deviceIndex)->identity;
    reply.data[11] =
        os::Storage::Number(identity.major, identity.minor).getValue();
    reply.data[12] = 0;
    reply.length   = 13;
}

uint64_t FsServer::truncate(const char* path) {
    if (os::Runtime::stringsEqual(path, "/dev/null")) return 0;
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || readOnly(backend)) return 1;
    vfs::FileStatus status{};
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    return filesystem->lookup(relativePath(path, backend), status) &&
                   (status.mode & 0xf000U) == vfs::modeRegular &&
                   filesystem->truncate(status)
               ? 0
               : 1;
}

FileDescriptor* FsServer::getDescriptorRecord(uint64_t ownerPid,
                                              uint64_t descriptor) {
    if (descriptor == 0 || descriptor > descriptorCapacity) return nullptr;
    FileDescriptor& entry = descriptors_[descriptor - 1];
    if (entry.description && entry.mappingClaimed) {
        os::wire::TaskInfo caller{};
        return !os::detail::TaskQuery().query(ownerPid, caller) &&
                       caller.managedService &&
                       caller.serviceRole ==
                           static_cast<uint32_t>(os::ServiceRole::Memory)
                   ? &entry
                   : nullptr;
    }
    return entry.description != nullptr && entry.ownerPid == ownerPid ? &entry
                                                                      : nullptr;
}

OpenFileDescription* FsServer::acquireDescription(uint64_t ownerPid,
                                                  uint64_t descriptor) {
    std::lock_guard<std::mutex> guard(descriptorLock_);
    FileDescriptor* entry = getDescriptorRecord(ownerPid, descriptor);
    if (entry == nullptr || entry->description == nullptr) return nullptr;
    ++entry->description->referenceCount;
    return entry->description;
}

void FsServer::releaseDescription(OpenFileDescription* description) {
    std::lock_guard<std::mutex> guard(descriptorLock_);
    if (description->referenceCount != 0) --description->referenceCount;
    if (description->referenceCount == 0 && !description->filesystem)
        *description = {};
}

std::mutex& FsServer::descriptionMutex(const OpenFileDescription* description) {
    return descriptionLocks_[description - openDescriptions_];
}

bool FsServer::create(const char* path, bool directory, uint16_t mode,
                      uint32_t uid, uint32_t gid) {
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || readOnly(backend)) return false;
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    const char* relative = relativePath(path, backend);
    if (uid != UINT32_MAX && (uid > UINT16_MAX || gid > UINT16_MAX))
        return false;
    if (!filesystem->create(relative, directory)) return false;
    if (uid != UINT32_MAX && (!filesystem->changeMode(relative, mode) ||
                              !filesystem->changeOwner(relative, uid, gid))) {
        filesystem->remove(relative);
        return false;
    }
    return true;
}

bool FsServer::remove(const char* path, bool directoryOnly, bool fileOnly) {
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || readOnly(backend)) return false;
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    const char* relative = relativePath(path, backend);
    if (directoryOnly || fileOnly) {
        vfs::FileStatus status{};
        if (!filesystem->lookup(relative, status) ||
            (directoryOnly && (status.mode & 0xf000) != vfs::modeDirectory) ||
            (fileOnly && (status.mode & 0xf000) == vfs::modeDirectory))
            return false;
    }
    return filesystem->remove(relative);
}

bool FsServer::link(const char* existing, const char* newPath) {
    const PathBackend backend   = pathBackend(existing);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || pathBackend(newPath) != backend ||
        readOnly(backend)) {
        return false;
    }
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    return filesystem->link(relativePath(existing, backend),
                            relativePath(newPath, backend));
}

bool FsServer::changeMode(const char* path, uint16_t mode) {
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || readOnly(backend)) return false;
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    return filesystem->changeMode(relativePath(path, backend), mode);
}

bool FsServer::rename(const char* existing, const char* newPath,
                      bool directoryOnly) {
    const PathBackend backend = pathBackend(existing);
    auto* filesystem          = fileSystem(backend);
    if (!filesystem || pathBackend(newPath) != backend || readOnly(backend) ||
        PathText::startsWithPath(existing, "/dev") ||
        PathText::startsWithPath(newPath, "/dev") ||
        PathText::startsWithPath(existing, "/sys") ||
        PathText::startsWithPath(newPath, "/sys") ||
        PathText::startsWithPath(existing, "/proc") ||
        PathText::startsWithPath(newPath, "/proc"))
        return false;
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    std::lock_guard<std::mutex> descriptions(descriptorLock_);
    vfs::FileStatus before{}, after{};
    if (!filesystem->lookup(relativePath(existing, backend), before) ||
        (directoryOnly && (before.mode & 0xf000) != vfs::modeDirectory))
        return false;
    const size_t oldLength = os::Runtime::stringLength(existing),
                 newLength = os::Runtime::stringLength(newPath);
    for (const auto& description : openDescriptions_)
        if (description.referenceCount &&
            description.filesystem == filesystem &&
            PathText::startsWithPath(description.path, existing)) {
            const size_t suffix =
                os::Runtime::stringLength(description.path) - oldLength;
            if (newLength + suffix >= sizeof(description.path)) return false;
        }
    if (!filesystem->rename(relativePath(existing, backend),
                            relativePath(newPath, backend)) ||
        !filesystem->lookup(relativePath(newPath, backend), after))
        return false;
    for (auto& description : openDescriptions_) {
        if (!description.referenceCount || description.filesystem != filesystem)
            continue;
        if (description.inode == before.inode &&
            description.privateData == before.privateData) {
            description.inode       = after.inode;
            description.privateData = after.privateData;
        }
        if (PathText::startsWithPath(description.path, existing)) {
            char updated[384];
            os::Runtime::copyString(updated, newPath, sizeof(updated));
            os::Runtime::copyString(updated + newLength,
                                    description.path + oldLength,
                                    sizeof(updated) - newLength);
            os::Runtime::copyString(description.path, updated,
                                    sizeof(description.path));
        }
    }
    return true;
}

bool FsServer::changeOwner(const char* path, uint16_t uid, uint16_t gid) {
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || readOnly(backend)) return false;
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    return filesystem->changeOwner(relativePath(path, backend), uid, gid);
}

bool FsServer::changeTimes(const char* path, uint32_t accessTime,
                           uint32_t modificationTime) {
    const PathBackend backend   = pathBackend(path);
    vfs::FileSystem* filesystem = fileSystem(backend);
    if (filesystem == nullptr || readOnly(backend)) return false;
    std::lock_guard<std::mutex> guard(fileSystemLock(backend));
    return filesystem->changeTimes(relativePath(path, backend), accessTime,
                                   modificationTime);
}

vfs::FileSystem* FsServer::fileSystem(PathBackend backend) const {
    const uint32_t index = static_cast<uint32_t>(backend);
    return index < mountCapacity ? fileSystems_[index] : nullptr;
}
std::mutex& FsServer::fileSystemLock(PathBackend backend) {
    return fileSystemLocks_[static_cast<uint32_t>(backend)];
}
BlockDeviceClient* FsServer::blockDevice(PathBackend backend) {
    const MountEntry* entry = mountEntry(backend);
    return entry && entry->deviceIndex < catalog_.count()
               ? &devices_[entry->deviceIndex]
               : nullptr;
}
bool FsServer::readOnly(PathBackend backend) const {
    const MountEntry* entry           = mountEntry(backend);
    const vfs::FileSystem* filesystem = fileSystem(backend);
    return !entry || !filesystem || !mounted_[static_cast<uint32_t>(backend)] ||
           entry->spec.readOnly || devices_[entry->deviceIndex].readOnly() ||
           filesystem->readOnly();
}

void FsServer::setStat(os::IpcMessage& reply, os::File::Type type,
                       uint64_t size, uint32_t mode) {
    reply.data[0] = 0;
    reply.data[1] = static_cast<uint64_t>(type);
    reply.data[2] = size;
    reply.data[3] = mode;
    for (uint32_t index = 4; index <= 12; ++index) reply.data[index] = 0;
    reply.data[5]  = type == os::File::Type::Directory ? 2 : 1;
    reply.data[11] = os::Storage::Number(0, 1).getValue();
    reply.length   = 13;
}

}  // namespace vfs::server
