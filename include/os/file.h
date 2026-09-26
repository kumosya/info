#pragma once

#include <stddef.h>
#include <stdint.h>

#include "os/abi.h"
#include "os/protocol.h"

namespace os {

struct FileStat {
    FileType type;
    uint64_t size;
    uint32_t mode;
    uint64_t inode;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t accessTime;
    uint64_t modificationTime;
    uint64_t changeTime;
};

struct DirectoryEntry {
    FileType type;
    char name[48];
};

struct MountInfo {
    char device[64];
    char target[64];
    char type[16];
    bool readOnly;
};

struct FileOperationResult {
    uint64_t requestId;
    int64_t status;
    uint64_t transferred;
};

constexpr size_t maximumAsyncFileTransfer =
    (ipcDataWordCount - 4) * sizeof(uint64_t);

uint64_t openFile(uint64_t fs, const char* path);
bool closeFile(uint64_t fs, uint64_t descriptor);
int64_t readFile(uint64_t fs, uint64_t descriptor, void* buffer, size_t size);
int64_t writeFile(uint64_t fs, uint64_t descriptor, const void* buffer,
                  size_t size);
bool readFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset,
                   size_t size, uint64_t completionEndpoint,
                   uint64_t requestId);
bool writeFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset,
                    const void* buffer, size_t size,
                    uint64_t completionEndpoint, uint64_t requestId);
bool waitFileOperation(uint64_t completionEndpoint, FileOperationResult& result,
                       void* buffer = nullptr, size_t capacity = 0);
bool seekFile(uint64_t fs, uint64_t descriptor, uint64_t offset);
int64_t seekFilePosition(uint64_t fs, uint64_t descriptor, int64_t offset,
                         int whence);
int cloneFileDescriptors(uint64_t fs, uint64_t parentProcessId,
                         uint64_t* oldDescriptors,
                         uint64_t* newDescriptors, size_t capacity);
bool createFile(uint64_t fs, const char* path);
bool makeDirectory(uint64_t fs, const char* path);
bool removePath(uint64_t fs, const char* path);
bool truncateFile(uint64_t fs, const char* path);
bool statFile(uint64_t fs, const char* path, FileStat& stat);
bool linkFile(uint64_t fs, const char* existing, const char* newPath);
bool changeFileMode(uint64_t fs, const char* path, uint32_t mode);
bool changeFileOwner(uint64_t fs, const char* path, uint32_t uid,
                     uint32_t gid);
bool changeFileTimes(uint64_t fs, const char* path, uint64_t accessTime,
                     uint64_t modificationTime);
bool readDirectory(uint64_t fs, const char* path, uint64_t index,
                   DirectoryEntry& entry);
int64_t readDirectoryBatch(uint64_t fs, const char* path, uint64_t index,
                           DirectoryEntry* entries, size_t capacity);
bool mountFileSystem(uint64_t fs, const char* device, const char* target,
                     const char* type);
bool unmountFileSystem(uint64_t fs, const char* target);
bool listMount(uint64_t fs, uint32_t index, MountInfo& info);
bool formatFileSystem(uint64_t fs, const char* device, const char* type);

}  // namespace os
