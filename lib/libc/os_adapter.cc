#include <cerrno>
#include <devctl.h>
#include <fcntl.h>
#include <cstdlib>
#include <new>
#include <termios.h>
#include <unistd.h>

#include "internal.h"
#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/task/msgq-records.h"
#include "service/tty/records.h"
#include <os/console.h>
#include <os/device.h>
#include <os/elf.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/pty.h>
#include <os/service.h>
#include <os/task.h>
#include <sys/stat.h>
extern "C" {
#include "internal.h"
}

static_assert(sizeof(OsDirectoryEntry) == sizeof(os::wire::DirectoryEntry));
static_assert(sizeof(OsTaskIdentifiers) == sizeof(os::wire::TaskIdentifiers));
static_assert(sizeof(OsTaskInfo) == sizeof(os::wire::TaskInfo));
static_assert(sizeof(OsCredentialInfo) == sizeof(os::wire::CredentialInfo));
static_assert(sizeof(OsSignalAction) == sizeof(os::wire::SignalAction));
static_assert(sizeof(OsMessageQueueAttributes) ==
              sizeof(os::wire::MessageQueueAttributes));
static_assert(sizeof(OsExecImageInfo) == sizeof(os::wire::ExecImageInfo));

namespace {
int64_t posixFileResult(int64_t status) {
    if (status >= 0) return status;
    switch (static_cast<os::ErrorCode>(status)) {
        case os::ErrorCode::InvalidArgument:
            return -EINVAL;
        case os::ErrorCode::InvalidHandle:
            return -EBADF;
        case os::ErrorCode::PermissionDenied:
            return -EACCES;
        case os::ErrorCode::NotFound:
            return -ENOENT;
        case os::ErrorCode::OutOfMemory:
            return -ENOMEM;
        case os::ErrorCode::WouldBlock:
            return -EAGAIN;
        case os::ErrorCode::NotSupported:
            return -ENOTSUP;
        case os::ErrorCode::Busy:
            return -EBUSY;
        case os::ErrorCode::Timeout:
            return -ETIMEDOUT;
        case os::ErrorCode::Interrupted:
            return -EINTR;
        case os::ErrorCode::Cancelled:
            return -ECANCELED;
        case os::ErrorCode::EndpointClosed:
            return -EPIPE;
        case os::ErrorCode::PeerDied:
            return -ECONNRESET;
        case os::ErrorCode::BadAddress:
            return -EFAULT;
        default:
            return -EIO;
    }
}
os::File borrowedFile(uint64_t fs, uint64_t descriptor) {
    os::File value;
    value.borrow(fs, descriptor, nullptr, O_RDWR);
    return value;
}
bool filePathMessage(os::IpcMessage& request, os::FsRequest op,
                     const char* path) {
    if (!path || !*path || os::Runtime::stringLength(path) >= 384) return false;
    request.type   = static_cast<uint64_t>(op);
    request.length = 48;
    os::Runtime::copyString(reinterpret_cast<char*>(request.data), path, 384);
    return true;
}
bool filePathCall(uint64_t fs, os::FsRequest op, const char* path) {
    os::IpcMessage request{}, reply{};
    return filePathMessage(request, op, path) &&
           !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}
}  // namespace

