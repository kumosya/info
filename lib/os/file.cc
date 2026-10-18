#include "os/file.h"

#include "os/abi.h"
#include "os/service.h"
#include "os/terminal.h"
#include <errno.h>
#include <sys/stat.h>

namespace {

class FileSupport {
 public:
static ssize_t posixResult(int64_t result) {
    if (result >= 0) return result;
    switch (static_cast<os::ErrorCode>(result)) {
        case os::ErrorCode::InvalidArgument: return -EINVAL;
        case os::ErrorCode::InvalidHandle: return -EBADF;
        case os::ErrorCode::PermissionDenied: return -EACCES;
        case os::ErrorCode::NotFound: return -ENOENT;
        case os::ErrorCode::OutOfMemory: return -ENOMEM;
        case os::ErrorCode::WouldBlock: return -EAGAIN;
        case os::ErrorCode::NotSupported: return -ENOTSUP;
        case os::ErrorCode::Busy: return -EBUSY;
        case os::ErrorCode::Timeout: return -ETIMEDOUT;
        case os::ErrorCode::Interrupted: return -EINTR;
        case os::ErrorCode::Cancelled: return -ECANCELED;
        case os::ErrorCode::EndpointClosed: return -EPIPE;
        case os::ErrorCode::PeerDied: return -ECONNRESET;
        default: return -EIO;
    }
}

static void setPath(os::IpcMessage& message, const char* path) {
    os::Runtime::copyString(reinterpret_cast<char*>(message.data), path,
                   384);
    message.length = 48;
}

};

}  // namespace

#pragma GCC visibility push(hidden)
namespace os {
class FileProtocol {
 public:

static bool closeFile(uint64_t fs, uint64_t descriptor) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(FsRequest::Close);
    request.data[0] = descriptor;
    request.length  = 1;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static int64_t readFile(uint64_t fs, uint64_t descriptor, void* buffer, size_t size) {
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
        if (static_cast<int64_t>(count) < 0)
            return total ? static_cast<int64_t>(total) : static_cast<int64_t>(count);
        if (count > chunk || reply.length < 1 + (count + 7) / 8)
            return static_cast<int64_t>(ErrorCode::IoError);
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

static int64_t writeFile(uint64_t fs, uint64_t descriptor, const void* buffer,
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
        if (callService(fs, request, reply) != 0 || static_cast<int64_t>(reply.data[0]) <= 0 || reply.data[0] > chunk) {
            return total == 0 ? static_cast<int64_t>(ErrorCode::IoError)
                              : static_cast<int64_t>(total);
        }
        total += reply.data[0];
    }
    return static_cast<int64_t>(total);
}

static bool readFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset,
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

static bool writeFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset,
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

static bool waitFileOperation(uint64_t completionEndpoint, FileOperationResult& result,
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

static int64_t seekFilePosition(uint64_t fs, uint64_t descriptor, int64_t offset,
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

static int cloneFileDescriptors(uint64_t fs, uint64_t parentTaskId,
                         uint64_t* oldDescriptors,
                         uint64_t* newDescriptors, size_t capacity) {
    if (fs == 0 || parentTaskId == 0 || oldDescriptors == nullptr ||
        newDescriptors == nullptr || capacity == 0) {
        return -1;
    }
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::CloneDescriptors);
    request.data[0] = parentTaskId;
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

static bool createFile(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Create);
    FileSupport::setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool makeDirectory(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::MakeDirectory);
    FileSupport::setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool removePath(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Remove);
    FileSupport::setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool truncateFile(uint64_t fs, const char* path) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Truncate);
    FileSupport::setPath(request, path);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool linkFile(uint64_t fs, const char* existing, const char* newPath) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Link);
    Runtime::copyString(reinterpret_cast<char*>(request.data), existing, 384);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 384, newPath, 384);
    request.length = 96;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool changeFileMode(uint64_t fs, const char* path, uint32_t mode) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ChangeMode);
    Runtime::copyString(reinterpret_cast<char*>(request.data), path, 384);
    request.data[48] = mode;
    request.length = 49;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool changeFileOwner(uint64_t fs, const char* path, uint32_t uid,
                     uint32_t gid) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ChangeOwner);
    Runtime::copyString(reinterpret_cast<char*>(request.data), path, 384);
    request.data[48] = uid;
    request.data[49] = gid;
    request.length = 50;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool changeFileTimes(uint64_t fs, const char* path, uint64_t accessTime,
                     uint64_t modificationTime) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ChangeTimes);
    Runtime::copyString(reinterpret_cast<char*>(request.data), path, 384);
    request.data[48] = accessTime;
    request.data[49] = modificationTime;
    request.length = 50;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool readDirectory(uint64_t fs, const char* path, uint64_t index,
                   DirectoryEntry& entry) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ReadDirectory);
    FileSupport::setPath(request, path);
    request.data[48] = index;
    request.length = 49;
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0) {
        return false;
    }
    entry.type = static_cast<FileType>(reply.data[1]);
    Runtime::copyString(entry.name, reinterpret_cast<const char*>(&reply.data[2]),
               sizeof(entry.name));
    return true;
}

