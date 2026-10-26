#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/server.h"

namespace vfs::server {

int FsServer::deviceDirectory(const char* path, uint32_t index,
                              vfs::DirectoryEntry& entry) const {
    if (os::Runtime::stringsEqual(path, "/dev/pts")) return 1;
    if (!os::Runtime::stringsEqual(path, "/dev"))
        return catalog_.list(path, index, entry);
    uint32_t count = 0;
    while (true) {
        os::IpcMessage request{}, reply{};
        request.type    = static_cast<uint64_t>(os::DeviceRequest::ListNode);
        request.data[0] = count;
        request.length  = 1;
        if (!deviceEndpoint_ ||
            os::callService(deviceEndpoint_, request, reply) != 0 ||
            reply.data[0] > 1)
            return -1;
        if (reply.data[0] == 1) break;
        if (count++ == index) {
            entry      = {};
            entry.type = reply.data[1] ? 2 : 1;
            os::Runtime::copyString(
                entry.name, reinterpret_cast<const char*>(&reply.data[2]),
                sizeof(entry.name));
            return 0;
        }
    }
    return catalog_.list(path, index - count, entry);
}

void FsServer::readDirectory(const char* path, uint64_t index,
                             os::IpcMessage& reply, uint64_t caller) {
    if (index > UINT32_MAX) {
        reply.data[0] = 1;
        return;
    }
    vfs::DirectoryEntry entry{};
    int result = -1;
    if (PathText::startsWithPath(path, "/proc")) {
        result = processObjectDirectory(path, index, caller, entry);
        if (result == -2)
            result = vfs::ProcessFiles::directory(path, index, caller, entry);
    } else if (PathText::startsWithPath(path, "/sys"))
        result = vfs::SystemFiles::directory(path, static_cast<uint32_t>(index),
                                             entry);
    else if (PathText::startsWithPath(path, "/dev"))
        result = deviceDirectory(path, static_cast<uint32_t>(index), entry);
    else {
        const PathBackend backend   = pathBackend(path);
        vfs::FileSystem* filesystem = fileSystem(backend);
        if (filesystem) {
            std::lock_guard<std::mutex> guard(fileSystemLock(backend));
            result =
                filesystem->readDirectory(relativePath(path, backend),
                                          static_cast<uint32_t>(index), entry);
        }
    }
    reply.data[0] = result < 0 ? 1 : (result > 0 ? 2 : 0);
    if (result == 0) {
        reply.data[1] = static_cast<uint64_t>(
            entry.type == 2 ? os::File::Type::Directory : os::File::Type::File);
        os::Runtime::copyString(reinterpret_cast<char*>(&reply.data[2]),
                                entry.name, 256);
        reply.data[34] = entry.inode;
        reply.data[35] = PathText::startsWithPath(path, "/proc")
                             ? vfs::ProcessFiles::nextCursor(path, entry, index)
                             : index + 1;
        reply.length   = 36;
    }
}

void FsServer::readDirectoryBatch(const char* path, uint64_t index,
                                  uint64_t requested, os::IpcMessage& reply,
                                  uint64_t caller, bool filesystemLocked) {
    constexpr uint32_t maximum =
        (os::ipcDataWordCount - 2) * 8 / sizeof(os::wire::DirectoryEntry);
    const uint32_t capacity =
        requested < maximum ? static_cast<uint32_t>(requested) : maximum;
    if (!capacity || index > UINT32_MAX || capacity > UINT32_MAX - index) {
        reply.data[0] = 1;
        return;
    }
    vfs::DirectoryEntry entries[maximum]{};
    int count = 0;
    if (PathText::startsWithPath(path, "/proc")) {
        uint64_t cursor = index;
        while (count < static_cast<int>(capacity)) {
            int result =
                processObjectDirectory(path, cursor, caller, entries[count]);
            if (result == -2)
                result = vfs::ProcessFiles::directory(path, cursor, caller,
                                                      entries[count]);
            if (result < 0) {
                reply.data[0] = 1;
                return;
            }
            if (result > 0) break;
            cursor =
                vfs::ProcessFiles::nextCursor(path, entries[count], cursor);
            ++count;
        }
    } else if (PathText::startsWithPath(path, "/dev") ||
               PathText::startsWithPath(path, "/sys")) {
        while (count < static_cast<int>(capacity)) {
            const int result =
                PathText::startsWithPath(path, "/sys")
                    ? vfs::SystemFiles::directory(
                          path, static_cast<uint32_t>(index + count),
                          entries[count])
                    : deviceDirectory(path,
                                      static_cast<uint32_t>(index + count),
                                      entries[count]);
            if (result < 0) {
                reply.data[0] = 1;
                return;
            }
            if (result > 0) break;
            ++count;
        }
    } else {
        const PathBackend backend   = pathBackend(path);
        vfs::FileSystem* filesystem = fileSystem(backend);
        if (!filesystem) {
            reply.data[0] = 1;
            return;
        }
        if (!filesystemLocked) fileSystemLock(backend).lock();
        count = filesystem->readDirectoryBatch(relativePath(path, backend),
                                               static_cast<uint32_t>(index),
                                               entries, capacity);
        if (!filesystemLocked) fileSystemLock(backend).unlock();
        if (count < 0) {
            reply.data[0] = 1;
            return;
        }
    }
    auto* output = reinterpret_cast<os::wire::DirectoryEntry*>(&reply.data[2]);
    uint64_t cursor = index;
    for (int i = 0; i < count; ++i) {
        output[i].type = static_cast<uint64_t>(entries[i].type == 2
                                                   ? os::File::Type::Directory
                                                   : os::File::Type::File);
        os::Runtime::copyString(output[i].name, entries[i].name,
                                sizeof(output[i].name));
        output[i].inode      = entries[i].inode;
        cursor               = PathText::startsWithPath(path, "/proc")
                                   ? vfs::ProcessFiles::nextCursor(path, entries[i], cursor)
                                   : cursor + 1;
        output[i].nextOffset = cursor;
    }
    reply.data[0] = 0;
    reply.data[1] = static_cast<uint64_t>(count);
    reply.length  = 2 + (count * sizeof(os::wire::DirectoryEntry) + 7) / 8;
}
}  // namespace vfs::server

namespace vfs::server {
void FsServer::readOpenedDirectory(uint64_t owner, uint64_t descriptor,
                                   uint64_t cursor, uint64_t capacity,
                                   os::IpcMessage& reply) {
    auto* entry = acquireDescription(owner, descriptor);
    if (!entry) {
        reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidHandle);
        return;
    }
    if ((entry->mode & 0xf000) != vfs::modeDirectory) {
        releaseDescription(entry);
        reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidArgument);
        return;
    }
    auto read = [&](bool locked) {
        char path[384];
        {
            std::lock_guard<std::mutex> guard(descriptorLock_);
            os::Runtime::copyString(path, entry->path, sizeof(path));
        }
        readDirectoryBatch(path, cursor, capacity, reply, owner, locked);
        if (reply.data[0])
            reply.data[0] = static_cast<int64_t>(os::ErrorCode::IoError);
    };
    if (entry->filesystem) {
        std::lock_guard<std::mutex> guard(fileSystemLock(entry->mountIndex));
        read(true);
    } else
        read(false);
    releaseDescription(entry);
}
}  // namespace vfs::server
