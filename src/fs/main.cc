#include <cerrno>
#include <fcntl.h>
#include <new>

#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/server.h"
#include <os/task.h>

namespace vfs::server {

bool FsServer::handle(const os::IpcMessage& original, os::IpcMessage& reply) {
    os::IpcMessage request = original;
    const auto operation   = static_cast<os::FsRequest>(request.type);
    if (operation == os::FsRequest::Fchmod ||
        operation == os::FsRequest::Fchown ||
        operation == os::FsRequest::Ftimes) {
        reply.length = 1;
        if (request.length != 3 ||
            (operation == os::FsRequest::Fchmod && request.data[1] > 07777) ||
            (operation == os::FsRequest::Fchown &&
             (request.data[1] >= 65535 || request.data[2] >= 65535)) ||
            (operation == os::FsRequest::Ftimes &&
             (request.data[1] > UINT32_MAX || request.data[2] > UINT32_MAX))) {
            reply.data[0] =
                static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            return true;
        }
        os::IpcMessage pathRequest{};
        pathRequest.senderPid = request.senderPid;
        pathRequest.senderTid = request.senderTid;
        {
            std::lock_guard<std::mutex> guard(descriptorLock_);
            auto* entry =
                getDescriptorRecord(request.senderPid, request.data[0]);
            if (!entry || !entry->description ||
                entry->description->backend != DescriptorBackend::Filesystem) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidHandle);
                return true;
            }
            os::Runtime::copyString(reinterpret_cast<char*>(pathRequest.data),
                                    entry->description->path, 384);
        }
        pathRequest.type = static_cast<uint64_t>(
            operation == os::FsRequest::Fchmod   ? os::FsRequest::ChangeMode
            : operation == os::FsRequest::Fchown ? os::FsRequest::ChangeOwner
                                                 : os::FsRequest::ChangeTimes);
        pathRequest.length   = operation == os::FsRequest::Fchmod ? 49 : 50;
        pathRequest.data[48] = request.data[1];
        pathRequest.data[49] = request.data[2];
        const bool handled   = handle(pathRequest, reply);
        if (reply.data[0] == 1)
            reply.data[0] = static_cast<int64_t>(os::ErrorCode::IoError);
        return handled;
    }
    switch (operation) {
        case os::FsRequest::Open:
        case os::FsRequest::Stat:
        case os::FsRequest::Create:
        case os::FsRequest::Remove:
        case os::FsRequest::RemoveFile:
        case os::FsRequest::MakeDirectory:
        case os::FsRequest::ReadDirectory:
        case os::FsRequest::ReadDirectoryBatch:
        case os::FsRequest::Truncate:
        case os::FsRequest::ChangeMode:
        case os::FsRequest::ChangeOwner:
        case os::FsRequest::ChangeTimes:
        case os::FsRequest::Link:
        case os::FsRequest::Rename:
        case os::FsRequest::DirOpen:
        case os::FsRequest::DirCreate:
        case os::FsRequest::DirRemove:
        case os::FsRequest::DirRename:
            if (!PathText::collapseSlashes(
                    reinterpret_cast<char*>(request.data), 384) ||
                ((operation == os::FsRequest::Link ||
                  operation == os::FsRequest::Rename ||
                  operation == os::FsRequest::DirRename) &&
                 !PathText::collapseSlashes(
                     reinterpret_cast<char*>(request.data) + 384, 384))) {
                reply.length = 1;
                reply.data[0] =
                    (operation == os::FsRequest::RemoveFile ||
                     operation == os::FsRequest::DirOpen ||
                     operation == os::FsRequest::DirCreate ||
                     operation == os::FsRequest::DirRemove ||
                     operation == os::FsRequest::DirRename)
                        ? static_cast<int64_t>(os::ErrorCode::InvalidArgument)
                        : 1;
                return true;
            }
            break;
        default:
            break;
    }
    if (!permitted(request)) {
        if ((operation == os::FsRequest::ReadAtAsync ||
             operation == os::FsRequest::WriteAtAsync) &&
            request.handleCount == 1) {
            os::IpcMessage completion{};
            completion.type    = os::fsCompletionNotification;
            completion.data[0] = request.data[3];
            completion.data[1] = static_cast<uint64_t>(-3LL);
            completion.length  = 3;
            os::sendMessage(request.handles[0], completion);
            os::closeHandle(request.handles[0]);
            return false;
        }
        reply.data[0] = operation == os::FsRequest::Open ? 0
                        : operation == os::FsRequest::Read ||
                                operation == os::FsRequest::Write ||
                                operation == os::FsRequest::ReadAt ||
                                operation == os::FsRequest::WriteAt
                            ? static_cast<uint64_t>(-3LL)
                            : 1;
        if (operation == os::FsRequest::Ftruncate ||
            operation == os::FsRequest::RemoveFile ||
            operation == os::FsRequest::DirOpen ||
            operation == os::FsRequest::DirClose ||
            operation == os::FsRequest::DirRead ||
            operation == os::FsRequest::DirCreate ||
            operation == os::FsRequest::DirRemove ||
            operation == os::FsRequest::DirRename)
            reply.data[0] =
                static_cast<int64_t>(os::ErrorCode::PermissionDenied);
        reply.length = operation == os::FsRequest::Open ? 2 : 1;
        if (operation == os::FsRequest::Open)
            reply.data[1] = static_cast<uint64_t>(-EACCES);
        return true;
    }
    if (operation == os::FsRequest::ReadAtAsync ||
        operation == os::FsRequest::WriteAtAsync) {
        handleAsync(request, operation == os::FsRequest::WriteAtAsync);
        return false;
    }
    const char* path = reinterpret_cast<const char*>(request.data);
    switch (operation) {
        case os::FsRequest::DirOpen: {
            if (request.length != 48) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            }
            os::IpcMessage info{};
            stat(path, info, request.senderPid);
            if (info.data[0]) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::NotFound);
                break;
            }
            if (info.data[1] !=
                static_cast<uint64_t>(os::File::Type::Directory)) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            }
            const uint64_t descriptor = open(
                request.senderPid, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            reply.data[0] =
                descriptor ? 0 : static_cast<int64_t>(os::ErrorCode::IoError);
            reply.data[1] = descriptor;
            reply.length  = 2;
            break;
        }
        case os::FsRequest::DirClose:
            reply.data[0] =
                request.length == 1
                    ? (close(request.senderPid, request.data[0])
                           ? static_cast<int64_t>(os::ErrorCode::InvalidHandle)
                           : 0)
                    : static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            break;
        case os::FsRequest::DirRead:
            if (request.length != 3 || !request.data[2] ||
                request.data[2] > 14) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            }
            readOpenedDirectory(request.senderPid, request.data[0],
                                request.data[1], request.data[2], reply);
            break;
        case os::FsRequest::DirCreate: {
            os::wire::TaskInfo caller{};
            if (request.length != 49 || request.data[48] > 07777 ||
                os::detail::TaskQuery().query(request.senderPid, caller)) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            }
            reply.data[0] = create(path, true, request.data[48],
                                   caller.effectiveUid, caller.effectiveGid)
                                ? 0
                                : static_cast<int64_t>(os::ErrorCode::IoError);
            break;
        }
        case os::FsRequest::DirRemove:
            reply.data[0] =
                request.length == 48
                    ? (remove(path, true)
                           ? 0
                           : static_cast<int64_t>(os::ErrorCode::IoError))
                    : static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            break;
        case os::FsRequest::DirRename:
            reply.data[0] =
                request.length == 96
                    ? (rename(path, path + 384, true)
                           ? 0
                           : static_cast<int64_t>(os::ErrorCode::IoError))
                    : static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            break;
        case os::FsRequest::Open: {
            const int flags = request.length >= 49
                                  ? static_cast<int>(request.data[48])
                                  : O_RDONLY;
            const uint16_t mode =
                request.length >= 50 ? request.data[49] & 07777 : 0666;
            os::IpcMessage status{};
            stat(path, status, request.senderPid);
            const bool exists = status.data[0] == 0;
            reply.length      = 2;
            if ((flags & O_ACCMODE) > O_RDWR) {
                reply.data[0] = 0;
                reply.data[1] = static_cast<uint64_t>(-EINVAL);
                break;
            }
            if ((flags & O_ACCMODE) > O_RDWR ||
                (!exists && !(flags & O_CREAT)) ||
                (exists &&
                 (flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) ||
                (exists && (flags & O_DIRECTORY) &&
                 status.data[1] !=
                     static_cast<uint64_t>(os::File::Type::Directory))) {
                reply.data[0] = 0;
                reply.data[1] =
                    static_cast<uint64_t>(!exists                 ? -ENOENT
                                          : (flags & O_DIRECTORY) ? -ENOTDIR
                                                                  : -EEXIST);
                break;
            }
            if (!exists) {
                if (!create(path, false)) {
                    reply.data[0] = 0;
                    reply.data[1] = static_cast<uint64_t>(-EEXIST);
                    break;
                }
                os::wire::TaskInfo caller{};
                if (!os::detail::TaskQuery().query(request.senderPid, caller))
                    changeOwner(path, caller.effectiveUid, caller.effectiveGid);
                changeMode(path, mode);
            }
            if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY &&
                truncate(path)) {
                reply.data[0] = 0;
                reply.data[1] = static_cast<uint64_t>(-EIO);
                break;
            }
            reply.data[0] = open(request.senderPid, path, flags);
            reply.data[1] = reply.data[0] ? 0 : static_cast<uint64_t>(-EIO);
            break;
        }
        case os::FsRequest::Close:
            reply.data[0] = close(request.senderPid, request.data[0]);
            break;
        case os::FsRequest::Sync: {
            auto* entry =
                acquireDescription(request.senderPid, request.data[0]);
            int result = -EBADF;
            if (entry) {
                result = -EINVAL;
                if (entry->filesystem) {
                    std::lock_guard<std::mutex> guard(
                        fileSystemLock(entry->mountIndex));
                    result = entry->filesystem->sync() ? 0 : -EIO;
                }
                releaseDescription(entry);
            }
            reply.data[0] = static_cast<int64_t>(result);
            reply.length  = 1;
            break;
        }
        case os::FsRequest::Read:
            read(request.senderPid, request.data[0], request.data[1], reply);
            break;
        case os::FsRequest::Write:
            write(request.senderPid, request, reply);
            break;
        case os::FsRequest::Seek:
            seek(request.senderPid, request.data[0],
                 static_cast<int64_t>(request.data[1]), request.data[2], reply);
            break;
        case os::FsRequest::Stat:
            stat(path, reply, request.senderPid);
            break;
        case os::FsRequest::Create:
        case os::FsRequest::MakeDirectory:
            reply.data[0] =
                create(path, operation == os::FsRequest::MakeDirectory) ? 0 : 1;
            if (!reply.data[0]) {
                os::wire::TaskInfo caller{};
                if (!os::detail::TaskQuery().query(request.senderPid, caller))
                    changeOwner(path, caller.effectiveUid, caller.effectiveGid);
            }
            break;
        case os::FsRequest::Ftruncate: {
            reply.data[0] =
                static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            if (request.length != 2 || request.data[1]) break;
            OpenFileDescription* entry = nullptr;
            {
                std::lock_guard<std::mutex> descriptors(descriptorLock_);
                auto* descriptor =
                    getDescriptorRecord(request.senderPid, request.data[0]);
                if (descriptor && (descriptor->flags & O_ACCMODE) != O_RDONLY) {
                    entry = descriptor->description;
                    ++entry->referenceCount;
                }
            }
            if (!entry) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidHandle);
                break;
            }
            {
                std::lock_guard<std::mutex> guard(descriptionMutex(entry));
                if (entry->filesystem &&
                    (entry->mode & 0xf000) == vfs::modeRegular &&
                    !readOnly(entry->mountIndex)) {
                    std::lock_guard<std::mutex> filesystem(
                        fileSystemLock(entry->mountIndex));
                    const vfs::FileStatus status{
                        entry->inode,      entry->mode,
                        entry->size,       entry->uid,
                        entry->gid,        entry->links,
                        entry->accessTime, entry->modificationTime,
                        entry->changeTime, entry->privateData};
                    const bool success = entry->filesystem->truncate(status);
                    reply.data[0] =
                        success ? 0
                                : static_cast<int64_t>(os::ErrorCode::IoError);
                    if (success) entry->size = 0;
                }
            }
            releaseDescription(entry);
            break;
        }
        case os::FsRequest::RemoveFile:
            reply.data[0] = remove(path, false, true)
                                ? 0
                                : static_cast<int64_t>(os::ErrorCode::IoError);
            break;
        case os::FsRequest::Remove:
            reply.data[0] = remove(path) ? 0 : 1;
            break;
        case os::FsRequest::ReadDirectory:
            readDirectory(path, request.data[48], reply, request.senderPid);
            break;
        case os::FsRequest::ReadDirectoryBatch:
            readDirectoryBatch(path, request.data[48], request.data[49], reply,
                               request.senderPid);
            break;
        case os::FsRequest::Truncate:
            reply.data[0] = truncate(path);
            break;
        case os::FsRequest::Link:
            reply.data[0] = link(path, path + 384) ? 0 : 1;
            break;
        case os::FsRequest::Rename:
            reply.data[0] = rename(path, path + 384) ? 0 : 1;
            break;
        case os::FsRequest::ChangeMode:
            reply.data[0] =
                changeMode(path, static_cast<uint16_t>(request.data[48])) ? 0
                                                                          : 1;
            break;
        case os::FsRequest::ChangeOwner:
            if (request.data[48] > 0xffffU || request.data[49] > 0xffffU) {
                reply.data[0] = 1;
            } else {
                reply.data[0] =
                    changeOwner(path, static_cast<uint16_t>(request.data[48]),
                                static_cast<uint16_t>(request.data[49]))
                        ? 0
                        : 1;
            }
            break;
        case os::FsRequest::ChangeTimes:
            if (request.data[48] > UINT32_MAX ||
                request.data[49] > UINT32_MAX) {
                reply.data[0] = 1;
            } else {
                reply.data[0] =
                    changeTimes(path, static_cast<uint32_t>(request.data[48]),
                                static_cast<uint32_t>(request.data[49]))
                        ? 0
                        : 1;
            }
            break;
        case os::FsRequest::CloneDescriptors:
            cloneDescriptors(request.data[0], request.senderPid, reply);
            break;
        case os::FsRequest::DelegateMapping:
            delegateMapping(request, reply);
            break;
        case os::FsRequest::ClaimMapping:
            claimMapping(request, reply);
            break;
        case os::FsRequest::ReleaseMapping:
            releaseMapping(request, reply);
            break;
        case os::FsRequest::ListMappings:
            listMappings(request, reply);
            break;
        case os::FsRequest::CancelMapping:
            cancelMapping(request, reply);
            break;
        case os::FsRequest::CloneForChild:
            cloneDescriptors(request.senderPid, request.data[0], reply);
            break;
        case os::FsRequest::Fstat:
            descriptorStat(request.senderPid, request.data[0], reply);
            break;
        case os::FsRequest::ReadAt:
        case os::FsRequest::WriteAt:
            transferAt(request, reply, operation == os::FsRequest::WriteAt);
            break;
        case os::FsRequest::Mount:
            reply.data[0] = mount(request) ? 0 : 1;
            if (!reply.data[0]) {
                std::lock_guard<std::mutex> state(mountStateLock_);
                const char* target =
                    reinterpret_cast<const char*>(request.data) + 64;
                for (uint32_t index = 0; index < mountCapacity; ++index)
                    if (mounted_[index] &&
                        os::Runtime::stringsEqual(
                            mountTable_[index].spec.target, target)) {
                        reply.data[1] =
                            readOnly(static_cast<PathBackend>(index));
                        reply.length = 2;
                        break;
                    }
            }
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

