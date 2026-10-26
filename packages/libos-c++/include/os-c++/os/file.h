#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcntl.h>

#include <os/ipc.h>
#include <sys/types.h>
namespace os {
class Fs;
class Mount {
   public:
    Mount();
    ~Mount();
    Mount(const Mount&)            = delete;
    Mount& operator=(const Mount&) = delete;
    Mount(Mount&& other) noexcept;
    Mount& operator=(Mount&& other) noexcept;
    int create(const char* source, const char* target, const char* type,
               unsigned long flags = 0, const void* data = nullptr);
    int close();
    const char* getDevice() const;
    const char* getTarget() const;
    const char* getType() const;
    bool getReadOnly() const;
    bool getMounted() const;
    bool getOwned() const;
    int getError() const;

   private:
    friend class Fs;
    uint64_t service_{};
    char device_[64]{}, target_[64]{}, type_[16]{};
    bool readOnly_{}, mounted_{}, owned_{};
    int error_{};
};
class File {
   public:
    enum class Type : uint64_t { None = 0, File = 1, Directory = 2 };
    enum class Backend : int {
        Filesystem   = 0,
        Pipe         = 1,
        Terminal     = 2,
        SharedMemory = 3
    };
    class Opt {
       public:
        Opt();
        ~Opt();
        Opt(const Opt&)            = delete;
        Opt& operator=(const Opt&) = delete;
        Opt(Opt&& other) noexcept;
        Opt& operator=(Opt&& other) noexcept;
        int poll();
        int wait(uint64_t deadline);
        int close();
        uint64_t getId() const;
        int64_t getStatus() const;
        uint64_t getTransferred() const;
        bool getCompleted() const;
        const void* getData() const;

       private:
        friend class File;
        Ipc endpoint_;
        uint64_t id_{}, transferred_{};
        int64_t status_{};
        size_t requested_{};
        bool submitted_{}, completed_{}, reading_{};
        unsigned char data_[4088]{};
    };
    File();
    ~File();
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&)            = delete;
    File& operator=(const File&) = delete;
    int open(const char* path, int flags, mode_t mode = 0666);
    int close();
    int adopt(uint64_t service, uint64_t handle, const char* path, int flags);
    int borrow(uint64_t service, uint64_t handle, const char* path, int flags);
    int adoptHandle(uint64_t handle, Backend backend, int flags);
    uint64_t releaseHandle();
    ssize_t read(void* buffer, size_t size);
    ssize_t write(const void* buffer, size_t size);
    ssize_t pread(void* buffer, size_t size, off_t offset);
    ssize_t pwrite(const void* buffer, size_t size, off_t offset);
    off_t seek(off_t offset, int whence);
    int truncate(off_t length);
    int flush();
    int refresh(const char* path = nullptr);
    int readAsync(uint64_t offset, size_t size, Opt& operation);
    int writeAsync(uint64_t offset, const void* buffer, size_t size,
                   Opt& operation);
    uint64_t getHandle() const;
    int getFlags() const;
    int getError() const;
    const char* getPath() const;
    Type getType() const;
    uint64_t getSize() const;
    uint32_t getMode() const;
    uint64_t getInode() const;
    uint32_t getLinkCount() const;
    uint32_t getUid() const;
    uint32_t getGid() const;
    uint64_t getAccessTime() const;
    uint64_t getModificationTime() const;
    uint64_t getChangeTime() const;
    uint64_t getDevice() const;
    uint64_t getSpecialDevice() const;
    bool getOpened() const;
    bool getSnapshotValid() const;
    Backend getBackend() const;
    size_t getMaximumAsyncTransfer() const;
    int setMode(mode_t mode);
    int setOwner(uid_t uid, gid_t gid);
    int setTimes(const struct timespec times[2]);
    void setStatusFlags(int flags);

   private:
    friend class Fs;
    uint64_t service();
    ssize_t transfer(void* buffer, size_t size, off_t offset, bool writing);
    int submit(uint64_t offset, const void* buffer, size_t size, Opt& operation,
               bool writing);
    Type type_{};
    uint64_t size_{};
    uint32_t mode_{};
    uint64_t inode_{};
    uint32_t links_{}, uid_{}, gid_{};
    uint64_t accessTime_{}, modificationTime_{}, changeTime_{}, device_{},
        specialDevice_{};
    uint64_t service_{}, handle_{};
    int flags_{}, error_{};
    char path_[384]{};
    bool ownsHandle_{}, ownsService_{}, metadataValid_{}, opened_{};
    Backend backend_{Backend::Filesystem};
};
class Fs {
   public:
    Fs();
    ~Fs();
    Fs(const Fs&)            = delete;
    Fs& operator=(const Fs&) = delete;
    int open(const char* path, int flags, mode_t mode, File& file);
    int query(const char* path, File& file);
    int removeFile(const char* path);
    int rename(const char* existing, const char* target);
    int link(const char* existing, const char* target);
    int queryMount(uint32_t index, Mount& mount);
    int format(const char* device, const char* type);

   private:
    uint64_t service_{};
    uint64_t service();
};
}  // namespace os
