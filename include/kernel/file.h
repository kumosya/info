#ifndef _KERNEL_FILE_H
#define _KERNEL_FILE_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/spinlock.h"
#include "os/abi.h"

namespace kernel {
class Task;
class Endpoint;
namespace posix {

class FileTable {
   public:
    static FileTable* create(Task& owner);
    bool inherit(FileTable& parent);
    void closeAll(bool notifyServer = true, bool execOnly = false);
    int64_t dispatch(uint64_t number, uint64_t a, uint64_t b, uint64_t c,
                     uint64_t d, uint64_t e, uint64_t f);
    int64_t open(const char* path, int flags, uint32_t mode);
    int64_t close(int fd);
    int64_t read(int fd, void* kernelBuffer, size_t size, bool writing = false, int64_t offset = -1);
    int64_t stat(const char* path, void* result);
    bool absolute(const char* path, char* output);
    bool copyUserPath(uint64_t address, char* output);

   private:
    enum class Kind : uint8_t { File, Directory, Pipe, Channel, Terminal, SharedMemory };
    class Description {
        friend class FileTable;
       private:
        uint32_t references = 1;
        Kind kind = Kind::File;
        int flags = 0;
        uint64_t handle = 0;
        uint64_t directoryIndex = 0;
        char path[384]{};
    };
    class Entry {
        friend class FileTable;
       private:
        Description* description = nullptr;
        uint64_t remoteId = 0;
        bool closeOnExec = false;
    };
    explicit FileTable(Task& owner) : owner_(owner) {}
    static Description* description(Kind kind, int flags, uint64_t handle, const char* path);
    static void release(Description* description);
    Entry acquire(int fd);
    int install(Description* description, uint64_t remoteId, bool closeOnExec, int minimum = 0);
    int duplicate(int fd, int target, bool exact, bool closeOnExec);
    Endpoint* service(const char* name, uint64_t& cachedHandle);
    bool call(os::IpcMessage& request, os::IpcMessage& reply);
    bool pathRequest(uint64_t operation, const char* path, os::IpcMessage& reply, uint64_t value = 0, uint64_t second = 0);
    int64_t fstat(int fd, void* result);
    int64_t seek(int fd, int64_t offset, int whence);
    int64_t directory(int fd, uint64_t address, size_t size);
    int64_t terminal(Description& description, void* buffer, size_t size, bool writing);
    int64_t pathOperation(uint64_t number, uint64_t a, uint64_t b, uint64_t c, uint64_t d);
    Task& owner_;
    SpinLock lock_;
    Entry entries_[64]{};
    bool standardClosed_[3]{};
    char cwd_[384] = "/";
    uint64_t fsHandle_ = 0, terminalHandle_ = 0;
};
}
}
#endif
