#include "os/file.h"

#include "os/abi.h"

namespace {

void setPath(os::IpcMessage& message, const char* path) {
    os::copyString(reinterpret_cast<char*>(message.data), path,
                   sizeof(message.data));
    message.length = 8;
}

}  // namespace

namespace os {

uint64_t openFile(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Open);
    setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 ? reply.data[0] : 0;
}

bool closeFile(uint64_t fs, uint64_t descriptor) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(FsRequest::Close);
    request.data[0] = descriptor;
    request.length  = 1;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

int64_t readFile(uint64_t fs, uint64_t descriptor, void* buffer, size_t size) {
    if (buffer == nullptr) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    size_t total = 0;
    while (total < size) {
        const size_t chunk =
            size - total < (ipcDataWordCount - 1) * sizeof(uint64_t)
                ? size - total
                : (ipcDataWordCount - 1) * sizeof(uint64_t);
        IpcMessage request{};
        request.type    = static_cast<uint64_t>(FsRequest::Read);
        request.data[0] = descriptor;
        request.data[1] = chunk;
        request.length  = 2;
        IpcMessage reply{};
        if (callService(fs, request, reply) != 0) {
            return static_cast<int64_t>(ErrorCode::IoError);
        }
        const size_t count = reply.data[0];
        for (size_t index = 0; index < count; ++index) {
            static_cast<uint8_t*>(buffer)[total + index] =
                reinterpret_cast<const uint8_t*>(&reply.data[1])[index];
        }
        total += count;
        if (count < chunk) {
            break;
        }
    }
    return static_cast<int64_t>(total);
}

int64_t writeFile(uint64_t fs, uint64_t descriptor, const void* buffer,
                  size_t size) {
    if (buffer == nullptr) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    size_t total = 0;
    while (total < size) {
        const size_t chunk =
            size - total < (ipcDataWordCount - 2) * sizeof(uint64_t)
                ? size - total
                : (ipcDataWordCount - 2) * sizeof(uint64_t);
        IpcMessage request{};
        request.type    = static_cast<uint64_t>(FsRequest::Write);
        request.data[0] = descriptor;
        request.data[1] = chunk;
        for (size_t index = 0; index < chunk; ++index) {
            reinterpret_cast<uint8_t*>(&request.data[2])[index] =
                static_cast<const uint8_t*>(buffer)[total + index];
        }
        request.length = static_cast<uint32_t>(
            2 + (chunk + sizeof(uint64_t) - 1) / sizeof(uint64_t));
        IpcMessage reply{};
        if (callService(fs, request, reply) != 0 || reply.data[0] == 0) {
            return total == 0 ? static_cast<int64_t>(ErrorCode::IoError)
                              : static_cast<int64_t>(total);
        }
        total += reply.data[0];
    }
    return static_cast<int64_t>(total);
}

bool readFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset,
                   size_t size, uint64_t completionEndpoint,
                   uint64_t requestId) {
    if (fs == 0 || completionEndpoint == 0 || size == 0 ||
        size > maximumAsyncFileTransfer) {
        return false;
    }
    IpcMessage request{};
    request.type        = static_cast<uint64_t>(FsRequest::ReadAtAsync);
    request.data[0]     = descriptor;
    request.data[1]     = offset;
    request.data[2]     = size;
    request.data[3]     = requestId;
    request.length      = 4;
    request.handleCount = 1;
    request.handles[0]  = completionEndpoint;
    return sendMessage(fs, request) == 0;
}