bool FsServer::format(const os::IpcMessage&) const {
    // Formatting mounted or unverified devices is not supported.
    return false;
}

OpenFileDescription* FsServer::getDescriptor(uint64_t ownerPid,
                                             uint64_t descriptor) {
    FileDescriptor* entry = getDescriptorRecord(ownerPid, descriptor);
    return entry == nullptr ? nullptr : entry->description;
}

}  // namespace vfs::server

namespace vfs::server {

class FsApplication {
   public:
    static void runFsWorker(void* self) {
        static_cast<FsApplication*>(self)->workerLoop();
    }
    FsServer fsServer;
    uint64_t fsEndpoint;

    [[noreturn]] void workerLoop(bool maintenance = false) {
        uint64_t nextMaintenance = 0;
        while (true) {
            const auto now = os::Sys().readMonotonicTime();
            if (maintenance && now >= nextMaintenance) {
                fsServer.reapDescriptors();
                nextMaintenance = os::Sys().readMonotonicTime() + 1000000000ULL;
            }
            os::IpcMessage request{};
            if (os::receiveMessage(fsEndpoint, request,
                                   maintenance ? nextMaintenance
                                               : os::ipcInfiniteDeadline) != 0)
                continue;
            os::IpcMessage reply{};
            if (fsServer.handle(request, reply)) {
                os::replyMessage(fsEndpoint, request.replyToken, reply);
            }
            fsServer.collectClosedDescriptions();
        }
    }

