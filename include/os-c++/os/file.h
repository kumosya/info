#ifndef _OS_FILE_H
#define _OS_FILE_H

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <sys/types.h>
#include <ctime>

#include "os/abi.h"
#include "os/protocol.h"

namespace os {

struct DirectoryEntry {
    FileType type;
    char name[256];
    uint64_t inode;
    uint64_t nextOffset;
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

class File {
   public:
    enum class Backend { Filesystem, Pipe, Terminal, SharedMemory };
    explicit File(uint64_t service = 0, const char* path = nullptr) : service_(service) { if (path) Runtime::copyString(path_, path, sizeof(path_)); }
    static File adopt(uint64_t service, uint64_t handle, const char* path, int flags) {
        File value = borrow(service, handle, path, flags); value.ownsHandle_ = true; return value;
    }
    static File adoptHandle(uint64_t handle, Backend backend, int flags) {
        File value; value.handle_=handle; value.backend_=backend; value.flags_=flags; value.ownsHandle_=true; return value;
    }
    void rebindNativeHandle(uint64_t value) { handle_=value; metadataValid_=false; }
    void setStatusFlags(int flags) { flags_=(flags_&O_ACCMODE)|(flags&~O_ACCMODE); }
    ~File();
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    static File borrow(uint64_t service, uint64_t handle, const char* path = nullptr, int flags = O_RDWR);
    static File* fromDescriptor(int descriptor);
    int toDescriptor();
    int open(const char* path, int flags, mode_t mode = 0666);
    int close();
    ssize_t read(void* buffer, size_t size);
    ssize_t write(const void* buffer, size_t size);
    ssize_t pread(void* buffer, size_t size, off_t offset);
    ssize_t pwrite(const void* buffer, size_t size, off_t offset);
    off_t lseek(off_t offset, int whence);
    int ftruncate(off_t length);
    int fsync();
    int refresh(const char* path = nullptr);
    int chmod(mode_t mode);
    int chown(uid_t uid, gid_t gid);
    int utimens(const struct timespec times[2]);
    int mkdir(const char* path, mode_t mode);
    int unlink(const char* path);
    int rmdir(const char* path);
    int truncate(const char* path, off_t length);
    int link(const char* existing, const char* target);
    int rename(const char* existing, const char* target);
    int mount(const char* source, const char* target, const char* type, unsigned long flags = 0, const void* data = nullptr);
    int umount(const char* target);
    bool listMount(uint32_t index, MountInfo& info);
    int format(const char* device, const char* type);
    bool readDirectory(const char* path, uint64_t index, DirectoryEntry& entry);
    ssize_t readDirectoryBatch(const char* path, uint64_t index, DirectoryEntry* entries, size_t count);
    int cloneDescriptors(uint64_t parent, uint64_t* oldHandles, uint64_t* newHandles, size_t capacity);
    bool readAsync(uint64_t offset, size_t size, uint64_t endpoint, uint64_t requestId);
    bool writeAsync(uint64_t offset, const void* buffer, size_t size, uint64_t endpoint, uint64_t requestId);
    static bool waitOperation(uint64_t endpoint, FileOperationResult& result, void* buffer = nullptr, size_t capacity = 0);
    uint64_t getNativeHandle() const { return handle_; }
    uint64_t releaseNativeHandle() { uint64_t result = handle_; handle_ = 0; ownsHandle_ = false; return result; }
    uint64_t getService() { return service(); }
    int getFlags() const { return flags_; }
    int getError() const { return error_; }
    const char* getPath() const { return path_; }
    FileType getType() const { ensureMetadata(); return metadata_.type; }
    uint64_t getSize() const { ensureMetadata(); return metadata_.size; }
    uint32_t getMode() const { ensureMetadata(); return metadata_.mode; }
    uint64_t getInode() const { ensureMetadata(); return metadata_.inode; }
    uint32_t getLinks() const { ensureMetadata(); return metadata_.links; }
    uint32_t getUid() const { ensureMetadata(); return metadata_.uid; }
    uint32_t getGid() const { ensureMetadata(); return metadata_.gid; }
    uint64_t getAccessTime() const { ensureMetadata(); return metadata_.accessTime; }
    uint64_t getModificationTime() const { ensureMetadata(); return metadata_.modificationTime; }
    uint64_t getChangeTime() const { ensureMetadata(); return metadata_.changeTime; }
    uint64_t getDevice() const { ensureMetadata(); return metadata_.device; }
    uint64_t getSpecialDevice() const { ensureMetadata(); return metadata_.specialDevice; }
   private:
    uint64_t service();
    void ensureMetadata() const { if (!metadataValid_) const_cast<File*>(this)->refresh(); }
    ssize_t transfer(void* buffer, size_t size, off_t offset, bool writing);
    struct Metadata {
        FileType type{};
        uint64_t size{};
        uint32_t mode{};
        uint64_t inode{};
        uint32_t links{};
        uint32_t uid{};
        uint32_t gid{};
        uint64_t accessTime{};
        uint64_t modificationTime{};
        uint64_t changeTime{};
        uint64_t device{};
        uint64_t specialDevice{};
    } metadata_{};
    uint64_t service_ = 0, handle_ = 0;
    int flags_ = 0, error_ = 0;
    char path_[384]{};
    bool ownsHandle_ = false, ownsService_ = false, metadataValid_ = false;
    Backend backend_ = Backend::Filesystem;
};
}  // namespace os
#endif