#pragma GCC visibility push(hidden)
extern "C" {

void __libc_os_bind_environment(char*** environment) {
    os::Runtime::bindEnvironment(environment);
    os::Runtime::bindWorkingDirectory(libcGetCurrentDirectory);
}

void* __libc_file_adopt(uint64_t fs, uint64_t handle, const char* path,
                        int flags, int kind) {
    auto* result = static_cast<os::File*>(malloc(sizeof(os::File)));
    if (!result) return nullptr;
    new (result) os::File;
    const int status =
        kind == DESCRIPTOR_FILE
            ? result->adopt(fs, handle, path, flags)
            : result->adoptHandle(handle,
                                  (kind == DESCRIPTOR_SHARED_MEMORY || kind == DESCRIPTOR_TYPED_MEMORY)
                                      ? os::File::Backend::SharedMemory
                                  : (kind == DESCRIPTOR_TERMINAL_READ ||
                                     kind == DESCRIPTOR_TERMINAL_WRITE) &&
                                          !handle
                                      ? os::File::Backend::Terminal
                                      : os::File::Backend::Pipe,
                                  flags);
    if (status) {
        result->~File();
        free(result);
        return nullptr;
    }
    return result;
}
uint64_t __libc_file_open(uint64_t fs, const char* path, int flags, mode_t mode,
                          int* error) {
    os::IpcMessage request{}, reply{};
    int status = -EINVAL;
    if (filePathMessage(request, os::FsRequest::Open, path)) {
        request.length      = 50;
        request.data[48]    = flags;
        request.data[49]    = mode;
        const int transport = os::callService(fs, request, reply);
        status = transport ? static_cast<int>(posixFileResult(transport))
                 : reply.length < 2 ? -EIO
                 : reply.data[0]    ? 0
                                    : static_cast<int>(reply.data[1]);
    }
    if (error) *error = status;
    return status ? 0 : reply.data[0];
}

int __libc_file_close(void* object) {
    auto* file       = static_cast<os::File*>(object);
    const int status = file->close();
    if (status) return 0;
    file->~File();
    free(file);
    return 1;
}
void __libc_file_rebind(void* object, uint64_t handle) {
    if (!object) return;
    auto& file         = *static_cast<os::File*>(object);
    const auto backend = file.getBackend();
    const int flags    = file.getFlags();
    char path[384];
    os::Runtime::copyString(path, file.getPath(), sizeof(path));
    file.releaseHandle();
    if (file.close()) return;
    if (backend == os::File::Backend::Filesystem)
        file.adopt(libcFsService(), handle, path, flags);
    else
        file.adoptHandle(handle, backend, flags);
}
uint64_t __libc_file_handle(void* object) {
    return object ? static_cast<os::File*>(object)->getHandle() : 0;
}
void __libc_file_set_flags(void* object, int flags) {
    if (object) static_cast<os::File*>(object)->setStatusFlags(flags);
}
ssize_t __libc_file_read(void* object, void* data, size_t size) {
    return object ? posixFileResult(
                        static_cast<os::File*>(object)->read(data, size))
                  : -EBADF;
}
ssize_t __libc_file_write(void* object, const void* data, size_t size) {
    return object ? posixFileResult(
                        static_cast<os::File*>(object)->write(data, size))
                  : -EBADF;
}
ssize_t __libc_file_pread(void* object, void* data, size_t size, off_t offset) {
    return object ? posixFileResult(static_cast<os::File*>(object)->pread(
                        data, size, offset))
                  : -EBADF;
}
ssize_t __libc_file_pwrite(void* object, const void* data, size_t size,
                           off_t offset) {
    return object ? posixFileResult(static_cast<os::File*>(object)->pwrite(
                        data, size, offset))
                  : -EBADF;
}
off_t __libc_file_seek(void* object, off_t offset, int whence) {
    return object ? posixFileResult(
                        static_cast<os::File*>(object)->seek(offset, whence))
                  : -EBADF;
}
int __libc_file_fsync(void* object) {
    return object ? posixFileResult(static_cast<os::File*>(object)->flush())
                  : -EBADF;
}

int __libc_file_stat(void* object, struct stat* result) {
    if (!object) return -EBADF;
    if (!result) return -EFAULT;
    auto& file      = *static_cast<os::File*>(object);
    const int error = file.refresh();
    if (error < 0) return posixFileResult(error);
    *result                = {};
    result->st_mode        = file.getMode();
    result->st_dev         = file.getDevice();
    result->st_ino         = file.getInode();
    result->st_nlink       = file.getLinkCount();
    result->st_uid         = file.getUid();
    result->st_gid         = file.getGid();
    result->st_rdev        = file.getSpecialDevice();
    result->st_size        = file.getSize();
    result->st_blksize     = 4096;
    result->st_blocks      = (result->st_size + 511) / 512;
    result->st_atim.tv_sec = file.getAccessTime();
    result->st_mtim.tv_sec = file.getModificationTime();
    result->st_ctim.tv_sec = file.getChangeTime();
    return 0;
}

int __libc_os_is_terminal_handle(uint64_t handle) {
    os::detail::PtyClient terminal(handle);
    return handle != 0 && terminal.refreshAttributes();
}

uint64_t __libc_os_get_fs_service() {
    return os::detail::RegistryClient(os::getBootstrapHandle())
        .lookup(os::fsServiceName, 1000);
}

uint64_t __libc_os_open_file(uint64_t fs, const char* path) {
    return __libc_file_open(fs, path, O_RDONLY, 0666, nullptr);
}

int __libc_os_close_file(uint64_t fs, uint64_t descriptor) {
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::FsRequest::Close);
    request.length  = 1;
    request.data[0] = descriptor;
    return !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}

int64_t __libc_os_read_file(uint64_t fs, uint64_t descriptor, void* buffer,
                            size_t size) {
    return borrowedFile(fs, descriptor).read(buffer, size);
}

int64_t __libc_os_write_file(uint64_t fs, uint64_t descriptor,
                             const void* buffer, size_t size) {
    return borrowedFile(fs, descriptor).write(buffer, size);
}

int __libc_os_seek_file(uint64_t fs, uint64_t descriptor, uint64_t offset) {
    return (borrowedFile(fs, descriptor).seek(offset, 0) >= 0);
}

int64_t __libc_os_seek_file_position(uint64_t fs, uint64_t descriptor,
                                     int64_t offset, int whence) {
    return borrowedFile(fs, descriptor).seek(offset, whence);
}

int __libc_os_clone_file_descriptors(uint64_t fs, uint64_t parentTaskId,
                                     uint64_t* oldDescriptors,
                                     uint64_t* newDescriptors,
                                     size_t capacity) {
    if (!oldDescriptors || !newDescriptors) return -1;
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::FsRequest::CloneDescriptors);
    request.length  = 1;
    request.data[0] = parentTaskId;
    if (os::callService(fs, request, reply) || reply.length < 2 ||
        reply.data[0] || reply.data[1] > capacity ||
        reply.data[1] > (os::ipcDataWordCount - 2) / 2 ||
        reply.length < 2 + reply.data[1] * 2)
        return -1;
    for (size_t i = 0; i < reply.data[1]; ++i) {
        oldDescriptors[i] = reply.data[2 + i * 2];
        newDescriptors[i] = reply.data[3 + i * 2];
    }
    return reply.data[1];
}

int __libc_os_create_file(uint64_t fs, const char* path) {
    int status;
    const uint64_t descriptor =
        __libc_file_open(fs, path, O_WRONLY | O_CREAT | O_EXCL, 0666, &status);
    return descriptor && __libc_os_close_file(fs, descriptor);
}

int __libc_os_make_directory(uint64_t fs, const char* path) {
    if (!path || os::Runtime::stringLength(path) >= 384) return 0;
    os::IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(os::FsRequest::DirCreate);
    request.length   = 49;
    request.data[48] = 0755;
    os::Runtime::copyString(reinterpret_cast<char*>(request.data), path, 384);
    return !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}