    int run() {
        os::Sys().log("[FS] FS Server started in Ring 3\n");
        const uint64_t registry = os::getBootstrapHandle();
        fsEndpoint              = os::detail::EndpointControl().create();
        if (registry == 0 || fsEndpoint == 0 ||
            !os::detail::RegistryClient(registry).registerName(
                os::fsServiceName, fsEndpoint)) {
            os::Sys().log("[FS] registration failed\n");
            return 1;
        }
        const uint64_t storage = os::detail::RegistryClient(registry).lookup(
            os::storageServiceName, 5000);
        const uint64_t device = os::detail::RegistryClient(registry).lookup(
            os::deviceServiceName, 5000);
        if (!fsServer.init(storage, device)) {
            os::Sys().log("[FS] fstab filesystem initialization failed\n");
            return 2;
        }
        if (os::mapMemory(workerStackAddress,
                          workerStackSize * workerThreadCount,
                          1) != workerStackAddress) {
            os::Sys().log("[FS] worker stack allocation failed\n");
            return 3;
        }
        for (uint32_t index = 0; index < workerThreadCount; ++index) {
            const uint64_t stackTop =
                workerStackAddress + workerStackSize * (index + 1);
            if (os::createThread(runFsWorker, stackTop, this) == 0) {
                os::Sys().log("[FS] unable to start request worker\n");
                return 4;
            }
        }
        os::Sys().log("[FS] storage IPC probe complete\n");
        os::Sys().log("[FS] mounted filesystems configured in /etc/fstab\n");
        workerLoop(true);
    }
};
}  // namespace vfs::server
int main(int, char**) {
    // The service is too large for its startup stack. Its storage has process
    // lifetime; explicitly destroy the object if startup returns with an error.
    alignas(vfs::server::FsApplication) static unsigned char
        storage[sizeof(vfs::server::FsApplication)];
    auto* application = new (storage) vfs::server::FsApplication;
    const int status  = application->run();
    application->~FsApplication();
    return status;
}