static int64_t readDirectoryBatch(uint64_t fs, const char* path, uint64_t index,
                           DirectoryEntry* entries, size_t capacity) {
    constexpr size_t maximumEntries =
        (ipcDataWordCount - 2) * sizeof(uint64_t) / sizeof(DirectoryEntry);
    if (fs == 0 || entries == nullptr || capacity == 0) {
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    }
    if (capacity > maximumEntries) capacity = maximumEntries;
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::ReadDirectoryBatch);
    FileSupport::setPath(request, path);
    request.data[48] = index;
    request.data[49] = capacity;
    request.length  = 50;
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

static bool mountFileSystem(uint64_t fs, const char* device, const char* target,
                     const char* type) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Mount);
    Runtime::copyString(reinterpret_cast<char*>(request.data), device, 64);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 64, target, 64);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 128, type, 16);
    request.length = 18;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool unmountFileSystem(uint64_t fs, const char* target) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Unmount);
    FileSupport::setPath(request, target);
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

static bool listMount(uint64_t fs, uint32_t index, MountInfo& info) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(FsRequest::ListMounts);
    request.data[0] = index;
    request.length  = 1;
    IpcMessage reply{};
    if (callService(fs, request, reply) != 0 || reply.data[0] != 0) {
        return false;
    }
    Runtime::copyString(info.device, reinterpret_cast<const char*>(&reply.data[1]),
               sizeof(info.device));
    Runtime::copyString(info.target, reinterpret_cast<const char*>(&reply.data[9]),
               sizeof(info.target));
    Runtime::copyString(info.type, reinterpret_cast<const char*>(&reply.data[17]),
               sizeof(info.type));
    info.readOnly = reply.data[19] != 0;
    return true;
}

static bool formatFileSystem(uint64_t fs, const char* device, const char* type) {
    IpcMessage request{};
    request.type = static_cast<uint64_t>(FsRequest::Format);
    Runtime::copyString(reinterpret_cast<char*>(request.data), device, 64);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 64, type, 16);
    request.length = 10;
    IpcMessage reply{};
    return callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

};
}  // namespace os

#pragma GCC visibility pop

