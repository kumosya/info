#include "kernel/file.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/new.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "os/protocol.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

namespace kernel::posix {
namespace {
size_t length(const char* value) { size_t count = 0; while (value[count]) ++count; return count; }
void copy(char* output, const char* input, size_t size) {
    size_t i = 0; for (; i + 1 < size && input[i]; ++i) output[i] = input[i]; output[i] = 0;
}
void decodeStat(const os::IpcMessage& reply, struct stat& status) {
    status = {};
    status.st_mode = reply.data[3]; status.st_size = reply.data[2]; status.st_ino = reply.data[4];
    status.st_nlink = reply.data[5]; status.st_uid = reply.data[6]; status.st_gid = reply.data[7];
    status.st_atim.tv_sec = reply.data[8]; status.st_mtim.tv_sec = reply.data[9]; status.st_ctim.tv_sec = reply.data[10];
    status.st_dev = reply.data[11]; status.st_rdev = reply.data[12];
    status.st_blksize = 4096; status.st_blocks = (status.st_size + 511) / 512;
}
}

FileTable* FileTable::create(Task& owner) {
    void* memory = heap::kernelAllocate(sizeof(FileTable));
    return memory ? new (memory) FileTable(owner) : nullptr;
}
FileTable::Description* FileTable::description(Kind kind, int flags, uint64_t handle, const char* path) {
    void* memory = heap::kernelAllocate(sizeof(Description));
    if (!memory) return nullptr;
    auto* value = new (memory) Description;
    value->kind = kind; value->flags = flags; value->handle = handle;
    if (path) copy(value->path, path, sizeof(value->path));
    return value;
}
void FileTable::release(Description* value) {
    if (value && __atomic_sub_fetch(&value->references, 1U, __ATOMIC_ACQ_REL) == 0) heap::kernelFree(value);
}
FileTable::Entry FileTable::acquire(int fd) {
    if (fd < 0 || fd >= 64) return {};
    InterruptSpinLockGuard guard(lock_);
    if (!entries_[fd].description && fd < 3 && !standardClosed_[fd]) {
        const uint64_t handle = owner_.standardHandle(fd);
        void* object = nullptr; HandleType type = HandleType::None; uint32_t rights = 0;
        Kind kind = Kind::Terminal;
        if (handle) {
            if (!owner_.handles().inspect(handle, &object, &type, &rights)) return {};
            kind = type == HandleType::Channel ? Kind::Channel : Kind::Pipe;
        }
        entries_[fd].description = description(kind, fd ? O_WRONLY : O_RDONLY, handle, "/dev/tty");
    }
    Entry result = entries_[fd];
    if (result.description) __atomic_add_fetch(&result.description->references, 1U, __ATOMIC_RELAXED);
    return result;
}
int FileTable::install(Description* value, uint64_t remote, bool closeOnExec, int minimum) {
    InterruptSpinLockGuard guard(lock_);
    for (int fd = minimum < 0 ? 0 : minimum; fd < 64; ++fd) {
        if (entries_[fd].description || (fd < 3 && !standardClosed_[fd])) continue;
        entries_[fd].description = value; entries_[fd].remoteId = remote; entries_[fd].closeOnExec = closeOnExec;
        return fd;
    }
    return -24;
}
Endpoint* FileTable::service(const char* name, uint64_t& cached) {
    uint64_t handle = __atomic_load_n(&cached, __ATOMIC_ACQUIRE);
    if (!handle) {
        auto* registry = static_cast<Endpoint*>(owner_.handles().get(owner_.bootstrapHandle(), HandleType::Endpoint, endpointRightSend));
        if (!registry) return nullptr;
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::RegistryRequest::Lookup);
        copy(reinterpret_cast<char*>(request.data), name, 64); request.length = 8;
        if (registry->call(&request, &reply) || reply.data[0] || reply.handleCount != 1) return nullptr;
        uint64_t expected = 0;
        if (__atomic_compare_exchange_n(&cached, &expected, reply.handles[0], false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
            handle = reply.handles[0];
        else { owner_.handles().close(reply.handles[0]); handle = expected; }
    }
    return static_cast<Endpoint*>(owner_.handles().get(handle, HandleType::Endpoint, endpointRightSend));
}
bool FileTable::call(os::IpcMessage& request, os::IpcMessage& reply) {
    Endpoint* endpoint = service("fs", fsHandle_);
    return endpoint && endpoint->call(&request, &reply) == 0;
}
bool FileTable::pathRequest(uint64_t operation, const char* path, os::IpcMessage& reply, uint64_t value, uint64_t second) {
    os::IpcMessage request{};
    request.type = operation; copy(reinterpret_cast<char*>(request.data), path, 384);
    request.data[48] = value; request.data[49] = second; request.length = 50;
    return call(request, reply);
}
bool FileTable::copyUserPath(uint64_t address, char* output) {
    for (size_t i = 0; i < 384; ++i) {
        if (!syscall::copyFromUser(output + i, reinterpret_cast<const void*>(address + i), 1)) return false;
        if (!output[i]) return true;
    }
    return false;
}
bool FileTable::absolute(const char* path, char* output) {
    if (!*path) return false;
    char joined[384]{};
    size_t used = 0;
    if (*path != '/') {
        InterruptSpinLockGuard guard(lock_);
        copy(joined, cwd_, sizeof(joined)); used = length(joined);
        if (used + 1 >= sizeof(joined)) return false;
        joined[used++] = '/';
    }
    if (length(path) >= sizeof(joined) - used) return false;
    copy(joined + used, path, sizeof(joined) - used);
    output[0] = '/'; size_t write = 1, read = 0;
    while (joined[read]) {
        while (joined[read] == '/') ++read;
        const size_t start = read;
        while (joined[read] && joined[read] != '/') ++read;
        const size_t count = read - start;
        if (!count || (count == 1 && joined[start] == '.')) continue;
        if (count == 2 && joined[start] == '.' && joined[start+1] == '.') {
            while (write > 1 && output[write-1] != '/') --write;
            if (write > 1) --write;
        } else {
            if (count > 255) return false;
            if (write > 1) output[write++] = '/';
            for (size_t i = 0; i < count; ++i) output[write++] = joined[start+i];
        }
    }
    output[write] = 0; return true;
}
int64_t FileTable::stat(const char* path, void* output) {
    os::IpcMessage reply{};
    if (!pathRequest(static_cast<uint64_t>(os::FsRequest::Stat), path, reply)) return -5;
    if (reply.data[0]) return -2;
    decodeStat(reply, *static_cast<struct stat*>(output)); return 0;
}
int64_t FileTable::open(const char* path, int flags, uint32_t mode) {
    char absolutePath[384]; if (!absolute(path, absolutePath)) return -36;
    struct stat status{};
    bool exists = stat(absolutePath, &status) == 0;
    if (exists && (flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) return -17;
    os::IpcMessage reply{};
    if (!exists && (flags & O_CREAT)) {
        if (!pathRequest(static_cast<uint64_t>(os::FsRequest::Create), absolutePath, reply) || reply.data[0]) return -13;
        if (!pathRequest(static_cast<uint64_t>(os::FsRequest::ChangeMode), absolutePath, reply, mode & 07777) || reply.data[0]) return -13;
        exists = stat(absolutePath, &status) == 0;
    }
    if (!exists) return -2;
    const bool directory = S_ISDIR(status.st_mode);
    if ((flags & O_DIRECTORY) && !directory) return -20;
    if (directory && (flags & O_ACCMODE) != O_RDONLY) return -21;
    const uint32_t uid = owner_.effectiveUid(), gid = owner_.effectiveGid();
    const unsigned shift = uid == status.st_uid ? 6 : gid == status.st_gid ? 3 : 0;
    const unsigned access = (flags & O_ACCMODE) == O_RDWR ? 6 : (flags & O_ACCMODE) == O_WRONLY ? 2 : 4;
    if (uid && ((status.st_mode >> shift) & access) != access) return -13;
    if (!directory && (flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY &&
        (!pathRequest(static_cast<uint64_t>(os::FsRequest::Truncate), absolutePath, reply) || reply.data[0])) return -13;
    uint64_t remote = 0;
    if (!directory) {
        if (!pathRequest(static_cast<uint64_t>(os::FsRequest::Open), absolutePath, reply, flags) || !reply.data[0]) return -13;
        remote = reply.data[0];
    }
    Description* value = description(directory ? Kind::Directory : Kind::File, flags & ~O_CLOEXEC, 0, absolutePath);
    if (!value) return -12;
    const int fd = install(value, remote, (flags & O_CLOEXEC) != 0);
    if (fd < 0) {
        release(value);
        if (remote) { os::IpcMessage request{}; request.type = static_cast<uint64_t>(os::FsRequest::Close); request.data[0] = remote; request.length = 1; call(request, reply); }
    }
    return fd;
}
int64_t FileTable::close(int fd) {
    if (fd < 0 || fd >= 64) return -9;
    Entry entry = acquire(fd); if (!entry.description) return -9;
    bool last = true;
    {
        InterruptSpinLockGuard guard(lock_);
        if (entries_[fd].description != entry.description || entries_[fd].remoteId != entry.remoteId) {
            release(entry.description); return -9;
        }
        entries_[fd] = {}; if (fd < 3) standardClosed_[fd] = true;
        for (const Entry& other : entries_)
            if (other.description == entry.description && other.remoteId == entry.remoteId) { last = false; break; }
    }
    if (last) {
        if (entry.remoteId) {
            os::IpcMessage request{}, reply{};
            request.type = static_cast<uint64_t>(os::FsRequest::Close); request.data[0] = entry.remoteId; request.length = 1;
            call(request, reply);
        } else if (entry.description->handle) owner_.handles().close(entry.description->handle);
    }
    release(entry.description); release(entry.description);
    return 0;
}

void FileTable::closeAll(bool notifyServer, bool execOnly) {
    for (int fd = 0; fd < 64; ++fd) {
        bool present = false;
        {
            InterruptSpinLockGuard guard(lock_);
            present = entries_[fd].description && (!execOnly || entries_[fd].closeOnExec);
        }
        if (!present) continue;
        if (notifyServer) close(fd);
        else {
            Entry entry;
            { InterruptSpinLockGuard guard(lock_); entry = entries_[fd]; entries_[fd] = {}; }
            release(entry.description);
        }
    }
}

bool FileTable::inherit(FileTable& parent) {
    {
        InterruptSpinLockGuard guard(parent.lock_);
        copy(cwd_, parent.cwd_, sizeof(cwd_)); fsHandle_ = parent.fsHandle_; terminalHandle_ = parent.terminalHandle_;
        for (int fd = 0; fd < 64; ++fd) {
            entries_[fd] = parent.entries_[fd];
            if (entries_[fd].description) __atomic_add_fetch(&entries_[fd].description->references, 1U, __ATOMIC_RELAXED);
        }
        for (unsigned i = 0; i < 3; ++i) standardClosed_[i] = parent.standardClosed_[i];
    }
    if (!fsHandle_) return true;
    os::IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::FsRequest::CloneForChild);
    request.data[0] = owner_.id(); request.length = 1;
    if (!parent.call(request, reply) || reply.data[0] || reply.data[1] > 64 || reply.length < 2 + reply.data[1] * 2) return false;
    for (Entry& entry : entries_) {
        if (!entry.remoteId) continue;
        bool found = false;
        for (unsigned i = 0; i < reply.data[1]; ++i) if (entry.remoteId == reply.data[2 + i * 2]) {
            entry.remoteId = reply.data[3 + i * 2]; found = true; break;
        }
        if (!found) return false;
    }
    return true;
}

int FileTable::duplicate(int fd, int target, bool exact, bool closeOnExec) {
    if (target < 0 || target >= 64) return -9;
    Entry source = acquire(fd);
    if (!source.description) return -9;
    if (exact && fd == target) { release(source.description); return closeOnExec ? -22 : fd; }
    if (exact) {
        // close may wait for the server; retain a reference to the source.
        close(target);
        InterruptSpinLockGuard guard(lock_);
        if (entries_[target].description) { release(source.description); return -16; }
        entries_[target] = source; entries_[target].closeOnExec = closeOnExec;
        if (target < 3) standardClosed_[target] = false;
        return target;
    }
    const int result = install(source.description, source.remoteId, closeOnExec, target);
    if (result < 0) release(source.description);
    return result;
}

int64_t FileTable::terminal(Description& value, void* buffer, size_t size, bool writing) {
    Endpoint* endpoint = service("terminal", terminalHandle_);
    if (!endpoint) return -6;
    while (true) {
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(writing ? os::TerminalRequest::Write : os::TerminalRequest::Read);
        request.data[0] = owner_.terminalId();
        const size_t count = size < (writing ? 512U : 48U) ? size : (writing ? 512U : 48U);
        request.data[1] = count; request.length = 2;
        if (writing) {
            const auto* input = static_cast<const uint8_t*>(buffer);
            for (size_t i = 0; i < count; ++i) reinterpret_cast<uint8_t*>(&request.data[2])[i] = input[i];
            request.length += (count + 7) / 8;
        }
        if (endpoint->call(&request, &reply)) return -5;
        if (static_cast<int64_t>(reply.data[0]) == -6 && !writing) {
            if (value.flags & O_NONBLOCK) return -11;
            if (owner_.pendingSignals(Thread::getCurrent())) return -4;
            Thread::sleepCurrent(8); continue;
        }
        if (reply.data[0] || reply.data[1] > count) return -5;
        if (!writing) {
            if (reply.length < 2 + (reply.data[1] + 7) / 8) return -5;
            auto* output = static_cast<uint8_t*>(buffer);
            for (size_t i = 0; i < reply.data[1]; ++i) output[i] = reinterpret_cast<const uint8_t*>(&reply.data[2])[i];
        }
        return reply.data[1];
    }
}

int64_t FileTable::read(int fd, void* buffer, size_t size, bool writing, int64_t offset) {
    if (!size) return 0;
    Entry entry = acquire(fd);
    Description* value = entry.description;
    if (!value) return -9;
    int64_t result = -9;
    if ((value->flags & O_ACCMODE) == (writing ? O_RDONLY : O_WRONLY)) { release(value); return -9; }
    if (size > 4096) size = 4096;
    if (value->kind == Kind::Directory) result = -21;
    else if (offset >= 0 && value->kind != Kind::File) result = -29;
    else if (value->kind == Kind::Pipe || value->kind == Kind::Channel) {
        result = writing ? Pipe::write(owner_, value->handle, buffer, size)
                         : Pipe::read(owner_, value->handle, buffer, size);
        if (result < 0) result = result == -6 ? -11 : result == -10 ? -4 : writing && result == -11 ? -32 : -5;
    } else if (value->kind == Kind::Terminal) result = terminal(*value, buffer, size, writing);
    else if (value->kind == Kind::File) {
        os::IpcMessage request{}, reply{};
        const bool positioned = offset >= 0;
        request.type = static_cast<uint64_t>(positioned ? (writing ? os::FsRequest::WriteAt : os::FsRequest::ReadAt) :
                                                        (writing ? os::FsRequest::Write : os::FsRequest::Read));
        request.data[0] = entry.remoteId;
        const size_t words = positioned ? 3 : 2;
        if (positioned) { request.data[1] = offset; request.data[2] = size; }
        else request.data[1] = size;
        request.length = words;
        if (writing) {
            const auto* input = static_cast<const uint8_t*>(buffer);
            for (size_t i = 0; i < size; ++i) reinterpret_cast<uint8_t*>(&request.data[words])[i] = input[i];
            request.length += (size + 7) / 8;
        }
        if (writing && !positioned && (value->flags & O_APPEND)) seek(fd, 0, 2);
        if (!call(request, reply)) result = -5;
        else {
            result = static_cast<int64_t>(reply.data[0]);
            if (result < 0) result = result == -3 ? -13 : -5;
            else if (static_cast<uint64_t>(result) > size || (!writing && reply.length < 1 + (result + 7) / 8)) result = -5;
            else if (!writing) {
                auto* output = static_cast<uint8_t*>(buffer);
                for (int64_t i = 0; i < result; ++i) output[i] = reinterpret_cast<const uint8_t*>(&reply.data[1])[i];
            }
        }
    }
    release(value);
    if (result == -32) owner_.deliverSignal(13);
    return result;
}

int64_t FileTable::seek(int fd, int64_t offset, int whence) {
    Entry entry = acquire(fd); if (!entry.description) return -9;
    if (entry.description->kind == Kind::Directory) {
        if (whence != 0 || offset < 0) { release(entry.description); return -22; }
        __atomic_store_n(&entry.description->directoryIndex, static_cast<uint64_t>(offset), __ATOMIC_RELEASE);
        release(entry.description); return offset;
    }
    if (entry.description->kind != Kind::File) { release(entry.description); return -29; }
    os::IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::FsRequest::Seek);
    request.data[0] = entry.remoteId; request.data[1] = offset; request.data[2] = whence; request.length = 3;
    const int64_t result = call(request, reply) && !reply.data[0] ? static_cast<int64_t>(reply.data[1]) : -22;
    release(entry.description); return result;
}

int64_t FileTable::fstat(int fd, void* output) {
    Entry entry = acquire(fd); if (!entry.description) return -9;
    int64_t result = 0;
    if (entry.description->kind == Kind::File) {
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::FsRequest::Fstat); request.data[0] = entry.remoteId; request.length = 1;
        if (!call(request, reply) || reply.data[0]) result = -5;
        else decodeStat(reply, *static_cast<struct stat*>(output));
    } else if (entry.description->kind == Kind::Directory) result = stat(entry.description->path, output);
    else {
        auto& status = *static_cast<struct stat*>(output);
        status = {}; status.st_mode = entry.description->kind == Kind::Pipe ? 0010600 : 0020666;
        status.st_uid = owner_.effectiveUid(); status.st_gid = owner_.effectiveGid(); status.st_nlink = 1; status.st_blksize = 4096;
    }
    release(entry.description); return result;
}
}
