#include <cerrno>
#include <unistd.h>

#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include <os/console.h>
#include <os/file.h>
#include <os/service.h>
#include <os/mm.h>
#include <sys/stat.h>
namespace os {
namespace {
constexpr size_t asyncCapacity = 4088;
uint64_t nextOperation         = 1;
int64_t bufferedRead(uint64_t endpoint,uint64_t descriptor,void* output,size_t size,int64_t offset) {
    constexpr uint64_t capacity=65536,base=0x3000000000ULL;
    static uint32_t slots=0;
    const auto unsupported=static_cast<int>(ErrorCode::NotSupported);
    IpcMessage probe{},features{};probe.type=uint64_t(FsRequest::ReadBuffer);
    if(callService(endpoint,probe,features) || features.length!=2 || features.data[1]!=0x4653425546464552ULL)return unsupported;
    unsigned slot=16;
    for(unsigned i=0;i<16;++i)if(!(__atomic_fetch_or(&slots,1U<<i,__ATOMIC_ACQ_REL)&(1U<<i))){slot=i;break;}
    if(slot==16)return unsupported;
    struct Mapping {unsigned slot;uint64_t address=0;uint32_t* slots;~Mapping(){if(address)detail::MemoryControl().unmapTarget(address,65536);__atomic_fetch_and(slots,~(1U<<slot),__ATOMIC_RELEASE);}} mapping{slot,0,&slots};
    Mem::Page page;
    if(page.create(4,0))return unsupported;
    const uint64_t address=base+slot*capacity;
    if(detail::MemoryControl().mapPages(page.getHandle(),address,capacity,0,1)!=address)return unsupported;
    mapping.address=address;
    size_t total=0;
    while(total<size) {
        const auto chunk=size-total<capacity?size-total:capacity;
        IpcMessage request{},reply{};request.type=uint64_t(FsRequest::ReadBuffer);request.length=3;
        request.data[0]=descriptor;request.data[1]=offset<0?UINT64_MAX:uint64_t(offset)+total;request.data[2]=chunk;
        request.handleCount=1;request.handles[0]=page.getHandle();
        const auto status=callService(endpoint,request,reply);
        if(status)return total?int64_t(total):status;
        const auto count=int64_t(reply.data[0]);
        if(reply.length!=2 || reply.data[1]!=0x4653425546464552ULL || count>int64_t(chunk))return total?int64_t(total):static_cast<int>(ErrorCode::IoError);
        if(count<0)return total?int64_t(total):count;
        Runtime::copyBytes(static_cast<uint8_t*>(output)+total,reinterpret_cast<const void*>(address),size_t(count));
        total+=size_t(count);if(size_t(count)<chunk)break;
    }
    return int64_t(total);
}
int error(ErrorCode value) { return static_cast<int>(value); }
int result(uint64_t endpoint, IpcMessage& request, IpcMessage& reply) {
    if (!endpoint) return error(ErrorCode::NotFound);
    const int status = callService(endpoint, request, reply);
    if (status) return status;
    if (!reply.length) return error(ErrorCode::IoError);
    return static_cast<int64_t>(reply.data[0]) > 0
               ? error(ErrorCode::IoError)
               : static_cast<int>(reply.data[0]);
}
bool terminated(const char* text, size_t size) {
    for (size_t i = 0; i < size; ++i)
        if (!text[i]) return true;
    return false;
}
int openError(int64_t value) {
    switch (value) {
        case -ENOENT:
            return error(ErrorCode::NotFound);
        case -EACCES:
        case -EPERM:
            return error(ErrorCode::PermissionDenied);
        case -ENOMEM:
            return error(ErrorCode::OutOfMemory);
        case -EINVAL:
        case -ENOTDIR:
        case -EISDIR:
            return error(ErrorCode::InvalidArgument);
        case -EBUSY:
        case -EEXIST:
            return error(ErrorCode::Busy);
        default:
            return error(ErrorCode::IoError);
    }
}
bool gone(int status) {
    return status == error(ErrorCode::InvalidHandle) ||
           status == error(ErrorCode::PeerDied) ||
           status == error(ErrorCode::EndpointClosed);
}
int closeEndpoint(uint64_t& endpoint) {
    if (!endpoint) return 0;
    Ipc::Handle handle(endpoint, false);
    const int status = handle.close();
    if (status && !gone(status)) return status;
    endpoint = 0;
    return 0;
}
int pathRequest(uint64_t endpoint, FsRequest operation, const char* path,
                const char* target = nullptr) {
    char from[384], to[384];
    if (!Runtime::absolutePath(path, from, sizeof(from)) ||
        (target && !Runtime::absolutePath(target, to, sizeof(to))))
        return error(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(operation);
    request.length = target ? 96 : 48;
    Runtime::copyString(reinterpret_cast<char*>(request.data), from, 384);
    if (target)
        Runtime::copyString(reinterpret_cast<char*>(request.data) + 384, to,
                            384);
    return result(endpoint, request, reply);
}
}  // namespace
File::File() = default;
File::~File() { close(); }
File::File(File&& other) noexcept { *this = static_cast<File&&>(other); }
File& File::operator=(File&& other) noexcept {
    if (this == &other) return *this;
    if (opened_ || service_) {
        error_ = error(ErrorCode::Busy);
        return *this;
    }
    service_          = other.service_;
    handle_           = other.handle_;
    flags_            = other.flags_;
    error_            = other.error_;
    type_             = other.type_;
    size_             = other.size_;
    mode_             = other.mode_;
    inode_            = other.inode_;
    links_            = other.links_;
    uid_              = other.uid_;
    gid_              = other.gid_;
    accessTime_       = other.accessTime_;
    modificationTime_ = other.modificationTime_;
    changeTime_       = other.changeTime_;
    device_           = other.device_;
    specialDevice_    = other.specialDevice_;
    metadataValid_    = other.metadataValid_;
    backend_          = other.backend_;
    ownsHandle_       = other.ownsHandle_;
    ownsService_      = other.ownsService_;
    opened_           = other.opened_;
    Runtime::copyString(path_, other.path_, sizeof(path_));
    other.service_ = other.handle_ = 0;
    other.opened_ = other.ownsHandle_ = other.ownsService_ =
        other.metadataValid_          = false;
    other.path_[0]                    = 0;
    other.error_                      = 0;
    return *this;
}
uint64_t File::service() {
    if (!service_) {
        service_     = detail::RegistryClient().lookup(fsServiceName, 1000);
        ownsService_ = service_ != 0;
    }
    return service_;
}
int File::borrow(uint64_t endpoint, uint64_t handle, const char* path,
                 int flags) {
    if (opened_ || service_) return error_ = error(ErrorCode::Busy);
    if (!handle || (flags & O_ACCMODE) > O_RDWR ||
        (path && (!*path || Runtime::stringLength(path) >= sizeof(path_))))
        return error_ = error(ErrorCode::InvalidArgument);
    if (!endpoint) {
        endpoint = detail::RegistryClient().lookup(fsServiceName, 1000);
        if (!endpoint) return error_ = error(ErrorCode::NotFound);
        ownsService_ = true;
    }
    service_       = endpoint;
    handle_        = handle;
    flags_         = flags;
    backend_       = Backend::Filesystem;
    opened_        = true;
    metadataValid_ = false;
    ownsHandle_    = false;
    if (path) Runtime::copyString(path_, path, sizeof(path_));
    return error_ = 0;
}
int File::adopt(uint64_t endpoint, uint64_t handle, const char* path,
                int flags) {
    const int status = borrow(endpoint, handle, path, flags);
    if (!status) ownsHandle_ = true;
    return status;
}
int File::adoptHandle(uint64_t handle, Backend backend, int flags) {
    if (opened_ || service_) return error_ = error(ErrorCode::Busy);
    if ((flags & O_ACCMODE) > O_RDWR || backend == Backend::Filesystem ||
        static_cast<int>(backend) < 1 || static_cast<int>(backend) > 3 ||
        (!handle && backend != Backend::Terminal))
        return error_ = error(ErrorCode::InvalidArgument);
    handle_       = handle;
    backend_      = backend;
    flags_        = flags;
    ownsHandle_   = handle != 0;
    opened_       = true;
    return error_ = 0;
}
int File::open(const char* path, int flags, mode_t mode) {
    if (opened_) return error_ = error(ErrorCode::Busy);
    char resolved[384];
    if (!Runtime::absolutePath(path, resolved, sizeof(resolved)) ||
        (flags & O_ACCMODE) > O_RDWR || (mode & ~07777U))
        return error_ = error(ErrorCode::InvalidArgument);
    if (!service()) return error_ = error(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::Open);
    request.length = 50;
    Runtime::copyString(reinterpret_cast<char*>(request.data), resolved, 384);
    request.data[48] = flags;
    request.data[49] = mode;
    const int status = callService(service_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 2) return error_ = error(ErrorCode::IoError);
    if (!reply.data[0])
        return error_ = openError(static_cast<int64_t>(reply.data[1]));
    handle_     = reply.data[0];
    flags_      = flags;
    backend_    = Backend::Filesystem;
    ownsHandle_ = opened_ = true;
    metadataValid_        = false;
    type_                 = Type::None;
    Runtime::copyString(path_, resolved, sizeof(path_));
    return error_ = 0;
}
int File::close() {
    if (opened_ && ownsHandle_ && handle_) {
        int status;
        if (backend_ == Backend::Filesystem) {
            IpcMessage request{}, reply{};
            request.type    = static_cast<uint64_t>(FsRequest::Close);
            request.length  = 1;
            request.data[0] = handle_;
            status          = result(service_, request, reply);
            if (reply.length && reply.data[0] == 1)
                status = error(ErrorCode::InvalidHandle);
        } else {
            Ipc::Handle handle(handle_, false);
            status = handle.close();
        }
        if (status && !gone(status)) return error_ = status;
    }
    opened_ = ownsHandle_ = metadataValid_ = false;
    handle_                                = 0;
    path_[0]                               = 0;
    type_                                  = Type::None;
    if (ownsService_) {
        const int status = closeEndpoint(service_);
        if (status) return error_ = status;
    }
    service_      = 0;
    ownsService_  = false;
    return error_ = 0;
}
uint64_t File::releaseHandle() {
    const uint64_t value = handle_;
    handle_              = 0;
    opened_ = ownsHandle_ = metadataValid_ = false;
    path_[0]                               = 0;
    type_                                  = Type::None;
    return value;
}
ssize_t File::read(void* buffer, size_t size) {
    return transfer(buffer, size, -1, false);
}
ssize_t File::write(const void* buffer, size_t size) {
    return transfer(const_cast<void*>(buffer), size, -1, true);
}
ssize_t File::pread(void* buffer, size_t size, off_t offset) {
    if (offset < 0) return error_ = error(ErrorCode::InvalidArgument);
    return transfer(buffer, size, offset, false);
}
ssize_t File::pwrite(const void* buffer, size_t size, off_t offset) {
    if (offset < 0) return error_ = error(ErrorCode::InvalidArgument);
    return transfer(const_cast<void*>(buffer), size, offset, true);
}
ssize_t File::transfer(void* buffer, size_t size, off_t offset, bool writing) {
    if (!opened_ || (writing ? (flags_ & O_ACCMODE) == O_RDONLY
                             : (flags_ & O_ACCMODE) == O_WRONLY))
        return error_ = error(ErrorCode::InvalidHandle);
    if (size && !buffer) return error_ = error(ErrorCode::BadAddress);
    if (size > INT64_MAX ||
        (offset >= 0 && size > static_cast<uint64_t>(INT64_MAX - offset)))
        return error_ = error(ErrorCode::InvalidArgument);
    if (backend_ != Backend::Filesystem) {
        if (offset >= 0) return error_ = error(ErrorCode::NotSupported);
        int64_t count;
        if (backend_ == Backend::Pipe ||
            (backend_ == Backend::Terminal && handle_)) {
            detail::PipeClient pipe(handle_);
            count =
                writing ? pipe.write(buffer, size) : pipe.read(buffer, size);
        } else if (backend_ == Backend::Terminal) {
            Console console;
            const int status = console.openCurrent();
            count            = status    ? status
                               : writing ? console.write(buffer, size)
                                         : console.read(buffer, size);
        } else
            return error_ = error(ErrorCode::NotSupported);
        error_ = count < 0 ? static_cast<int>(count) : 0;
        return count;
    }
    if (metadataValid_ && type_ == Type::Directory)
        return error_ = error(ErrorCode::InvalidArgument);
    if(!writing && size>=16384) {
        const auto count=bufferedRead(service_,handle_,buffer,size,offset);
        if(count!=static_cast<int>(ErrorCode::NotSupported)){error_=count<0?int(count):0;return count;}
    }
    if (writing && offset < 0 && (flags_ & O_APPEND) && seek(0, SEEK_END) < 0)
        return error_;
    size_t total = 0;
    error_       = 0;
    while (total < size) {
        const bool positioned = offset >= 0;
        const size_t header   = positioned ? 3 : 2;
        const size_t maximum  = (ipcDataWordCount - (positioned ? 3
                                                     : writing  ? 2
                                                                : 1)) *
                               8;
        const size_t chunk = size - total < maximum ? size - total : maximum;
        IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(
            positioned ? (writing ? FsRequest::WriteAt : FsRequest::ReadAt)
                       : (writing ? FsRequest::Write : FsRequest::Read));
        request.data[0] = handle_;
        request.length  = header;
        if (positioned) {
            request.data[1] = offset + total;
            request.data[2] = chunk;
        } else
            request.data[1] = chunk;
        if (writing) {
            Runtime::copyBytes(&request.data[header],
                               static_cast<unsigned char*>(buffer) + total,
                               chunk);
            request.length += (chunk + 7) / 8;
        }
        int status    = callService(service_, request, reply);
        int64_t count = static_cast<int64_t>(reply.data[0]);
        if (!status &&
            (!reply.length || count > static_cast<int64_t>(chunk) ||
             (count >= 0 && !writing &&
              reply.length < 1 + (static_cast<uint64_t>(count) + 7) / 8)))
            status = error(ErrorCode::IoError);
        if (!status && count < 0) status = static_cast<int>(count);
        if (!status && writing && !count) status = error(ErrorCode::IoError);
        if (status) {
            error_ = status;
            return total ? static_cast<ssize_t>(total) : status;
        }
        if (!writing)
            Runtime::copyBytes(static_cast<unsigned char*>(buffer) + total,
                               &reply.data[1], count);
        total += count;
        if (writing && count) metadataValid_ = false;
        if (static_cast<size_t>(count) < chunk) break;
    }
    return total;
}
off_t File::seek(off_t offset, int whence) {
    if (!opened_) return error_ = error(ErrorCode::InvalidHandle);
    if (backend_ != Backend::Filesystem)
        return error_ = error(ErrorCode::NotSupported);
    if (whence < 0 || whence > 2)
        return error_ = error(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(FsRequest::Seek);
    request.length   = 3;
    request.data[0]  = handle_;
    request.data[1]  = offset;
    request.data[2]  = whence;
    const int status = result(service_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 2 || reply.data[1] > INT64_MAX)
        return error_ = error(ErrorCode::IoError);
    error_ = 0;
    return reply.data[1];
}
int File::truncate(off_t length) {
    if (!opened_ || (flags_ & O_ACCMODE) == O_RDONLY)
        return error_ = error(ErrorCode::InvalidHandle);
    if (backend_ != Backend::Filesystem || length)
        return error_ = error(length < 0 ? ErrorCode::InvalidArgument
                                         : ErrorCode::NotSupported);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(FsRequest::Ftruncate);
    request.length   = 2;
    request.data[0]  = handle_;
    request.data[1]  = length;
    const int status = result(service_, request, reply);
    if (!status) metadataValid_ = false;
    return error_ = status;
}
int File::flush() {
    if (!opened_) return error_ = error(ErrorCode::InvalidHandle);
    if (backend_ != Backend::Filesystem)
        return error_ = error(ErrorCode::NotSupported);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(FsRequest::Sync);
    request.length  = 1;
    request.data[0] = handle_;
    return error_   = result(service_, request, reply);
}
int File::refresh(const char* path) {
    if (backend_ != Backend::Filesystem) {
        if (!opened_) return error_ = error(ErrorCode::InvalidHandle);
        type_ = Type::File;
        mode_ = (backend_ == Backend::Pipe       ? 0010000
                 : backend_ == Backend::Terminal ? 0020000
                                                 : 0100000) |
                0600;
        links_         = 1;
        metadataValid_ = true;
        return error_  = 0;
    }
    if (path && opened_) return error_ = error(ErrorCode::Busy);
    char resolved[384];
    if (path) {
        if (!Runtime::absolutePath(path, resolved, sizeof(resolved)))
            return error_ = error(ErrorCode::InvalidArgument);
    } else
        Runtime::copyString(resolved, path_, sizeof(resolved));
    IpcMessage request{}, reply{};
    if (opened_) {
        request.type    = static_cast<uint64_t>(FsRequest::Fstat);
        request.length  = 1;
        request.data[0] = handle_;
    } else {
        if (!resolved[0]) return error_ = error(ErrorCode::NotFound);
        request.type   = static_cast<uint64_t>(FsRequest::Stat);
        request.length = 48;
        Runtime::copyString(reinterpret_cast<char*>(request.data), resolved,
                            384);
    }
    const int status = result(service(), request, reply);
    if (status) return error_ = status;
    if (reply.length < 13 || reply.data[1] < 1 || reply.data[1] > 2)
        return error_ = error(ErrorCode::IoError);
    type_             = static_cast<Type>(reply.data[1]);
    size_             = reply.data[2];
    mode_             = reply.data[3];
    inode_            = reply.data[4];
    links_            = reply.data[5];
    uid_              = reply.data[6];
    gid_              = reply.data[7];
    accessTime_       = reply.data[8];
    modificationTime_ = reply.data[9];
    changeTime_       = reply.data[10];
    device_           = reply.data[11];
    specialDevice_    = reply.data[12];
    metadataValid_    = true;
    if (!opened_) Runtime::copyString(path_, resolved, sizeof(path_));
    return error_ = 0;
}
int File::setMode(mode_t mode) {
    if (!path_[0] || (mode & ~07777U))
        return error_ = error(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::ChangeMode);
    request.length = 49;
    Runtime::copyString(reinterpret_cast<char*>(request.data), path_, 384);
    request.data[48] = mode;
    const int status = result(service(), request, reply);
    if (!status) metadataValid_ = false;
    return error_ = status;
}
int File::setOwner(uid_t uid, gid_t gid) {
    if (!path_[0]) return error_ = error(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::ChangeOwner);
    request.length = 50;
    Runtime::copyString(reinterpret_cast<char*>(request.data), path_, 384);
    request.data[48] = uid;
    request.data[49] = gid;
    const int status = result(service(), request, reply);
    if (!status) metadataValid_ = false;
    return error_ = status;
}
int File::setTimes(const timespec times[2]) {
    if (!path_[0]) return error_ = error(ErrorCode::InvalidArgument);
    if (times && times[0].tv_nsec == UTIME_OMIT &&
        times[1].tv_nsec == UTIME_OMIT)
        return error_ = 0;
    uint64_t values[2], now = 0;
    bool refreshed = false, clockRead = false;
    for (unsigned i = 0; i < 2; ++i) {
        if (!times || times[i].tv_nsec == UTIME_NOW) {
            if (!clockRead) {
                now       = Sys().readWallClockTime() / 1000000000ULL;
                clockRead = true;
            }
            values[i] = now;
        } else if (times[i].tv_nsec == UTIME_OMIT) {
            if (!refreshed) {
                const int status = refresh();
                if (status) return status;
                refreshed = true;
            }
            values[i] = i ? modificationTime_ : accessTime_;
        } else {
            if (times[i].tv_sec < 0 || times[i].tv_nsec < 0 ||
                times[i].tv_nsec >= 1000000000L)
                return error_ = error(ErrorCode::InvalidArgument);
            values[i] = times[i].tv_sec;
        }
        if (values[i] > UINT32_MAX)
            return error_ = error(ErrorCode::InvalidArgument);
    }
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::ChangeTimes);
    request.length = 50;
    Runtime::copyString(reinterpret_cast<char*>(request.data), path_, 384);
    request.data[48] = values[0];
    request.data[49] = values[1];
    const int status = result(service(), request, reply);
    if (!status) metadataValid_ = false;
    return error_ = status;
}
void File::setStatusFlags(int flags) {
    flags_ = (flags_ & O_ACCMODE) | (flags & ~O_ACCMODE);
}
uint64_t File::getHandle() const { return handle_; }
int File::getFlags() const { return flags_; }
int File::getError() const { return error_; }
const char* File::getPath() const { return path_; }
File::Type File::getType() const { return type_; }
uint64_t File::getSize() const { return size_; }
uint32_t File::getMode() const { return mode_; }
uint64_t File::getInode() const { return inode_; }
uint32_t File::getLinkCount() const { return links_; }
uint32_t File::getUid() const { return uid_; }
uint32_t File::getGid() const { return gid_; }
uint64_t File::getAccessTime() const { return accessTime_; }
uint64_t File::getModificationTime() const { return modificationTime_; }
uint64_t File::getChangeTime() const { return changeTime_; }
uint64_t File::getDevice() const { return device_; }
uint64_t File::getSpecialDevice() const { return specialDevice_; }
bool File::getOpened() const { return opened_; }
bool File::getSnapshotValid() const { return metadataValid_; }
File::Backend File::getBackend() const { return backend_; }
size_t File::getMaximumAsyncTransfer() const { return asyncCapacity; }
File::Opt::Opt() = default;
File::Opt::~Opt() {
    if (submitted_ && !completed_) endpoint_.destroy();
    endpoint_.close();
}
File::Opt::Opt(Opt&& other) noexcept { *this = static_cast<Opt&&>(other); }
File::Opt& File::Opt::operator=(Opt&& other) noexcept {
    if (this == &other) return *this;
    if (endpoint_.getValid() || submitted_) return *this;
    endpoint_    = static_cast<Ipc&&>(other.endpoint_);
    id_          = other.id_;
    transferred_ = other.transferred_;
    status_      = other.status_;
    requested_   = other.requested_;
    submitted_   = other.submitted_;
    completed_   = other.completed_;
    reading_     = other.reading_;
    Runtime::copyBytes(data_, other.data_, sizeof(data_));
    other.id_ = other.transferred_ = other.requested_ = 0;
    other.status_                                     = 0;
    other.submitted_ = other.completed_ = false;
    return *this;
}
int File::submit(uint64_t offset, const void* buffer, size_t size,
                 Opt& operation, bool writing) {
    if (operation.submitted_ || operation.endpoint_.getValid())
        return error_ = error(ErrorCode::Busy);
    if (!opened_ || backend_ != Backend::Filesystem ||
        (writing ? (flags_ & O_ACCMODE) == O_RDONLY
                 : (flags_ & O_ACCMODE) == O_WRONLY))
        return error_ = error(ErrorCode::InvalidHandle);
    if (!size || size > asyncCapacity || offset > INT64_MAX ||
        size > INT64_MAX - offset || (writing && !buffer))
        return error_ = error(ErrorCode::InvalidArgument);
    int status = operation.endpoint_.create();
    if (status) return error_ = status;
    Ipc::Handle endpoint(operation.endpoint_.getHandle(), false);
    status = endpoint.setFlags(handleFlagCloseOnExec);
    if (status) {
        operation.endpoint_.close();
        return error_ = status;
    }
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(writing ? FsRequest::WriteAtAsync
                                                    : FsRequest::ReadAtAsync);
    request.length  = 4;
    request.data[0] = handle_;
    request.data[1] = offset;
    request.data[2] = size;
    request.data[3] = __atomic_fetch_add(&nextOperation, 1, __ATOMIC_RELAXED);
    request.handleCount = 1;
    request.handles[0]  = operation.endpoint_.getHandle();
    if (writing) {
        Runtime::copyBytes(&request.data[4], buffer, size);
        request.length += (size + 7) / 8;
    }
    status = sendMessage(service_, request);
    if (status) {
        operation.endpoint_.close();
        return error_ = status;
    }
    operation.id_          = request.data[3];
    operation.status_      = error(ErrorCode::WouldBlock);
    operation.transferred_ = 0;
    operation.requested_   = size;
    operation.submitted_   = true;
    operation.completed_   = false;
    operation.reading_     = !writing;
    if (writing) metadataValid_ = false;
    return error_ = 0;
}
int File::readAsync(uint64_t offset, size_t size, Opt& operation) {
    return submit(offset, nullptr, size, operation, false);
}
int File::writeAsync(uint64_t offset, const void* buffer, size_t size,
                     Opt& operation) {
    return submit(offset, buffer, size, operation, true);
}
int File::Opt::poll() { return wait(0); }
int File::Opt::wait(uint64_t deadline) {
    if (!submitted_) return error(ErrorCode::InvalidHandle);
    if (completed_) return static_cast<int>(status_);
    IpcMessage completion{};
    const int transport =
        receiveMessage(endpoint_.getHandle(), completion, deadline);
    if (transport) return transport;
    for (uint32_t i = 0; i < completion.handleCount && i < 4; ++i) {
        Ipc::Handle handle(completion.handles[i]);
        handle.close();
    }
    if (completion.type != fsCompletionNotification || completion.length < 1 ||
        completion.data[0] != id_)
        return error(ErrorCode::IoError);
    if (completion.length < 3 || completion.data[2] > requested_ ||
        (reading_ && static_cast<int64_t>(completion.data[1]) == 0 &&
         completion.length < 3 + (completion.data[2] + 7) / 8)) {
        status_      = error(ErrorCode::IoError);
        transferred_ = 0;
        completed_   = true;
        return static_cast<int>(status_);
    }
    status_ = static_cast<int64_t>(completion.data[1]);
    if (status_ > 0) status_ = error(ErrorCode::IoError);
    transferred_ = status_ ? 0 : completion.data[2];
    if (reading_ && transferred_)
        Runtime::copyBytes(data_, &completion.data[3], transferred_);
    completed_ = true;
    return static_cast<int>(status_);
}
int File::Opt::close() {
    if (submitted_ && !completed_) return error(ErrorCode::Busy);
    const int status = endpoint_.close();
    if (status) return status;
    submitted_ = completed_ = false;
    id_ = transferred_ = requested_ = 0;
    status_                         = 0;
    return 0;
}
uint64_t File::Opt::getId() const { return id_; }
int64_t File::Opt::getStatus() const { return status_; }
uint64_t File::Opt::getTransferred() const { return transferred_; }
bool File::Opt::getCompleted() const { return completed_; }
const void* File::Opt::getData() const {
    return completed_ && reading_ && !status_ ? data_ : nullptr;
}
Fs::Fs() = default;
Fs::~Fs() { closeEndpoint(service_); }
uint64_t Fs::service() {
    if (!service_)
        service_ = detail::RegistryClient().lookup(fsServiceName, 1000);
    return service_;
}
int Fs::open(const char* path, int flags, mode_t mode, File& file) {
    return file.open(path, flags, mode);
}
int Fs::query(const char* path, File& file) { return file.refresh(path); }
int Fs::removeFile(const char* path) {
    return pathRequest(service(), FsRequest::RemoveFile, path);
}
int Fs::rename(const char* from, const char* to) {
    return pathRequest(service(), FsRequest::Rename, from, to);
}
int Fs::link(const char* from, const char* to) {
    return pathRequest(service(), FsRequest::Link, from, to);
}
int Fs::format(const char* device, const char* type) {
    char resolved[64];
    if (!Runtime::absolutePath(device, resolved, sizeof(resolved)) || !type ||
        !*type || Runtime::stringLength(type) >= 16)
        return error(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::Format);
    request.length = 10;
    Runtime::copyString(reinterpret_cast<char*>(request.data), resolved, 64);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 64, type, 16);
    return result(service(), request, reply);
}
int Fs::queryMount(uint32_t index, Mount& mount) {
    if (mount.owned_) return mount.error_ = error(ErrorCode::Busy);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(FsRequest::ListMounts);
    request.length   = 1;
    request.data[0]  = index;
    const int status = result(service(), request, reply);
    if (status) return mount.error_ = status;
    const char* device = reinterpret_cast<const char*>(&reply.data[1]);
    const char* target = reinterpret_cast<const char*>(&reply.data[9]);
    const char* type   = reinterpret_cast<const char*>(&reply.data[17]);
    if (reply.length < 20 || !terminated(device, 64) ||
        !terminated(target, 64) || !terminated(type, 16) || reply.data[19] > 1)
        return mount.error_ = error(ErrorCode::IoError);
    Runtime::copyString(mount.device_, device, 64);
    Runtime::copyString(mount.target_, target, 64);
    Runtime::copyString(mount.type_, type, 16);
    mount.readOnly_     = reply.data[19];
    mount.mounted_      = true;
    return mount.error_ = 0;
}
Mount::Mount() = default;
Mount::~Mount() { close(); }
Mount::Mount(Mount&& other) noexcept { *this = static_cast<Mount&&>(other); }
Mount& Mount::operator=(Mount&& other) noexcept {
    if (this == &other) return *this;
    if (owned_ || service_) {
        error_ = error(ErrorCode::Busy);
        return *this;
    }
    service_ = other.service_;
    Runtime::copyBytes(device_, other.device_, 64);
    Runtime::copyBytes(target_, other.target_, 64);
    Runtime::copyBytes(type_, other.type_, 16);
    readOnly_      = other.readOnly_;
    mounted_       = other.mounted_;
    owned_         = other.owned_;
    error_         = other.error_;
    other.service_ = 0;
    other.owned_ = other.mounted_ = false;
    other.device_[0] = other.target_[0] = other.type_[0] = 0;
    other.error_                                         = 0;
    return *this;
}
int Mount::create(const char* source, const char* target, const char* type,
                  unsigned long flags, const void* data) {
    if (mounted_ || service_) return error_ = error(ErrorCode::Busy);
    if (flags || data) return error_ = error(ErrorCode::NotSupported);
    char from[64], to[64];
    if (!Runtime::absolutePath(source, from, sizeof(from)) ||
        !Runtime::absolutePath(target, to, sizeof(to)) || !type || !*type ||
        Runtime::stringLength(type) >= 16)
        return error_ = error(ErrorCode::InvalidArgument);
    service_ = detail::RegistryClient().lookup(fsServiceName, 1000);
    if (!service_) return error_ = error(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::Mount);
    request.length = 18;
    Runtime::copyString(reinterpret_cast<char*>(request.data), from, 64);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 64, to, 64);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 128, type, 16);
    const int status = result(service_, request, reply);
    if (status) {
        closeEndpoint(service_);
        return error_ = status;
    }
    Runtime::copyString(device_, from, 64);
    Runtime::copyString(target_, to, 64);
    Runtime::copyString(type_, type, 16);
    owned_ = mounted_ = true;
    readOnly_         = reply.length >= 2 && reply.data[1];
    return error_     = 0;
}
int Mount::close() {
    if (owned_) {
        const int status = pathRequest(service_, FsRequest::Unmount, target_);
        if (status) return error_ = status;
        owned_ = mounted_ = false;
    }
    const int status = closeEndpoint(service_);
    if (status) return error_ = status;
    mounted_   = false;
    device_[0] = target_[0] = type_[0] = 0;
    return error_                      = 0;
}
const char* Mount::getDevice() const { return device_; }
const char* Mount::getTarget() const { return target_; }
const char* Mount::getType() const { return type_; }
bool Mount::getReadOnly() const { return readOnly_; }
bool Mount::getMounted() const { return mounted_; }
bool Mount::getOwned() const { return owned_; }
int Mount::getError() const { return error_; }
}  // namespace os