int __libc_os_remove_path(uint64_t fs, const char* path) {
    return filePathCall(fs, os::FsRequest::Remove, path);
}

int __libc_os_truncate_file(uint64_t fs, const char* path) {
    return filePathCall(fs, os::FsRequest::Truncate, path);
}

int __libc_os_stat_file(const char* path, OsFileStat* status) {
    if (status == nullptr) return 0;
    os::File value;
    if (!(value.refresh(path) == 0)) return 0;
    status->type             = static_cast<uint64_t>(value.getType());
    status->size             = value.getSize();
    status->mode             = value.getMode();
    status->inode            = value.getInode();
    status->links            = value.getLinkCount();
    status->uid              = value.getUid();
    status->gid              = value.getGid();
    status->accessTime       = value.getAccessTime();
    status->modificationTime = value.getModificationTime();
    status->changeTime       = value.getChangeTime();
    status->device           = value.getDevice();
    status->specialDevice    = value.getSpecialDevice();
    return 1;
}

int __libc_os_link_file(uint64_t fs, const char* existing,
                        const char* newPath) {
    os::IpcMessage request{}, reply{};
    if (!filePathMessage(request, os::FsRequest::Link, existing) || !newPath ||
        os::Runtime::stringLength(newPath) >= 384)
        return 0;
    os::Runtime::copyString(reinterpret_cast<char*>(request.data) + 384,
                            newPath, 384);
    request.length = 96;
    return !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}
int __libc_os_change_file_mode(uint64_t fs, const char* path, uint32_t mode) {
    os::IpcMessage request{}, reply{};
    if (!filePathMessage(request, os::FsRequest::ChangeMode, path)) return 0;
    request.length   = 49;
    request.data[48] = mode;
    return !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}
int __libc_os_change_file_owner(uint64_t fs, const char* path, uint32_t uid,
                                uint32_t gid) {
    os::IpcMessage request{}, reply{};
    if (!filePathMessage(request, os::FsRequest::ChangeOwner, path)) return 0;
    request.length   = 50;
    request.data[48] = uid;
    request.data[49] = gid;
    return !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}
int __libc_os_change_file_times(uint64_t fs, const char* path,
                                uint64_t accessTime,
                                uint64_t modificationTime) {
    os::IpcMessage request{}, reply{};
    if (!filePathMessage(request, os::FsRequest::ChangeTimes, path)) return 0;
    request.length   = 50;
    request.data[48] = accessTime;
    request.data[49] = modificationTime;
    return !os::callService(fs, request, reply) && reply.length &&
           !reply.data[0];
}

int64_t __libc_os_read_directory_batch(uint64_t fs, const char* path,
                                       uint64_t index,
                                       OsDirectoryEntry* entries,
                                       size_t capacity) {
    if (!fs || !path || !entries || !capacity ||
        os::Runtime::stringLength(path) >= 384)
        return static_cast<int>(os::ErrorCode::InvalidArgument);
    const size_t limit = capacity < 14 ? capacity : 14;
    os::IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(os::FsRequest::ReadDirectoryBatch);
    request.length = 50;
    os::Runtime::copyString(reinterpret_cast<char*>(request.data), path, 384);
    request.data[48] = index;
    request.data[49] = limit;
    const int status = os::callService(fs, request, reply);
    if (status) return status;
    if (reply.length < 2 || reply.data[0] || reply.data[1] > limit ||
        reply.length < 2 + (reply.data[1] * sizeof(*entries) + 7) / 8)
        return static_cast<int>(os::ErrorCode::IoError);
    os::Runtime::copyBytes(entries, &reply.data[2],
                           reply.data[1] * sizeof(*entries));
    return reply.data[1];
}

uint64_t __libc_os_create_endpoint() {
    return os::detail::EndpointControl().create();
}

int __libc_os_read_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                              size_t size, uint64_t completionEndpoint,
                              uint64_t requestId) {
    if (!fs || !descriptor || !completionEndpoint || !size || size > 4088)
        return 0;
    os::IpcMessage request{};
    request.type        = static_cast<uint64_t>(os::FsRequest::ReadAtAsync);
    request.length      = 4;
    request.data[0]     = descriptor;
    request.data[1]     = offset;
    request.data[2]     = size;
    request.data[3]     = requestId;
    request.handleCount = 1;
    request.handles[0]  = completionEndpoint;
    return !os::sendMessage(fs, request);
}
int __libc_os_write_file_async(uint64_t fs, uint64_t descriptor,
                               uint64_t offset, const void* buffer, size_t size,
                               uint64_t completionEndpoint,
                               uint64_t requestId) {
    if (!fs || !descriptor || !completionEndpoint || !buffer || !size ||
        size > 4088)
        return 0;
    os::IpcMessage request{};
    request.type        = static_cast<uint64_t>(os::FsRequest::WriteAtAsync);
    request.length      = 4 + (size + 7) / 8;
    request.data[0]     = descriptor;
    request.data[1]     = offset;
    request.data[2]     = size;
    request.data[3]     = requestId;
    request.handleCount = 1;
    request.handles[0]  = completionEndpoint;
    os::Runtime::copyBytes(&request.data[4], buffer, size);
    return !os::sendMessage(fs, request);
}
int __libc_os_wait_file_operation(uint64_t endpoint,
                                  OsFileOperationResult* result, void* buffer,
                                  size_t capacity) {
    if (!endpoint || !result) return 0;
    os::IpcMessage completion{};
    if (os::receiveMessage(endpoint, completion) ||
        completion.type != os::fsCompletionNotification ||
        completion.length < 3 || completion.data[2] > 4088)
        return 0;
    if (!completion.data[1] && completion.length > 3) {
        if (!buffer || completion.data[2] > capacity ||
            completion.length < 3 + (completion.data[2] + 7) / 8)
            return 0;
        os::Runtime::copyBytes(buffer, &completion.data[3], completion.data[2]);
    }
    result->requestId   = completion.data[0];
    result->status      = static_cast<int64_t>(completion.data[1]);
    result->transferred = completion.data[2];
    return 1;
}