namespace os {
File::~File() {
    if (ownsHandle_) close();
    if (ownsService_ && service_) closeHandle(service_);
}
File::File(File&& other) noexcept : File() { *this = static_cast<File&&>(other); }
File& File::operator=(File&& other) noexcept {
    if (this == &other) return *this;
    if (ownsHandle_) close();
    if (ownsService_ && service_) closeHandle(service_);
    service_=other.service_; handle_=other.handle_; flags_=other.flags_; error_=other.error_;
    metadata_=other.metadata_; metadataValid_=other.metadataValid_; backend_=other.backend_;
    Runtime::copyString(path_,other.path_,sizeof(path_)); ownsHandle_=other.ownsHandle_; ownsService_=other.ownsService_;
    other.service_=other.handle_=0; other.ownsHandle_=other.ownsService_=false;
    return *this;
}
uint64_t File::service() {
    if (!service_) { service_=ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(),fsServiceName,1000); ownsService_=service_!=0; }
    return service_;
}
File File::borrow(uint64_t fs, uint64_t handle, const char* path, int flags) {
    File result(fs); result.handle_=handle; result.flags_=flags;
    if (path) Runtime::copyString(result.path_,path,sizeof(result.path_));
    return result;
}
int File::open(const char* path, int flags, mode_t mode) {
    if (!path) return error_=-EFAULT;
    if (!*path) return error_=-ENOENT;
    if (Runtime::stringLength(path)>=sizeof(path_)) return error_=-ENAMETOOLONG;
    if ((flags&O_ACCMODE)>O_RDWR) return error_=-EINVAL;
    if (ownsHandle_ && close()) return error_;
    if (!service()) return error_=-ENODEV;
    if (refresh(path)==0 && (flags&O_DIRECTORY) && getType()!=FileType::Directory) return error_=-ENOTDIR;
    IpcMessage request{},reply{};
    request.type=static_cast<uint64_t>(FsRequest::Open); FileSupport::setPath(request,path);
    request.data[48]=static_cast<uint64_t>(flags); request.data[49]=mode; request.length=50;
    if (callService(service_,request,reply) || !reply.data[0])
        return error_=reply.length>1 && static_cast<int64_t>(reply.data[1])<0 ? static_cast<int>(reply.data[1]) : -EIO;
    handle_=reply.data[0]; flags_=flags; ownsHandle_=true; metadataValid_=false;
    Runtime::copyString(path_,path,sizeof(path_)); return error_=0;
}
int File::close() {
    if (backend_!=Backend::Filesystem) {
        const bool success=!handle_ || closeHandle(handle_); handle_=0; ownsHandle_=false;
        return error_=success?0:-EIO;
    }
    if (!handle_) return error_=-EBADF;
    if (!FileProtocol::closeFile(service(),handle_)) return error_=-EIO;
    handle_=0; ownsHandle_=false; metadataValid_=false; return error_=0;
}
ssize_t File::read(void* buffer, size_t size) {
    if (!size) return 0;
    if (!buffer) return -EFAULT;
    if (backend_!=Backend::Filesystem) {
        if ((flags_&O_ACCMODE)==O_WRONLY) return -EBADF;
        if (backend_==Backend::Pipe) return FileSupport::posixResult(Pipe::readPipe(handle_,buffer,size));
        if (backend_==Backend::Terminal) return FileSupport::posixResult(Terminal::read(buffer,size));
        return -EINVAL;
    }
    if (!handle_ || (flags_&O_ACCMODE)==O_WRONLY) return error_=-EBADF;
    if (!size) return 0;
    if (getType()==FileType::Directory) return error_=-EISDIR;
    if (!buffer) return error_=-EFAULT;
    return FileSupport::posixResult(FileProtocol::readFile(service(),handle_,buffer,size));
}
ssize_t File::write(const void* buffer, size_t size) {
    if (!size) return 0;
    if (!buffer) return -EFAULT;
    if (backend_!=Backend::Filesystem) {
        if ((flags_&O_ACCMODE)==O_RDONLY) return -EBADF;
        if (backend_==Backend::Pipe) return FileSupport::posixResult(Pipe::writePipe(handle_,buffer,size));
        if (backend_==Backend::Terminal) return FileSupport::posixResult(Terminal::write(buffer,size));
        return -EINVAL;
    }
    if (!handle_ || (flags_&O_ACCMODE)==O_RDONLY) return error_=-EBADF;
    if (!size) return 0;
    if (getType()==FileType::Directory) return error_=-EISDIR;
    if (!buffer) return error_=-EFAULT;
    if ((flags_&O_APPEND) && lseek(0,2)<0) return error_;
    const ssize_t count=FileSupport::posixResult(FileProtocol::writeFile(service(),handle_,buffer,size)); metadataValid_=false; return count;
}
ssize_t File::transfer(void* buffer, size_t size, off_t offset, bool writing) {
    if (backend_!=Backend::Filesystem) return -ESPIPE;
    if (!handle_ || (writing ? (flags_&O_ACCMODE)==O_RDONLY : (flags_&O_ACCMODE)==O_WRONLY)) return error_=-EBADF;
    if (offset<0 || (!buffer && size)) return error_=-EINVAL;
    size_t total=0;
    while (total<size) {
        const size_t maximum=(ipcDataWordCount-3)*8;
        const size_t chunk=size-total<maximum?size-total:maximum;
        IpcMessage request{},reply{}; request.type=static_cast<uint64_t>(writing?FsRequest::WriteAt:FsRequest::ReadAt);
        request.data[0]=handle_; request.data[1]=offset+total; request.data[2]=chunk; request.length=3;
        if (writing) { Runtime::copyBytes(&request.data[3],static_cast<unsigned char*>(buffer)+total,chunk); request.length+=(chunk+7)/8; }
        if (callService(service(),request,reply)) return total?static_cast<ssize_t>(total):static_cast<ssize_t>(-EIO);
        const int64_t count=static_cast<int64_t>(reply.data[0]);
        if (count<0 || static_cast<uint64_t>(count)>chunk || (!writing && reply.length<1+(count+7)/8))
            return total?static_cast<ssize_t>(total):static_cast<ssize_t>(-EIO);
        if (!writing) Runtime::copyBytes(static_cast<unsigned char*>(buffer)+total,&reply.data[1],count);
        total+=count; if (static_cast<size_t>(count)<chunk) break;
    }
    if (writing) metadataValid_=false;
    return total;
}
ssize_t File::pread(void* buffer,size_t size,off_t offset) { return transfer(buffer,size,offset,false); }
ssize_t File::pwrite(const void* buffer,size_t size,off_t offset) { return transfer(const_cast<void*>(buffer),size,offset,true); }
off_t File::lseek(off_t offset,int whence) {
    if (backend_!=Backend::Filesystem) return -ESPIPE;
    if (!handle_) return error_=-EBADF;
    return FileSupport::posixResult(FileProtocol::seekFilePosition(service(),handle_,offset,whence));
}
int File::refresh(const char* path) {
    if (backend_!=Backend::Filesystem) {
        metadata_={}; metadata_.type=FileType::File;
        metadata_.mode=(backend_==Backend::Pipe?0010000:backend_==Backend::Terminal?0020000:0100000)|0600;
        metadata_.links=1; metadataValid_=true; return error_=0;
    }
    IpcMessage request{},reply{};
    if (path) {
        if (!*path || Runtime::stringLength(path)>=sizeof(path_)) return error_=-ENAMETOOLONG;
        Runtime::copyString(path_,path,sizeof(path_));
    }
    if (handle_ && !path) { request.type=static_cast<uint64_t>(FsRequest::Fstat); request.data[0]=handle_; request.length=1; }
    else { if (!*path_) return error_=-ENOENT; request.type=static_cast<uint64_t>(FsRequest::Stat); FileSupport::setPath(request,path_); }
    if (!service() || callService(service_,request,reply) || reply.data[0]) { metadataValid_=false; return error_=-ENOENT; }
    metadata_.type=static_cast<FileType>(reply.data[1]); metadata_.size=reply.data[2]; metadata_.mode=reply.data[3];
    metadata_.inode=reply.data[4]; metadata_.links=reply.data[5]; metadata_.uid=reply.data[6]; metadata_.gid=reply.data[7];
    metadata_.accessTime=reply.data[8]; metadata_.modificationTime=reply.data[9]; metadata_.changeTime=reply.data[10];
    metadata_.device=reply.length>=13?reply.data[11]:0; metadata_.specialDevice=reply.length>=13?reply.data[12]:0;
    metadataValid_=true; return error_=0;
}
int File::chmod(mode_t mode) { metadataValid_=false; return FileProtocol::changeFileMode(service(),path_,mode)?0:-EACCES; }
int File::chown(uid_t uid,gid_t gid) { metadataValid_=false; return FileProtocol::changeFileOwner(service(),path_,uid,gid)?0:-EPERM; }
int File::utimens(const timespec times[2]) {
    const uint64_t now=Clock::getWallClockTime()/1000000000ULL;
    uint64_t values[2];
    for (unsigned i=0;i<2;++i) {
        if (!times || times[i].tv_nsec==UTIME_NOW) values[i]=now;
        else if (times[i].tv_nsec==UTIME_OMIT) {
            if(refresh()) return error_;
            values[i]=i?getModificationTime():getAccessTime();
        } else {
            if(times[i].tv_sec<0 || times[i].tv_nsec<0 || times[i].tv_nsec>=1000000000L) return -EINVAL;
            values[i]=times[i].tv_sec;
        }
        if(values[i]>UINT32_MAX) return -EOVERFLOW;
    }
    metadataValid_=false;
    return FileProtocol::changeFileTimes(service(),path_,values[0],values[1])?0:-EIO;
}
int File::mkdir(const char* path,mode_t mode) {
    if (!FileProtocol::makeDirectory(service(),path)) return -EIO;
    return FileProtocol::changeFileMode(service(),path,mode)?0:-EIO;
}
int File::unlink(const char* path) { return FileProtocol::removePath(service(),path)?0:-EIO; }
int File::rmdir(const char* path) { return unlink(path); }
int File::truncate(const char* path,off_t length) { if (length<0) return -EINVAL; if (length) return -ENOSYS; return FileProtocol::truncateFile(service(),path)?0:-EIO; }
int File::ftruncate(off_t length) { metadataValid_=false; return handle_?truncate(path_,length):-EBADF; }
int File::fsync() { return handle_?-ENOSYS:-EBADF; }
int File::link(const char* existing,const char* target) { return FileProtocol::linkFile(service(),existing,target)?0:-EIO; }
int File::rename(const char* existing,const char* target) {
    if (!existing || !target || Runtime::stringLength(existing)>=384 || Runtime::stringLength(target)>=384) return -EINVAL;
    IpcMessage request{},reply{}; request.type=static_cast<uint64_t>(FsRequest::Rename);
    Runtime::copyString(reinterpret_cast<char*>(request.data),existing,384); Runtime::copyString(reinterpret_cast<char*>(request.data)+384,target,384); request.length=96;
    return callService(service(),request,reply)==0 && !reply.data[0]?0:-EIO;
}
int File::mount(const char* source,const char* target,const char* type,unsigned long flags,const void* data) {
    if (flags || data) return -ENOTSUP;
    return FileProtocol::mountFileSystem(service(),source,target,type)?0:-EIO;
}
int File::umount(const char* target) { return FileProtocol::unmountFileSystem(service(),target)?0:-EIO; }
bool File::listMount(uint32_t index,MountInfo& info) { return FileProtocol::listMount(service(),index,info); }
int File::format(const char* device,const char* type) { return FileProtocol::formatFileSystem(service(),device,type)?0:-EIO; }
bool File::readDirectory(const char* path,uint64_t index,DirectoryEntry& entry) { return FileProtocol::readDirectory(service(),path,index,entry); }
ssize_t File::readDirectoryBatch(const char* path,uint64_t index,DirectoryEntry* entries,size_t count) { return FileProtocol::readDirectoryBatch(service(),path,index,entries,count); }
int File::cloneDescriptors(uint64_t parent,uint64_t* oldHandles,uint64_t* newHandles,size_t capacity) { return FileProtocol::cloneFileDescriptors(service(),parent,oldHandles,newHandles,capacity); }
bool File::readAsync(uint64_t offset,size_t size,uint64_t endpoint,uint64_t requestId) { return FileProtocol::readFileAsync(service(),handle_,offset,size,endpoint,requestId); }
bool File::writeAsync(uint64_t offset,const void* data,size_t size,uint64_t endpoint,uint64_t requestId) { return FileProtocol::writeFileAsync(service(),handle_,offset,data,size,endpoint,requestId); }
bool File::waitOperation(uint64_t endpoint,FileOperationResult& result,void* buffer,size_t capacity) { return FileProtocol::waitFileOperation(endpoint,result,buffer,capacity); }
}  // namespace os
