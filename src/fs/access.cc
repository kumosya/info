#include "os/task.h"
#include "server.h"
#include <fcntl.h>

namespace vfs::server {
namespace {
class AccessPolicy {
 public:
    static bool bits(uint32_t mode, uint32_t uid, uint32_t gid, const os::TaskInfo& caller, unsigned access);
};

bool AccessPolicy::bits(uint32_t mode, uint32_t uid, uint32_t gid, const os::TaskInfo& caller, unsigned access) {
    if (!caller.effectiveUid) return true;
    const unsigned shift = caller.effectiveUid == uid ? 6 : caller.effectiveGid == gid ? 3 : 0;
    return ((mode >> shift) & access) == access;
}
}

bool FsServer::pathAccess(const char* path, const os::TaskInfo& caller, unsigned access, bool parent) {
    if (!path || path[0] != '/') return false;
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
        const char saved = current[i]; current[i] = 0;
        os::IpcMessage status{};
        stat(current, status);
        current[i] = saved;
        if (status.data[0]) return false;
        const unsigned required = i == length ? access : 1;
        if (!AccessPolicy::bits(status.data[3], status.data[6], status.data[7], caller, required)) return false;
    }
    return true;
}

bool FsServer::permitted(const os::IpcMessage& request) {
    os::TaskInfo caller{};
    if (os::Process::queryTask(request.senderPid, caller)) return false;
    if (!caller.effectiveUid) return true;
    const auto operation = static_cast<os::FsRequest>(request.type);
    const char* path = reinterpret_cast<const char*>(request.data);
    switch (operation) {
        case os::FsRequest::Mount: case os::FsRequest::Unmount: case os::FsRequest::Format:
        case os::FsRequest::ChangeOwner: return false;
        case os::FsRequest::CloneDescriptors: return request.data[0] == caller.parentId;
        case os::FsRequest::CloneForChild: {
            os::TaskInfo child{};
            return !os::Process::queryTask(request.data[0], child) &&
                   child.parentId == request.senderPid;
        }
        case os::FsRequest::Read: case os::FsRequest::Write: case os::FsRequest::ReadAtAsync:
        case os::FsRequest::WriteAtAsync: case os::FsRequest::ReadAt: case os::FsRequest::WriteAt: {
            std::lock_guard<std::mutex> guard(descriptorLock_);
            FileDescriptor* descriptor = getDescriptorRecord(request.senderPid, request.data[0]);
            if (!descriptor) return false;
            const OpenFileDescription& entry = *descriptor->description;
            return AccessPolicy::bits(entry.mode ? entry.mode : 0666, entry.uid, entry.gid, caller,
                        operation == os::FsRequest::Read || operation == os::FsRequest::ReadAtAsync || operation == os::FsRequest::ReadAt ? 4 : 2);
        }
        case os::FsRequest::Close: case os::FsRequest::Seek: case os::FsRequest::ListMounts: case os::FsRequest::Fstat: return true;
        case os::FsRequest::Stat: return pathAccess(path, caller, 0);
        case os::FsRequest::Open: {
            const int flags = request.length >= 49 ? static_cast<int>(request.data[48]) : O_RDONLY;
            os::IpcMessage status{}; stat(path,status);
            if (status.data[0] && (flags&O_CREAT)) return pathAccess(path,caller,3,true);
            const unsigned access = (flags&O_ACCMODE)==O_RDWR ? 6 : (flags&O_ACCMODE)==O_WRONLY ? 2 : 4;
            return pathAccess(path,caller,access);
        }
        case os::FsRequest::ReadDirectory: case os::FsRequest::ReadDirectoryBatch: return pathAccess(path, caller, 5);
        case os::FsRequest::Create: case os::FsRequest::MakeDirectory: return pathAccess(path, caller, 3, true);
        case os::FsRequest::Truncate: return pathAccess(path, caller, 2);
        case os::FsRequest::ChangeMode: case os::FsRequest::ChangeTimes: {
            if (!pathAccess(path, caller, 0)) return false;
            os::IpcMessage status{}; stat(path, status);
            return !status.data[0] && status.data[6] == caller.effectiveUid;
        }
        case os::FsRequest::Link: return pathAccess(path, caller, 0) && pathAccess(path + 384, caller, 3, true);
        case os::FsRequest::Rename:
            if (!pathAccess(path + 384, caller, 3, true)) return false;
            [[fallthrough]];
        case os::FsRequest::Remove: {
            if (!pathAccess(path, caller, 3, true)) return false;
            // Sticky directories restrict removal to the file or directory owner.
            char parent[384]; os::Runtime::copyString(parent, path, sizeof(parent));
            size_t length = os::Runtime::stringLength(parent);
            while (length > 1 && parent[length - 1] == '/') --length;
            while (length > 1 && parent[length - 1] != '/') --length;
            parent[length > 1 ? length - 1 : 1] = 0;
            os::IpcMessage directory{}, file{}; stat(parent, directory); stat(path, file);
            return !(directory.data[3] & 01000) || directory.data[6] == caller.effectiveUid ||
                   (!file.data[0] && file.data[6] == caller.effectiveUid);
        }
        default: return false;
    }
}
}