int __libc_os_create_pipe(OsPipeHandles* handles) {
    if (handles == nullptr) return 0;
    os::Ipc::Pipe value;
    if (value.create() != 0) return 0;
    handles->readHandle  = value.releaseReadHandle();
    handles->writeHandle = value.releaseWriteHandle();
    return 1;
}

int64_t __libc_os_read_pipe(uint64_t handle, void* buffer, size_t size) {
    return os::detail::PipeClient(handle).read(buffer, size);
}

int64_t __libc_os_write_pipe(uint64_t handle, const void* buffer, size_t size) {
    return os::detail::PipeClient(handle).write(buffer, size);
}

uint64_t __libc_os_get_standard_handle(uint32_t index) {
    return os::getStandardHandle(index);
}

int __libc_os_close_handle(uint64_t handle) { return os::closeHandle(handle); }

int64_t __libc_os_read_terminal(void* buffer, size_t size) {
    os::Console console;
    const int status = console.openCurrent();
    return status ? status : console.read(buffer, size);
}

int64_t __libc_os_write_terminal(const void* buffer, size_t size) {
    os::Console console;
    const int status = console.openCurrent();
    return status ? status : console.write(buffer, size);
}

uint32_t __libc_os_get_terminal_id() { return os::Runtime::getConsoleId(); }

void __libc_os_set_terminal_id(uint32_t terminalId) {
    os::Runtime::setConsoleId(terminalId);
}

