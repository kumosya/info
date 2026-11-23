#include <fcntl.h>
#include <os/detail/client.h>
#include <os/runtime.h>
#include <os/task.h>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/fs/server.h"

namespace fs::server {

    namespace access_policy {

        static bool bits(uint32_t mode, uint32_t uid, uint32_t gid,
                         const services::wire::CredentialInfo& caller,
                         unsigned access);
    }

    bool access_policy::bits(uint32_t mode, uint32_t uid, uint32_t gid,
                             const services::wire::CredentialInfo& caller,
                             unsigned access) {
        if (!caller.effectiveUid) {
            return true;
        }

        bool member = caller.effectiveGid == gid;
        for (uint32_t i = 0; i < caller.groupCount; ++i) {
            if (caller.groups[i] == gid) {
                member = true;
            }
        }

        const unsigned shift = caller.effectiveUid == uid ? 6 : member ? 3 : 0;
        return ((mode >> shift) & access) == access;
    }

    bool FsServer::pathAccess(const char* path,
                              const services::wire::TaskInfo& caller,
                              unsigned access, bool parent) {
        if (!path || path[0] != '/') {
            return false;
        }

        services::wire::CredentialInfo credentials{};
        if (os::detail::TaskQuery().credentials(caller.id, credentials)) {
            return false;
        }

        char current[384]{};
        size_t length = 0;
        while (path[length] && length < sizeof(current)) {
            ++length;
        }

        if (length >= sizeof(current)) {
            return false;
        }

        if (parent) {
            while (length > 1 && path[length - 1] == '/') {
                --length;
            }

            while (length > 1 && path[length - 1] != '/') {
                --length;
            }

            if (length > 1) {
                --length;
            }
        }

        for (size_t i = 0; i < length; ++i) {
            current[i] = path[i];
        }

        for (size_t i = 1; i <= length; ++i) {
            if (i < length && current[i] != '/') {
                continue;
            }

            const char saved = current[i];
            current[i] = 0;
            os::IpcMessage status{};
            stat(current, status, caller.id);
            current[i] = saved;
            if (status.data[0]) {
                return false;
            }

            const unsigned required = i == length ? access : 1;
            if (!access_policy::bits(status.data[3], status.data[6],
                                     status.data[7], credentials, required)) {
                return false;
            }
        }

        return true;
    }

    /* 删除和覆盖目录项都要检查父目录权限，以及 sticky 目录的属主限制。 */
    bool FsServer::removalAccess(const char* path,
                                 const services::wire::TaskInfo& caller) {
        if (!pathAccess(path, caller, 3, true)) {
            return false;
        }

        char parent[384];
        os::Runtime::copyString(parent, path, sizeof(parent));
        size_t length = os::Runtime::stringLength(parent);
        while (length > 1 && parent[length - 1] == '/') {
            --length;
        }

        while (length > 1 && parent[length - 1] != '/') {
            --length;
        }

        parent[length > 1 ? length - 1 : 1] = 0;
        os::IpcMessage directory{}, file{};
        stat(parent, directory, caller.id);
        if (directory.data[0]) {
            return false;
        }

        stat(path, file, caller.id);
        return !(directory.data[3] & 01000) || file.data[0] ||
               directory.data[6] == caller.effectiveUid ||
               file.data[6] == caller.effectiveUid;
    }

