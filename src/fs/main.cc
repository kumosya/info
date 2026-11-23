#include <fcntl.h>
#include <os/detail/client.h>
#include <os/runtime.h>
#include <os/task.h>

#include <cerrno>
#include <new>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/fs/server.h"

namespace fs::server {

    bool FsServer::handle(const os::IpcMessage& original,
                          os::IpcMessage& reply) {
        os::IpcMessage request = original;
        const auto operation = static_cast<services::FsRequest>(request.type);
        if (operation == services::FsRequest::SyncAll) {
            reply.length = 1;
            reply.data[0] = request.length == 0 && request.handleCount == 0
                                ? syncAll()
                                : -EINVAL;
            return true;
        }

        if (operation == services::FsRequest::ReadBuffer) {
            readBuffer(request, reply);
            return true;
        }

        if (operation == services::FsRequest::Fchmod ||
            operation == services::FsRequest::Fchown ||
            operation == services::FsRequest::Ftimes) {
            reply.length = 1;
            if (request.length != 3 ||
                (operation == services::FsRequest::Fchmod &&
                 request.data[1] > 07777) ||
                (operation == services::FsRequest::Fchown &&
                 (request.data[1] >= 65535 || request.data[2] >= 65535)) ||
                (operation == services::FsRequest::Ftimes &&
                 (request.data[1] > UINT32_MAX ||
                  request.data[2] > UINT32_MAX))) {
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
                    entry->description->backend !=
                        DescriptorBackend::Filesystem) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::InvalidHandle);
                    return true;
                }