void __libc_os_yield() { os::yield(); }
void __libc_os_sleep(uint64_t milliseconds) { os::sleep(milliseconds); }
void __libc_os_exit_thread(int status) { os::exitThread(status); }
uint64_t __libc_os_create_thread(void (*entry)(void*), void* argument,
                                 uint64_t stackPointer) {
    return os::createThread(entry, stackPointer, argument);
}
int __libc_os_wait_thread(uint64_t threadId, int* exitCode, int noHang) {
    if (exitCode == nullptr) return -1;
    return os::detail::TaskControl().waitThread(threadId, *exitCode,
                                                noHang != 0);
}
int __libc_os_wait_address(volatile uint32_t* address, uint32_t expected) {
    return os::waitAddress(address, expected);
}
int __libc_os_wake_address(volatile uint32_t* address, uint32_t count) {
    return os::wakeAddress(address, count);
}
int __libc_os_set_signal_action(uint32_t signal, const OsSignalAction* action,
                                OsSignalAction* oldAction) {
    return os::detail::TaskControl().setSignalAction(
        signal, reinterpret_cast<const os::wire::SignalAction*>(action),
        reinterpret_cast<os::wire::SignalAction*>(oldAction));
}
int __libc_os_set_signal_mask(int how, uint64_t mask, uint64_t* oldMask) {
    if (oldMask == nullptr) return -1;
    return os::detail::TaskControl().setSignalMask(how, mask, *oldMask);
}
int __libc_os_get_pending_signals(uint64_t* mask) {
    if (mask == nullptr) return -1;
    return os::detail::TaskControl().getPendingSignals(*mask);
}
int __libc_os_wait_signal(uint64_t mask) {
    return os::detail::TaskControl().waitSignal(mask);
}
int __libc_os_signal_stack(const void* stack, void* oldStack) {
    return os::detail::TaskControl().signalStack(
        static_cast<const OsSignalStack*>(stack),
        static_cast<OsSignalStack*>(oldStack));
}
int __libc_os_core_limit(int set, uint64_t* soft, uint64_t* hard) {
    return soft && hard ? os::detail::TaskControl().coreLimit(set, *soft, *hard)
                        : -1;
}
int __libc_os_credentials(unsigned operation, uint32_t first, uint32_t second,
                          uint32_t third, const uint32_t* groups,
                          OsCredentialInfo* info) {
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::TaskOperation::Credentials);
    request.length  = 4;
    request.data[0] = operation;
    request.data[1] = first;
    request.data[2] = second;
    request.data[3] = third;
    if (operation == 9) {
        if (first > 32) return 22;
        request.length += first;
        for (uint32_t i = 0; i < first; ++i) request.data[4 + i] = groups[i];
    }
    int result = services::RequestChannel(os::CoreServiceKind::Task)
                     .result(request, reply);
    if (result) return result == -3 ? 1 : result == -4 ? 3 : 5;
    if (!reply.data[1] && info)
        os::Runtime::copyBytes(info, &reply.data[2], sizeof(*info));
    return reply.data[1];
}
int __libc_os_priority(unsigned operation, unsigned which, uint64_t who,
                       int value, int* result) {
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::TaskOperation::Priority);
    request.length  = 4;
    request.data[0] = operation;
    request.data[1] = which;
    request.data[2] = who;
    request.data[3] = static_cast<int64_t>(value);
    if (services::RequestChannel(os::CoreServiceKind::Task)
            .result(request, reply))
        return 5;
    if (result) *result = static_cast<int>(reply.data[2]);
    return static_cast<int>(reply.data[1]);
}
int __libc_os_resource_limit(uint64_t pid, int resource, int set,
                             uint64_t* soft, uint64_t* hard) {
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::TaskOperation::ResourceLimit);
    request.length  = 5;
    request.data[0] = pid;
    request.data[1] = resource;
    request.data[2] = set;
    request.data[3] = *soft;
    request.data[4] = *hard;
    if (services::RequestChannel(os::CoreServiceKind::Task)
            .result(request, reply))
        return 5;
    if (!reply.data[1]) {
        *soft = reply.data[2];
        *hard = reply.data[3];
    }
    return static_cast<int>(reply.data[1]);
}
int __libc_os_accessible(const void* address, uint64_t size, int writable) {
    return os::detail::TaskControl().accessible(address, size, writable);
}
uint64_t __libc_os_signal_generation(void) {
    os::IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(os::TaskOperation::SignalGeneration);
    request.length = 4;
    services::RequestChannel(os::CoreServiceKind::Task).result(request, reply);
    return reply.data[1];
}
void __libc_os_signal_return(void* frame) {
    os::detail::TaskControl().signalReturn(frame);
}
uint64_t __libc_os_open_message_queue(const char* name, int flags,
                                      uint32_t maximumMessages,
                                      uint32_t messageSize) {
    os::Ipc::MsgQueue queue;
    const int status = queue.open(name, flags, maximumMessages, messageSize);
    return status ? static_cast<uint64_t>(status) : queue.release();
}
int __libc_os_unlink_message_queue(const char* name) {
    return os::Ipc::MsgQueue().unlink(name);
}
int __libc_os_send_message_queue(uint64_t handle, const void* data, size_t size,
                                 uint32_t priority) {
    return os::detail::QueueClient(handle).send(data, size, priority);
}
int64_t __libc_os_receive_message_queue(uint64_t handle, void* data,
                                        size_t capacity, uint32_t* priority) {
    if (priority == nullptr) return -1;
    return os::detail::QueueClient(handle).receive(data, capacity, *priority);
}
int __libc_os_get_message_queue_attributes(
    uint64_t handle, OsMessageQueueAttributes* attributes) {
    if (attributes == nullptr) return -1;
    os::detail::QueueClient queue(handle);
    const int status = queue.refresh();
    if (!status) {
        attributes->flags           = queue.getFlags();
        attributes->maximumMessages = queue.getMaximumMessages();
        attributes->messageSize     = queue.getMessageSize();
        attributes->currentMessages = queue.getCurrentMessages();
    }
    return status;
}
int __libc_os_set_message_queue_flags(uint64_t handle, uint64_t flags,
                                      uint64_t* oldFlags) {
    if (oldFlags == nullptr) return -1;
    return os::detail::QueueClient(handle).setFlags(flags, oldFlags);
}
uint64_t __libc_os_open_shared_memory(const char* name, int flags) {
    return os::detail::MappingClient().openShared(name, flags);
}
int __libc_os_unlink_shared_memory(const char* name) {
    return os::detail::MappingClient().unlinkShared(name);
}
int __libc_os_resize_shared_memory(uint64_t handle, uint64_t size) {
    return os::detail::MemoryControl().resizeSharedMemory(handle, size);
}
uint64_t __libc_os_map_shared_memory(uint64_t handle, uint64_t address,
                                     uint64_t size, uint64_t offset,
                                     uint64_t protection) {
    return os::detail::MemoryControl().mapSharedMemory(handle, address, size,
                                                       offset, protection);
}
int __libc_os_send_thread_signal(uint64_t threadId, uint32_t signal) {
    return os::detail::TaskControl().sendThreadSignal(threadId, signal);
}
int __libc_os_register_message_queue_notification(uint64_t handle, int type,
                                                  uint32_t signal) {
    return os::detail::QueueClient(handle).setNotification(type, signal);
}
int64_t __libc_os_fork_task() { return os::forkTask(); }
int64_t __libc_os_fork_endpoint(void) {
    const int64_t endpoint = os::detail::EndpointControl().create();
    if (endpoint > 0 &&
        os::Ipc::Handle(endpoint, false).setFlags(os::handleFlagCloseOnExec)) {
        os::closeHandle(endpoint);
        return -5;
    }
    return endpoint;
}
int __libc_os_fork_notify(uint64_t endpoint, int status) {
    os::IpcMessage message{};
    message.length  = 1;
    message.data[0] = status;
    return os::sendMessage(endpoint, message,
                           os::Sys().readMonotonicTime() + 10000000000ULL);
}
int __libc_os_fork_receive(uint64_t endpoint, uint64_t child) {
    const uint64_t deadline = os::Sys().readMonotonicTime() + 10000000000ULL;
    for (;;) {
        os::IpcMessage message{};
        const int status = os::receiveMessage(endpoint, message, deadline);
        if (status) return status;
        if (message.senderPid == child && message.length == 1)
            return message.data[0] ? -5 : 0;
    }
}
int64_t __libc_os_wait_id(int64_t taskId, int options, uint32_t* uid, int* code,
                          int* status) {
    os::IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(os::TaskOperation::WaitId);
    request.length       = 4;
    request.data[0]      = taskId;
    request.data[1]      = options;
    const int64_t result = services::RequestChannel(os::CoreServiceKind::Task)
                               .result(request, reply);
    if (result < 0) return result;
    *uid    = reply.data[2];
    *code   = reply.data[3];
    *status = reply.data[4];
    return reply.data[0];
}
int64_t __libc_os_wait_child(int64_t taskId, int* status, int options) {
    if (status == nullptr) return -1;
    return os::detail::TaskControl().waitChild(taskId, *status, options);
}
int __libc_os_replace_task_image(const OsExecImageInfo* info) {
    if (info == nullptr) return -1;
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::TaskOperation::ReplaceImage);
    request.length  = 5;
    request.data[0] = reinterpret_cast<uint64_t>(info);
    return services::RequestChannel(os::CoreServiceKind::Task)
        .result(request, reply);
}
int __libc_os_image_interpreter(const void* image, size_t size, char* path,
                                size_t capacity) {
    os::Executable executable;
    const int status = executable.open(image, size);
    return status ? status : executable.getInterpreter(path, capacity);
}
int __libc_os_query_task(uint64_t taskId, OsTaskInfo* info) {
    if (!info) return -1;
    os::wire::TaskInfo value{};
    const int result = os::detail::TaskQuery().query(taskId, value);
    if (!result) os::Runtime::copyBytes(info, &value, sizeof(value));
    return result;
}
int __libc_os_set_credentials(uint64_t taskId, uint32_t uid, uint32_t gid,
                              int effectiveOnly) {
    if (effectiveOnly < 0 || effectiveOnly > 1) return -1;
    return os::detail::TaskControl().setCredentials(taskId, uid, gid,
                                                    effectiveOnly);
}
uint64_t __libc_os_get_task_id() { return os::detail::TaskControl().getId(); }
uint64_t __libc_os_get_thread_id() {
    return os::detail::TaskControl().getThreadId();
}
void __libc_os_exit_task(int status) { os::exitTask(status); }
void* __libc_os_allocate_memory(size_t size) {
    return os::Runtime::allocateMemory(size);
}
int __libc_os_free_memory(void* pointer) {
    return os::Runtime::freeMemory(pointer);
}
uint64_t __libc_os_map_memory(uint64_t address, uint64_t size,
                              uint64_t protection) {
    return os::mapMemory(address, size, protection);
}
int __libc_os_unmap_memory(uint64_t address, uint64_t size) {
    return os::unmapMemory(address, size) ? 0 : -1;
}
int __libc_os_protect_memory(uint64_t address, uint64_t size,
                             uint64_t protection) {
    return os::protectMemory(address, size, protection) ? 0 : -1;
}
uint64_t __libc_os_get_monotonic_time() {
    return os::Sys().readMonotonicTime();
}
uint64_t __libc_os_get_wall_clock_time() {
    return os::Sys().readWallClockTime();
}
uint64_t __libc_os_get_clock_resolution() {
    return os::Sys().readClockResolution();
}