    bool FsServer::permitted(const os::IpcMessage& request) {
        services::wire::TaskInfo caller{};
        if (os::detail::TaskQuery().query(request.senderPid, caller)) {
            return false;
        }

        if (request.type ==
                static_cast<uint64_t>(services::FsRequest::ReleaseMapping) ||
            request.type ==
                static_cast<uint64_t>(services::FsRequest::ListMappings)) {
            return caller.managedService &&
                   caller.serviceRole ==
                       static_cast<uint32_t>(services::ServiceRole::Memory);
        }

        if (!caller.effectiveUid) {
            return true;
        }

        const services::FsRequest operation =
            static_cast<services::FsRequest>(request.type);
        const char* path = reinterpret_cast<const char*>(request.data);
        switch (operation) {
            case services::FsRequest::Mount:
            case services::FsRequest::Unmount:
            case services::FsRequest::Format:
            case services::FsRequest::ChangeOwner:
                return false;
            case services::FsRequest::CloneDescriptors:
                return request.data[0] == caller.parentId;
            case services::FsRequest::CloneForChild: {
                services::wire::TaskInfo child{};
                return !os::detail::TaskQuery().query(request.data[0], child) &&
                       child.parentId == request.senderPid;
            }

            case services::FsRequest::Read:
            case services::FsRequest::Write:
            case services::FsRequest::ReadAtAsync:
            case services::FsRequest::WriteAtAsync:
            case services::FsRequest::Ftruncate:
            case services::FsRequest::ReadAt:
            case services::FsRequest::ReadBuffer:
            case services::FsRequest::WriteAt: {
                std::lock_guard<std::mutex> guard(descriptorLock_);
                FileDescriptor* descriptor =
                    getDescriptorRecord(request.senderPid, request.data[0]);
                if (!descriptor) {
                    return false;
                }

                // Access was granted when the open description was created.
                // Later credential changes do not revoke an already-open file
                // descriptor.
                const bool reading =
                    operation == services::FsRequest::Read ||
                    operation == services::FsRequest::ReadAtAsync ||
                    operation == services::FsRequest::ReadAt ||
                    operation == services::FsRequest::ReadBuffer;
                const int access = descriptor->flags & O_ACCMODE;
                return reading ? access != O_WRONLY : access != O_RDONLY;
            }

            case services::FsRequest::DelegateMapping:
            case services::FsRequest::ClaimMapping:
            case services::FsRequest::CancelMapping:
            case services::FsRequest::Close:
            case services::FsRequest::Seek:
            case services::FsRequest::ListMounts:
            case services::FsRequest::Fstat:
            case services::FsRequest::Sync:
            case services::FsRequest::DirClose:
            case services::FsRequest::DirRead:
                return true;
            case services::FsRequest::Stat:
                return pathAccess(path, caller, 0);
            case services::FsRequest::DirOpen:
                return pathAccess(path, caller, 5);
            case services::FsRequest::Open: {
                const int flags = request.length >= 49
                                      ? static_cast<int>(request.data[48])
                                      : O_RDONLY;
                os::IpcMessage status{};
                stat(path, status, caller.id);
                if (status.data[0] && (flags & O_CREAT)) {
                    return pathAccess(path, caller, 3, true);
                }

                const unsigned access = (flags & O_ACCMODE) == O_RDWR     ? 6
                                        : (flags & O_ACCMODE) == O_WRONLY ? 2
                                                                          : 4;
                return pathAccess(path, caller, access);
            }

            case services::FsRequest::ReadDirectory:
            case services::FsRequest::ReadDirectoryBatch:
                return pathAccess(path, caller, 5);
            case services::FsRequest::Create:
            case services::FsRequest::MakeDirectory:
            case services::FsRequest::DirCreate:
                return pathAccess(path, caller, 3, true);
            case services::FsRequest::Truncate:
                return pathAccess(path, caller, 2);
            case services::FsRequest::ChangeMode:
            case services::FsRequest::ChangeTimes: {
                if (!pathAccess(path, caller, 0)) {
                    return false;
                }

                os::IpcMessage status{};
                stat(path, status, caller.id);
                return !status.data[0] && status.data[6] == caller.effectiveUid;
            }

            case services::FsRequest::Link:
                return pathAccess(path, caller, 0) &&
                       pathAccess(path + 384, caller, 3, true);
            case services::FsRequest::Rename:
            case services::FsRequest::DirRename:
                if (!removalAccess(path + 384, caller)) {
                    return false;
                }

                [[fallthrough]];
            case services::FsRequest::Remove:
            case services::FsRequest::RemoveFile:
            case services::FsRequest::DirRemove: {
                return removalAccess(path, caller);
            }

            default:
                return false;
        }
    }
}  // namespace fs::server