bool writeFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset,
                    const void* buffer, size_t size,
                    uint64_t completionEndpoint, uint64_t requestId) {
    if (fs == 0 || completionEndpoint == 0 || buffer == nullptr || size == 0 ||
        size > maximumAsyncFileTransfer) {
        return false;
    }
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(FsRequest::WriteAtAsync);
    request.data[0] = descriptor;
    request.data[1] = offset;
    request.data[2] = size;
    request.data[3] = requestId;
    for (size_t index = 0; index < size; ++index) {
        reinterpret_cast<uint8_t*>(&request.data[4])[index] =
            static_cast<const uint8_t*>(buffer)[index];
    }
    request.length = static_cast<uint32_t>(4 + (size + sizeof(uint64_t) - 1) /
                                                   sizeof(uint64_t));
    request.handleCount = 1;
    request.handles[0]  = completionEndpoint;
    return sendMessage(fs, request) == 0;
}

bool waitFileOperation(uint64_t completionEndpoint, FileOperationResult& result,
                       void* buffer, size_t capacity) {
    if (completionEndpoint == 0) return false;
    while (true) {
        IpcMessage completion{};
        if (receiveMessage(completionEndpoint, completion) != 0) return false;
        if (completion.type != fsCompletionNotification) continue;
        result.requestId   = completion.data[0];
        result.status      = static_cast<int64_t>(completion.data[1]);
        result.transferred = completion.data[2];
        if (result.status == 0 && completion.length > 3) {
            if (buffer == nullptr || capacity < result.transferred) {
                return false;
            }
            for (size_t index = 0; index < result.transferred; ++index) {
                static_cast<uint8_t*>(buffer)[index] =
                    reinterpret_cast<const uint8_t*>(
                        &completion.data[3])[index];
            }
        }
        return true;
    }
}

bool seekFile(uint64_t fs, uint64_t descriptor, uint64_t offset) {
    return seekFilePosition(fs, descriptor, static_cast<int64_t>(offset), 0) >=
           0;
}

int64_t seekFilePosition(uint64_t fs, uint64_t descriptor, int64_t offset,
                         int whence) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(FsRequest::Seek);
    request.data[0] = descriptor;
    request.data[1] = static_cast<uint64_t>(offset);
    request.data[2] = static_cast<uint64_t>(whence);
    request.length  = 3;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0
               ? static_cast<int64_t>(reply.data[1])
               : -1;
}

int cloneFileDescriptors(uint64_t fs, uint64_t parentProcessId,
                         uint64_t* oldDescriptors,
                         uint64_t* newDescriptors, size_t capacity) {
    if (fs == 0 || parentProcessId == 0 || oldDescriptors == nullptr ||
        newDescriptors == nullptr || capacity == 0) {
        return -1;
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::CloneDescriptors);
    request.data[0] = parentProcessId;
    request.length = 1;
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0 ||
        reply.data[1] > capacity || reply.length < 2 + reply.data[1] * 2) {
        return -1;
    }
    const size_t count = static_cast<size_t>(reply.data[1]);
    for (size_t index = 0; index < count; ++index) {
        oldDescriptors[index] = reply.data[2 + index * 2];
        newDescriptors[index] = reply.data[3 + index * 2];
    }
    return static_cast<int>(count);
}

bool createFile(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Create);
    setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool makeDirectory(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::MakeDirectory);
    setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool removePath(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Remove);
    setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool truncateFile(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Truncate);
    setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool statFile(uint64_t fs, const char* path, FileStat& stat) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Stat);
    setPath(request, path);
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0) {
        return false;
    }
    stat.type = static_cast<FileType>(reply.data[1]);
    stat.size = reply.data[2];
    stat.mode = static_cast<uint32_t>(reply.data[3]);
    stat.inode = reply.data[4];
    stat.links = static_cast<uint32_t>(reply.data[5]);
    stat.uid = static_cast<uint32_t>(reply.data[6]);
    stat.gid = static_cast<uint32_t>(reply.data[7]);
    stat.accessTime = reply.data[8];
    stat.modificationTime = reply.data[9];
    stat.changeTime = reply.data[10];
    return true;
}