int __libc_os_get_task_identifiers(OsTaskIdentifiers* identifiers,
                                   uint64_t taskId) {
    if (identifiers == nullptr) return -1;
    os::wire::TaskIdentifiers value{};
    const int result = os::detail::TaskControl().getIdentifiers(value, taskId);
    if (result == 0) {
        identifiers->parentId    = value.parentId;
        identifiers->taskGroupId = value.taskGroupId;
        identifiers->sessionId   = value.sessionId;
    }
    return result;
}

int __libc_os_send_signal(uint64_t taskId, uint32_t signal) {
    return os::detail::TaskControl().sendSignal(taskId, signal);
}

int __libc_os_set_task_group(uint64_t taskGroupId, uint64_t taskId) {
    return os::detail::TaskControl().setGroup(taskGroupId, taskId);
}

int64_t __libc_os_create_session() {
    return os::detail::TaskControl().createSession();
}

uint64_t __libc_os_execute_task(const char* path, int argumentCount,
                                const char* const* arguments,
                                uint64_t standardInput, uint64_t standardOutput,
                                uint64_t standardError) {
    return os::detail::TaskControl().executeWithIo(
        path, argumentCount, arguments, standardInput, standardOutput,
        standardError);
}

int __libc_os_get_task_state(uint64_t handle, int* exitCode) {
    if (exitCode == nullptr) return -1;
    return os::detail::TaskControl().getState(handle, *exitCode);
}

int __libc_os_get_task_info(uint64_t handle, OsTaskInfo* info) {
    if (info == nullptr) return -1;
    return os::detail::TaskQuery().getInfo(
        handle, *reinterpret_cast<os::wire::TaskInfo*>(info));
}

int __libc_os_rename_file(uint64_t fs, const char* existing,
                          const char* newPath) {
    os::IpcMessage request{}, reply{};
    if (!existing || !newPath || os::Runtime::stringLength(existing) >= 384 ||
        os::Runtime::stringLength(newPath) >= 384)
        return 0;
    request.type = static_cast<uint64_t>(os::FsRequest::Rename);
    os::Runtime::copyString(reinterpret_cast<char*>(request.data), existing,
                            384);
    os::Runtime::copyString(reinterpret_cast<char*>(request.data) + 384,
                            newPath, 384);
    request.length = 96;
    return os::callService(fs, request, reply) == 0 && reply.data[0] == 0;
}

void __libc_os_runtime_fini() { os::Runtime::runtimeFini(); }

}  // extern "C"
#pragma GCC visibility pop

