#include <fcntl.h>

#include <os/task.h>
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/server.h"

namespace vfs::server {
namespace {
class AccessPolicy {
   public:
    static bool bits(uint32_t mode, uint32_t uid, uint32_t gid,
                     const os::wire::CredentialInfo& caller, unsigned access);
};

bool AccessPolicy::bits(uint32_t mode, uint32_t uid, uint32_t gid,
                        const os::wire::CredentialInfo& caller,
                        unsigned access) {
    if (!caller.effectiveUid) return true;
    bool member = caller.effectiveGid == gid;
    for (uint32_t i = 0; i < caller.groupCount; ++i)
        if (caller.groups[i] == gid) member = true;
    const unsigned shift = caller.effectiveUid == uid ? 6 : member ? 3 : 0;
    return ((mode >> shift) & access) == access;
}
}  // namespace

bool FsServer::pathAccess(const char* path, const os::wire::TaskInfo& caller,
                          unsigned access, bool parent) {
    if (!path || path[0] != '/') return false;
    os::wire::CredentialInfo credentials{};
    if (os::detail::TaskQuery().credentials(caller.id, credentials))
        return false;
    char current[384]{};
    size_t length = 0;
    while (path[length] && length < sizeof(current)) ++length;
    if (length >= sizeof(current)) return false;
    if (parent) {
        while (length > 1 && path[length - 1] == '/') --length;
        while (length > 1 && path[length - 1] != '/') --length;
        if (length > 1) --length;
    }
    for (size_t i = 0; i < length; ++i) current[i] = path[i];
    for (size_t i = 1; i <= length; ++i) {
        if (i < length && current[i] != '/') continue;
        const char saved = current[i];
        current[i]       = 0;
        os::IpcMessage status{};
        stat(current, status, caller.id);
        current[i] = saved;
        if (status.data[0]) return false;
        const unsigned required = i == length ? access : 1;
        if (!AccessPolicy::bits(status.data[3], status.data[6], status.data[7],
                                credentials, required))
            return false;
    }
    return true;
}

bool FsServer::permitted(const os::IpcMessage& request) {
    os::wire::TaskInfo caller{};
    if (os::detail::TaskQuery().query(request.senderPid, caller)) return false;
    if (request.type == static_cast<uint64_t>(os::FsRequest::ReleaseMapping) ||
        request.type == static_cast<uint64_t>(os::FsRequest::ListMappings))
        return caller.managedService &&
               caller.serviceRole ==
                   static_cast<uint32_t>(os::ServiceRole::Memory);
    if (!caller.effectiveUid) return true;
    const auto operation = static_cast<os::FsRequest>(request.type);
    const char* path     = reinterpret_cast<const char*>(request.data);
    switch (operation) {
        case os::FsRequest::Mount:
        case os::FsRequest::Unmount:
        case os::FsRequest::Format:
        case os::FsRequest::ChangeOwner:
            return false;
        case os::FsRequest::CloneDescriptors:
            return request.data[0] == caller.parentId;
        case os::FsRequest::CloneForChild: {
            os::wire::TaskInfo child{};
            return !os::detail::TaskQuery().query(request.data[0], child) &&
                   child.parentId == request.senderPid;
        }
        case os::FsRequest::Read:
        case os::FsRequest::Write:
        case os::FsRequest::ReadAtAsync:
        case os::FsRequest::WriteAtAsync:
        case os::FsRequest::Ftruncate:
        case os::FsRequest::ReadAt:
        case os::FsRequest::WriteAt: {
            std::lock_guard<std::mutex> guard(descriptorLock_);
            FileDescriptor* descriptor =
                getDescriptorRecord(request.senderPid, request.data[0]);
            if (!descriptor) return false;
            // Access was granted when the open description was created. Later
            // credential changes do not revoke an already-open file descriptor.
            const bool reading = operation == os::FsRequest::Read ||
                                 operation == os::FsRequest::ReadAtAsync ||
                                 operation == os::FsRequest::ReadAt;
            const int access = descriptor->flags & O_ACCMODE;
            return reading ? access != O_WRONLY : access != O_RDONLY;
        }
        case os::FsRequest::DelegateMapping:
        case os::FsRequest::ClaimMapping:
        case os::FsRequest::CancelMapping:
        case os::FsRequest::Close:
        case os::FsRequest::Seek:
        case os::FsRequest::ListMounts:
        case os::FsRequest::Fstat:
        case os::FsRequest::Sync:
        case os::FsRequest::DirClose:
        case os::FsRequest::DirRead:
            return true;
        case os::FsRequest::Stat:
            return pathAccess(path, caller, 0);
        case os::FsRequest::DirOpen:
            return pathAccess(path, caller, 5);
        case os::FsRequest::Open: {
            const int flags = request.length >= 49
                                  ? static_cast<int>(request.data[48])
                                  : O_RDONLY;
            os::IpcMessage status{};
            stat(path, status, caller.id);
            if (status.data[0] && (flags & O_CREAT))
                return pathAccess(path, caller, 3, true);
            const unsigned access = (flags & O_ACCMODE) == O_RDWR     ? 6
                                    : (flags & O_ACCMODE) == O_WRONLY ? 2
                                                                      : 4;
            return pathAccess(path, caller, access);
        }
        case os::FsRequest::ReadDirectory:
        case os::FsRequest::ReadDirectoryBatch:
            return pathAccess(path, caller, 5);
        case os::FsRequest::Create:
        case os::FsRequest::MakeDirectory:
        case os::FsRequest::DirCreate:
            return pathAccess(path, caller, 3, true);
        case os::FsRequest::Truncate:
            return pathAccess(path, caller, 2);
        case os::FsRequest::ChangeMode:
        case os::FsRequest::ChangeTimes: {
            if (!pathAccess(path, caller, 0)) return false;
            os::IpcMessage status{};
            stat(path, status, caller.id);
            return !status.data[0] && status.data[6] == caller.effectiveUid;
        }
        case os::FsRequest::Link:
            return pathAccess(path, caller, 0) &&
                   pathAccess(path + 384, caller, 3, true);
        case os::FsRequest::Rename:
        case os::FsRequest::DirRename:
            if (!pathAccess(path + 384, caller, 3, true)) return false;
            [[fallthrough]];
        case os::FsRequest::Remove:
        case os::FsRequest::RemoveFile:
        case os::FsRequest::DirRemove: {
            if (!pathAccess(path, caller, 3, true)) return false;
            // Sticky directories restrict removal to the file or directory
            // owner.
            char parent[384];
            os::Runtime::copyString(parent, path, sizeof(parent));
            size_t length = os::Runtime::stringLength(parent);
            while (length > 1 && parent[length - 1] == '/') --length;
            while (length > 1 && parent[length - 1] != '/') --length;
            parent[length > 1 ? length - 1 : 1] = 0;
            os::IpcMessage directory{}, file{};
            stat(parent, directory);
            stat(path, file);
            return !(directory.data[3] & 01000) ||
                   directory.data[6] == caller.effectiveUid ||
                   (!file.data[0] && file.data[6] == caller.effectiveUid);
        }
        default:
            return false;
    }
}
}  // namespace vfs::server