bool linkFile(uint64_t fs, const char* existing, const char* newPath) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Link);
    copyString(reinterpret_cast<char*>(request.data), existing, 256);
    copyString(reinterpret_cast<char*>(request.data) + 256, newPath, 256);
    request.length = 64;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool changeFileMode(uint64_t fs, const char* path, uint32_t mode) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ChangeMode);
    copyString(reinterpret_cast<char*>(request.data), path, 256);
    request.data[32] = mode;
    request.length = 33;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool changeFileOwner(uint64_t fs, const char* path, uint32_t uid,
                     uint32_t gid) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ChangeOwner);
    copyString(reinterpret_cast<char*>(request.data), path, 256);
    request.data[32] = uid;
    request.data[33] = gid;
    request.length = 34;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool changeFileTimes(uint64_t fs, const char* path, uint64_t accessTime,
                     uint64_t modificationTime) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ChangeTimes);
    copyString(reinterpret_cast<char*>(request.data), path, 256);
    request.data[32] = accessTime;
    request.data[33] = modificationTime;
    request.length = 34;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool readDirectory(uint64_t fs, const char* path, uint64_t index,
                   DirectoryEntry& entry) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ReadDirectory);
    setPath(request, path);
    request.data[7] = index;
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0) {
        return false;
    }
    entry.type = static_cast<FileType>(reply.data[1]);
    copyString(entry.name, reinterpret_cast<const char*>(&reply.data[2]),
               sizeof(entry.name));
    return true;
}

int64_t readDirectoryBatch(uint64_t fs, const char* path, uint64_t index,
                           DirectoryEntry* entries, size_t capacity) {
    constexpr size_t maximumEntries =
        (ipcDataWordCount - 2) * sizeof(uint64_t) / sizeof(DirectoryEntry);
    if (fs == 0 || entries == nullptr || capacity == 0) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    if (capacity > maximumEntries) capacity = maximumEntries;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ReadDirectoryBatch);
    setPath(request, path);
    request.data[7] = index;
    request.data[8] = capacity;
    request.length  = 9;
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0 ||
        reply.data[1] > capacity) {
        return static_cast<int64_t>(ErrorCode::IoError);
    }
    const size_t count = static_cast<size_t>(reply.data[1]);
    const size_t requiredWords =
        2 + (count * sizeof(DirectoryEntry) + sizeof(uint64_t) - 1) /
                sizeof(uint64_t);
    if (reply.length < requiredWords) {
        return static_cast<int64_t>(ErrorCode::IoError);
    }
    const auto* source = reinterpret_cast<const DirectoryEntry*>(&reply.data[2]);
    for (size_t entry = 0; entry < count; ++entry) {
        entries[entry] = source[entry];
    }
    return static_cast<int64_t>(count);
}

bool mountFileSystem(uint64_t fs, const char* device, const char* target,
                     const char* type) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Mount);
    copyString(reinterpret_cast<char*>(request.data), device, 64);
    copyString(reinterpret_cast<char*>(request.data) + 64, target, 64);
    copyString(reinterpret_cast<char*>(request.data) + 128, type, 16);
    request.length = 18;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool unmountFileSystem(uint64_t fs, const char* target) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Unmount);
    setPath(request, target);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

bool listMount(uint64_t fs, uint32_t index, MountInfo& info) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(FsRequest::ListMounts);
    request.data[0] = index;
    request.length  = 1;
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0) {
        return false;
    }
    copyString(info.device, reinterpret_cast<const char*>(&reply.data[1]),
               sizeof(info.device));
    copyString(info.target, reinterpret_cast<const char*>(&reply.data[9]),
               sizeof(info.target));
    copyString(info.type, reinterpret_cast<const char*>(&reply.data[17]),
               sizeof(info.type));
    info.readOnly = reply.data[19] != 0;
    return true;
}

bool formatFileSystem(uint64_t fs, const char* device, const char* type) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Format);
    copyString(reinterpret_cast<char*>(request.data), device, 64);
    copyString(reinterpret_cast<char*>(request.data) + 64, type, 16);
    request.length = 10;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

}  // namespace os