extern "C" void* __libc_file_from_descriptor(int descriptor) {
    OpenDescription* value = libcDescriptor(descriptor);
    return value ? value->fileObject : nullptr;
}
extern "C" int __libc_file_to_descriptor(void* object) {
    if (!object) return -1;
    auto& file = *static_cast<os::File*>(object);
    if (!file.getOpened()) return -1;
    const auto backend = file.getBackend();
    const int kind = backend == os::File::Backend::Filesystem ? DESCRIPTOR_FILE
                     : backend == os::File::Backend::SharedMemory
                         ? DESCRIPTOR_SHARED_MEMORY
                     : backend == os::File::Backend::Terminal
                         ? ((file.getFlags() & O_ACCMODE) == O_RDONLY
                                ? DESCRIPTOR_TERMINAL_READ
                                : DESCRIPTOR_TERMINAL_WRITE)
                         : ((file.getFlags() & O_ACCMODE) == O_RDONLY
                                ? DESCRIPTOR_PIPE_READ
                                : DESCRIPTOR_PIPE_WRITE);
    const int result = libcAllocateDescriptor(
        file.getHandle(), file.getPath(), file.getFlags(),
        static_cast<DescriptorKind>(kind));
    if (result >= 0) {
        libcDescriptors[result].closeOnExec =
            (file.getFlags() & O_CLOEXEC) != 0;
        file.releaseHandle();
    }
    return result;
}

extern "C" uint64_t __libc_os_mm_anonymous(uint64_t size, uint32_t protection,
                                           uint64_t hint) {
    return os::detail::MappingClient().anonymous(size, protection, true, hint);
}
extern "C" uint64_t __libc_os_mm_shared(uint64_t handle, uint64_t size,
                                        uint32_t protection, uint64_t offset,
                                        uint64_t hint) {
    return os::detail::MappingClient().shared(handle, size, protection, offset,
                                              hint);
}
extern "C" uint64_t __libc_os_mm_file(uint64_t descriptor, uint64_t size,
                                      uint32_t protection, uint64_t offset,
                                      uint64_t hint) {
    return os::detail::MappingClient().file(descriptor, size, protection,
                                            offset, hint);
}
extern "C" int __libc_os_mm_unmap(uint64_t address, uint64_t size) {
    return os::detail::MappingClient().unmap(address, size);
}
extern "C" int __libc_os_mm_protect(uint64_t address, uint64_t size,
                                    uint32_t protection) {
    return os::detail::MappingClient().protect(address, size, protection);
}
extern "C" int __libc_os_mm_sync(uint64_t address, uint64_t size) {
    return os::detail::MappingClient().sync(address, size);
}

extern "C" pid_t tcgetpgrp(int descriptor) {
    OpenDescription* description = libcDescriptor(descriptor);
    if (!description) { errno=EBADF;return -1; }
    if (!isatty(descriptor)) return -1;
    uint64_t group=0;
    if (description->handle) {
        os::detail::PtyClient terminal(description->handle);
        const int status=terminal.queryForegroundGroup(group);
        if(status) {errno=-posixFileResult(status);return -1;}
    } else {
        os::Console terminal;
        const int status=terminal.openCurrent();
        if(status) {errno=-posixFileResult(status);return -1;}
        group=terminal.getForegroundGroup();
    }
    if(group>INT32_MAX) {errno=EOVERFLOW;return -1;}
    return pid_t(group);
}
extern "C" int tcsetpgrp(int descriptor, pid_t group) {
    OpenDescription* description = libcDescriptor(descriptor);
    if (!description) {errno=EBADF;return -1;}
    if (!isatty(descriptor)) return -1;
    if(group<=0) {errno=EINVAL;return -1;}
    if (description->handle) {
        os::detail::PtyClient terminal(description->handle);
        if(!terminal.setForegroundGroup(uint64_t(group))) {
            errno=-posixFileResult(terminal.getError());return -1;
        }
    } else {
        os::Console terminal;
        int status=terminal.openCurrent();
        if(!status)status=terminal.setForegroundGroup(uint64_t(group));
        if(status) {errno=-posixFileResult(status);return -1;}
    }
    return 0;
}