                os::Runtime::copyString(
                    reinterpret_cast<char*>(pathRequest.data),
                    entry->description->path, 384);
            }

            pathRequest.type =
                static_cast<uint64_t>(operation == services::FsRequest::Fchmod
                                          ? services::FsRequest::ChangeMode
                                      : operation == services::FsRequest::Fchown
                                          ? services::FsRequest::ChangeOwner
                                          : services::FsRequest::ChangeTimes);
            pathRequest.length =
                operation == services::FsRequest::Fchmod ? 49 : 50;
            pathRequest.data[48] = request.data[1];
            pathRequest.data[49] = request.data[2];
            const bool handled = handle(pathRequest, reply);
            if (reply.data[0] == 1) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::IoError);
            }

            return handled;
        }

        switch (operation) {
            case services::FsRequest::Open:
            case services::FsRequest::Stat:
            case services::FsRequest::Create:
            case services::FsRequest::Remove:
            case services::FsRequest::RemoveFile:
            case services::FsRequest::MakeDirectory:
            case services::FsRequest::ReadDirectory:
            case services::FsRequest::ReadDirectoryBatch:
            case services::FsRequest::Truncate:
            case services::FsRequest::ChangeMode:
            case services::FsRequest::ChangeOwner:
            case services::FsRequest::ChangeTimes:
            case services::FsRequest::Link:
            case services::FsRequest::Rename:
            case services::FsRequest::DirOpen:
            case services::FsRequest::DirCreate:
            case services::FsRequest::DirRemove:
            case services::FsRequest::DirRename:
                if (!path_text::collapseSlashes(
                        reinterpret_cast<char*>(request.data), 384) ||
                    ((operation == services::FsRequest::Link ||
                      operation == services::FsRequest::Rename ||
                      operation == services::FsRequest::DirRename) &&
                     !path_text::collapseSlashes(
                         reinterpret_cast<char*>(request.data) + 384, 384))) {
                    reply.length = 1;
                    reply.data[0] =
                        (operation == services::FsRequest::RemoveFile ||
                         operation == services::FsRequest::DirOpen ||
                         operation == services::FsRequest::DirCreate ||
                         operation == services::FsRequest::DirRemove ||
                         operation == services::FsRequest::DirRename)
                            ? static_cast<int64_t>(
                                  os::ErrorCode::InvalidArgument)
                            : 1;
                    return true;
                }

                break;
            default:
                break;
        }

        if (!permitted(request)) {
            if ((operation == services::FsRequest::ReadAtAsync ||
                 operation == services::FsRequest::WriteAtAsync) &&
                request.handleCount == 1) {
                os::IpcMessage completion{};
                completion.type = services::fsCompletionNotification;
                completion.data[0] = request.data[3];
                completion.data[1] = static_cast<uint64_t>(-3LL);
                completion.length = 3;
                os::sendMessage(request.handles[0], completion);
                os::closeHandle(request.handles[0]);
                return false;
            }

            reply.data[0] = operation == services::FsRequest::Open ? 0
                            : operation == services::FsRequest::Read ||
                                    operation == services::FsRequest::Write ||
                                    operation == services::FsRequest::ReadAt ||
                                    operation == services::FsRequest::WriteAt
                                ? static_cast<uint64_t>(-3LL)
                                : 1;
            if (operation == services::FsRequest::Ftruncate ||
                operation == services::FsRequest::RemoveFile ||
                operation == services::FsRequest::DirOpen ||
                operation == services::FsRequest::DirClose ||
                operation == services::FsRequest::DirRead ||
                operation == services::FsRequest::DirCreate ||
                operation == services::FsRequest::DirRemove ||
                operation == services::FsRequest::DirRename) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::PermissionDenied);
            }

            reply.length = operation == services::FsRequest::Open ? 2 : 1;
            if (operation == services::FsRequest::Open) {
                reply.data[1] = static_cast<uint64_t>(-EACCES);
            }

            return true;
        }

        if (operation == services::FsRequest::ReadAtAsync ||
            operation == services::FsRequest::WriteAtAsync) {
            handleAsync(request,
                        operation == services::FsRequest::WriteAtAsync);
            return false;
        }

        const char* path = reinterpret_cast<const char*>(request.data);
        switch (operation) {
            case services::FsRequest::DirOpen: {
                if (request.length != 48) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                    break;
                }

                os::IpcMessage info{};
                stat(path, info, request.senderPid);
                if (info.data[0]) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::NotFound);
                    break;
                }

                if (info.data[1] !=
                    static_cast<uint64_t>(os::File::Type::Directory)) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                    break;
                }

                const uint64_t descriptor =
                    open(request.senderPid, path,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                reply.data[0] =
                    descriptor ? 0
                               : static_cast<int64_t>(os::ErrorCode::IoError);
                reply.data[1] = descriptor;
                reply.length = 2;
                break;
            }

            case services::FsRequest::DirClose:
                reply.data[0] =
                    request.length == 1
                        ? (close(request.senderPid, request.data[0])
                               ? static_cast<int64_t>(
                                     os::ErrorCode::InvalidHandle)
                               : 0)
                        : static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            case services::FsRequest::DirRead:
                if (request.length != 3 || !request.data[2] ||
                    request.data[2] > 14) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                    break;
                }

                readOpenedDirectory(request.senderPid, request.data[0],
                                    request.data[1], request.data[2], reply);
                break;
            case services::FsRequest::DirCreate: {
                services::wire::TaskInfo caller{};
                if (request.length != 49 || request.data[48] > 07777 ||
                    os::detail::TaskQuery().query(request.senderPid, caller)) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                    break;
                }

                reply.data[0] =
                    create(path, true, request.data[48], caller.effectiveUid,
                           caller.effectiveGid)
                        ? 0
                        : static_cast<int64_t>(os::ErrorCode::IoError);
                break;
            }

            case services::FsRequest::DirRemove:
                reply.data[0] =
                    request.length == 48
                        ? (remove(path, true)
                               ? 0
                               : static_cast<int64_t>(os::ErrorCode::IoError))
                        : static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            case services::FsRequest::DirRename:
                reply.data[0] =
                    request.length == 96
                        ? (rename(path, path + 384, true)
                               ? 0
                               : static_cast<int64_t>(os::ErrorCode::IoError))
                        : static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                break;
            case services::FsRequest::Open: {
                const int flags = request.length >= 49
                                      ? static_cast<int>(request.data[48])
                                      : O_RDONLY;
                const uint16_t mode =
                    request.length >= 50 ? request.data[49] & 07777 : 0666;
                os::IpcMessage status{};
                stat(path, status, request.senderPid);
                const bool exists = status.data[0] == 0;
                reply.length = 2;
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
                    reply.data[1] = static_cast<uint64_t>(
                        !exists                 ? -ENOENT
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

                    services::wire::TaskInfo caller{};
                    if (!os::detail::TaskQuery().query(request.senderPid,
                                                       caller)) {
                        changeOwner(path, caller.effectiveUid,
                                    caller.effectiveGid);
                    }

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

            case services::FsRequest::Close:
                reply.data[0] = close(request.senderPid, request.data[0]);
                break;
            case services::FsRequest::Sync: {
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
                reply.length = 1;
                break;
            }

            case services::FsRequest::Read:
                read(request.senderPid, request.data[0], request.data[1],
                     reply);
                break;
            case services::FsRequest::Write:
                write(request.senderPid, request, reply);
                break;
            case services::FsRequest::Seek:
                seek(request.senderPid, request.data[0],
                     static_cast<int64_t>(request.data[1]), request.data[2],
                     reply);
                break;
            case services::FsRequest::Stat:
                stat(path, reply, request.senderPid);
                break;
            case services::FsRequest::Create:
            case services::FsRequest::MakeDirectory:
                reply.data[0] =
                    create(path,
                           operation == services::FsRequest::MakeDirectory)
                        ? 0
                        : 1;
                if (!reply.data[0]) {
                    services::wire::TaskInfo caller{};
                    if (!os::detail::TaskQuery().query(request.senderPid,
                                                       caller)) {
                        changeOwner(path, caller.effectiveUid,
                                    caller.effectiveGid);
                    }
                }

                break;
            case services::FsRequest::Ftruncate: {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument);
                if (request.length != 2 || request.data[1]) {
                    break;
                }

                OpenFileDescription* entry = nullptr;
                {
                    std::lock_guard<std::mutex> descriptors(descriptorLock_);
                    auto* descriptor =
                        getDescriptorRecord(request.senderPid, request.data[0]);
                    if (descriptor &&
                        (descriptor->flags & O_ACCMODE) != O_RDONLY) {
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
                        (entry->mode & 0xf000) == fs::modeRegular &&
                        !readOnly(entry->mountIndex)) {
                        std::lock_guard<std::mutex> filesystem(
                            fileSystemLock(entry->mountIndex));
                        const fs::FileStatus status{
                            entry->inode,      entry->mode,
                            entry->size,       entry->uid,
                            entry->gid,        entry->links,
                            entry->accessTime, entry->modificationTime,
                            entry->changeTime, entry->privateData};
                        const bool success =
                            entry->filesystem->truncate(status);
                        reply.data[0] =
                            success
                                ? 0
                                : static_cast<int64_t>(os::ErrorCode::IoError);
                        if (success) {
                            entry->size = 0;
                        }
                    }
                }

                releaseDescription(entry);
                break;
            }

            case services::FsRequest::RemoveFile:
                reply.data[0] =
                    remove(path, false, true)
                        ? 0
                        : static_cast<int64_t>(os::ErrorCode::IoError);
                break;
            case services::FsRequest::Remove:
                reply.data[0] = remove(path) ? 0 : 1;
                break;
            case services::FsRequest::ReadDirectory:
                readDirectory(path, request.data[48], reply, request.senderPid);
                break;
            case services::FsRequest::ReadDirectoryBatch:
                readDirectoryBatch(path, request.data[48], request.data[49],
                                   reply, request.senderPid);
                break;
            case services::FsRequest::Truncate:
                reply.data[0] = truncate(path);
                break;
            case services::FsRequest::Link:
                reply.data[0] = link(path, path + 384) ? 0 : 1;
                break;
            case services::FsRequest::Rename:
                reply.data[0] = rename(path, path + 384) ? 0 : 1;
                break;
            case services::FsRequest::ChangeMode:
                reply.data[0] =
                    changeMode(path, static_cast<uint16_t>(request.data[48]))
                        ? 0
                        : 1;
                break;
            case services::FsRequest::ChangeOwner:
                if (request.data[48] > 0xffffU || request.data[49] > 0xffffU) {
                    reply.data[0] = 1;
                } else {
                    reply.data[0] =
                        changeOwner(path,
                                    static_cast<uint16_t>(request.data[48]),
                                    static_cast<uint16_t>(request.data[49]))
                            ? 0
                            : 1;
                }

                break;
            case services::FsRequest::ChangeTimes:
                if (request.data[48] > UINT32_MAX ||
                    request.data[49] > UINT32_MAX) {
                    reply.data[0] = 1;
                } else {
                    reply.data[0] =
                        changeTimes(path,
                                    static_cast<uint32_t>(request.data[48]),
                                    static_cast<uint32_t>(request.data[49]))
                            ? 0
                            : 1;
                }

                break;
            case services::FsRequest::CloneDescriptors:
                cloneDescriptors(request.data[0], request.senderPid, reply);
                break;
            case services::FsRequest::DelegateMapping:
                delegateMapping(request, reply);
                break;
            case services::FsRequest::ClaimMapping:
                claimMapping(request, reply);
                break;
            case services::FsRequest::ReleaseMapping:
                releaseMapping(request, reply);
                break;
            case services::FsRequest::ListMappings:
                listMappings(request, reply);
                break;
            case services::FsRequest::CancelMapping:
                cancelMapping(request, reply);
                break;
            case services::FsRequest::CloneForChild:
                cloneDescriptors(request.senderPid, request.data[0], reply);
                break;
            case services::FsRequest::Fstat:
                descriptorStat(request.senderPid, request.data[0], reply);
                break;
            case services::FsRequest::ReadAt:
            case services::FsRequest::WriteAt:
                transferAt(request, reply,
                           operation == services::FsRequest::WriteAt);
                break;
            case services::FsRequest::Mount:
                reply.data[0] = mount(request) ? 0 : 1;
                if (!reply.data[0]) {
                    std::lock_guard<std::mutex> state(mountStateLock_);
                    const char* target =
                        reinterpret_cast<const char*>(request.data) + 64;
                    for (uint32_t index = 0; index < mountCapacity; ++index) {
                        if (mounted_[index] &&
                            os::Runtime::stringsEqual(
                                mountTable_[index].spec.target, target)) {
                            reply.data[1] =
                                readOnly(static_cast<PathBackend>(index));
                            reply.length = 2;
                            break;
                        }
                    }
                }

                break;
            case services::FsRequest::Unmount:
                reply.data[0] = unmount(request) ? 0 : 1;
                break;
            case services::FsRequest::ListMounts:
                listMount(static_cast<uint32_t>(request.data[0]), reply);
                break;
            case services::FsRequest::Format:
                reply.data[0] = format(request) ? 0 : 1;
                break;
            default:
                reply.data[0] = 1;
                break;
        }

        if (reply.length == 0) {
            reply.length = 1;
        }

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
                        nextMaintenance =
                            os::Sys().readMonotonicTime() + 1000000000ULL;
                    }

                    os::IpcMessage request{};
                    if (os::receiveMessage(
                            fsEndpoint, request,
                            maintenance ? nextMaintenance
                                        : services::ipcInfiniteDeadline) != 0) {
                        continue;
                    }

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
                fsEndpoint = os::detail::EndpointControl().create();
                if (registry == 0 || fsEndpoint == 0 ||
                    !os::detail::RegistryClient(registry).registerName(
                        services::fsServiceName, fsEndpoint)) {
                    os::Sys().log("[FS] registration failed\n");
                    return 1;
                }

                const uint64_t storage =
                    os::detail::RegistryClient(registry).lookup(
                        services::storageServiceName, 5000);
                const uint64_t device =
                    os::detail::RegistryClient(registry).lookup(
                        services::deviceServiceName, 5000);
                if (!fsServer.init(storage, device)) {
                    os::Sys().log(
                        "[FS] fstab filesystem initialization failed\n");
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
                os::Sys().log(
                    "[FS] mounted filesystems configured in /etc/fstab\n");
                workerLoop(true);
            }
    };
}  // namespace fs::server

int main(int, char**) {
    // The service is too large for its startup stack. Its storage has process
    // lifetime; explicitly destroy the object if startup returns with an error.
    alignas(fs::server::FsApplication) static unsigned char
        storage[sizeof(fs::server::FsApplication)];
    auto* application = new (storage) fs::server::FsApplication;
    const int status = application->run();
    application->~FsApplication();
    return status;
}