extern "C" int tcgetattr(int descriptor, struct termios* attributes) {
    if (!attributes) {
        errno = EFAULT;
        return -1;
    }
    OpenDescription* description = libcDescriptor(descriptor);
    if (!description) {
        errno = EBADF;
        return -1;
    }
    if (!isatty(descriptor)) return -1;
    termios value{};
    value.c_cflag    = CS8;
    value.c_cc[VMIN] = 1;
    if (description->handle) {
        os::detail::PtyClient terminal(description->handle);
        if (!terminal.refreshAttributes()) {
            errno = EIO;
            return -1;
        }
        value.c_lflag = (terminal.getCanonicalMode() ? ICANON : 0) |
                        (terminal.getEcho() ? ECHO : 0) |
                        (terminal.getSignalMode() ? ISIG : 0);
        value.c_cc[VERASE] = terminal.getEraseCharacter();
        value.c_cc[VINTR]  = terminal.getInterruptCharacter();
        value.c_cc[VEOF]   = terminal.getEofCharacter();
        value.c_cc[VSUSP]  = terminal.getSuspendCharacter();
        value.c_cc[VQUIT]  = terminal.getQuitCharacter();
    } else {
        os::Console terminal;
        const int status = terminal.openCurrent();
        if (status) {
            errno = -posixFileResult(status);
            return -1;
        }
        value.c_lflag      = ICANON | ISIG | (terminal.getEcho() ? ECHO : 0);
        value.c_cc[VERASE] = 127;
        value.c_cc[VINTR]  = 3;
        value.c_cc[VEOF]   = 4;
        value.c_cc[VSUSP]  = 26;
        value.c_cc[VQUIT]  = 28;
    }
    *attributes = value;
    return 0;
}
extern "C" int tcsetattr(int descriptor, int action,
                         const struct termios* attributes) {
    if (!attributes) {
        errno = EFAULT;
        return -1;
    }
    if (action != TCSANOW) {
        errno = action == TCSADRAIN || action == TCSAFLUSH ? ENOTSUP : EINVAL;
        return -1;
    }
    termios previous{};
    if (tcgetattr(descriptor, &previous)) return -1;
    if (attributes->c_iflag || attributes->c_oflag ||
        attributes->c_cflag != CS8 ||
        (attributes->c_lflag & ~(ICANON | ISIG | ECHO)) || attributes->c_line ||
        attributes->__c_ispeed || attributes->__c_ospeed) {
        errno = ENOTSUP;
        return -1;
    }
    for (unsigned index = 0; index < NCCS; ++index)
        if (index != VERASE && index != VINTR && index != VEOF &&
            index != VSUSP && index != VQUIT &&
            attributes->c_cc[index] != previous.c_cc[index]) {
            errno = ENOTSUP;
            return -1;
        }
    OpenDescription* description = libcDescriptor(descriptor);
    if (!description) {
        errno = EBADF;
        return -1;
    }
    if (description->handle) {
        os::detail::PtyClient terminal(description->handle);
        if (!terminal.refreshAttributes()) {
            errno = EIO;
            return -1;
        }
        terminal.setCanonicalMode(attributes->c_lflag & ICANON);
        terminal.setEcho(attributes->c_lflag & ECHO);
        terminal.setSignalMode(attributes->c_lflag & ISIG);
        terminal.setEraseCharacter(attributes->c_cc[VERASE]);
        terminal.setInterruptCharacter(attributes->c_cc[VINTR]);
        terminal.setEofCharacter(attributes->c_cc[VEOF]);
        terminal.setSuspendCharacter(attributes->c_cc[VSUSP]);
        terminal.setQuitCharacter(attributes->c_cc[VQUIT]);
        if (!terminal.applyAttributes()) {
            errno = EIO;
            return -1;
        }
        return 0;
    }
    if (!(attributes->c_lflag & ICANON) || !(attributes->c_lflag & ISIG)) {
        errno = ENOTSUP;
        return -1;
    }
    for (unsigned index = 0; index < NCCS; ++index)
        if (attributes->c_cc[index] != previous.c_cc[index]) {
            errno = ENOTSUP;
            return -1;
        }
    os::Console terminal;
    int status = terminal.openCurrent();
    if (!status) status = terminal.setEcho(attributes->c_lflag & ECHO);
    if (status) {
        errno = -posixFileResult(status);
        return -1;
    }
    return 0;
}

extern "C" int __libc_os_mm_policy(unsigned operation,uint64_t address,uint64_t size,uint64_t argument) {
    const int result=os::detail::MappingClient().policy(operation,address,size,argument);
    return result<0?-int(posixFileResult(result)):result;
}

extern "C" int posix_devctl(int descriptor, int command, void* data,
                             size_t length, int* deviceInfo) {
    const int saved = errno;
    const auto finish = [saved](int error) { errno = saved; return error; };
    OpenDescription* description = libcDescriptor(descriptor);
    if (!description) return finish(EBADF);
    if (!data || !deviceInfo || !__libc_os_accessible(deviceInfo,sizeof(*deviceInfo),1)) return finish(EFAULT);
    if (command != OSINFO_DEVCTL_TTY_GET_SIZE && command != OSINFO_DEVCTL_TTY_SET_SIZE)
        return finish(ENOTTY);
    if (length != sizeof(osinfo_devctl_tty_size)) return finish(EINVAL);
    if(!__libc_os_accessible(data,length,command==OSINFO_DEVCTL_TTY_GET_SIZE))return finish(EFAULT);
    if (command == OSINFO_DEVCTL_TTY_SET_SIZE &&
        (description->flags & O_ACCMODE) == O_RDONLY) return finish(EACCES);
    if (!description->handle || !isatty(descriptor)) return finish(ENOTTY);
    os::Ipc::Handle endpoint(os::detail::RegistryClient().lookup(os::ttyServiceName, 100));
    if (!endpoint.getValue()) return finish(ENODEV);
    os::IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::TtyRequest::DeviceControl);
    request.length = command == OSINFO_DEVCTL_TTY_SET_SIZE ? 4 : 2;
    request.data[0] = command;
    request.data[1] = length;
    request.handleCount = 1;
    request.handles[0] = description->handle;
    if (command == OSINFO_DEVCTL_TTY_SET_SIZE)
        os::Runtime::copyBytes(&request.data[2], data, length);
    if (os::callService(endpoint.getValue(), request, reply) || reply.length != 4)
        return finish(EIO);
    if (reply.data[0]) return finish(static_cast<int>(reply.data[0]));
    *deviceInfo = static_cast<int>(reply.data[1]);
    if (!*deviceInfo && command == OSINFO_DEVCTL_TTY_GET_SIZE)
        os::Runtime::copyBytes(data, &reply.data[2], length);
    return finish(0);
}

extern "C" int64_t __libc_os_mm_offset(uint64_t address,uint64_t size,
                                        uint64_t* offset,uint64_t* contiguous,unsigned* rights) {
    return os::detail::MappingClient().memoryOffset(address,size,*offset,*contiguous,*rights);
}

extern "C" int64_t __libc_os_typed_open(const char* name,int flags,unsigned policy) {
    return os::detail::MappingClient().openTyped(name,flags,policy);
}
extern "C" int64_t __libc_os_typed_allocate(uint64_t handle,uint64_t size) {
    return os::detail::MappingClient().allocateTyped(handle,size);
}
extern "C" int __libc_os_typed_info(uint64_t handle,uint64_t* available) {
    return os::detail::MappingClient().typedInfo(handle,*available);
}
