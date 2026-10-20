#include "os_adapter.h"

#include "os/abi.h"
#include "os/file.h"
#include "os/mm.h"
#include "os/pty.h"
#include "os/task.h"
#include "os/elf.h"
#include "os/service.h"
#include "os/terminal.h"
#include <new>
#include <cstdlib>
#include <sys/stat.h>
#include <cerrno>
extern "C" {
#include "internal.h"
}

static_assert(sizeof(OsDirectoryEntry) == sizeof(os::DirectoryEntry));
static_assert(sizeof(OsPipeHandles) == sizeof(os::PipeHandles));
static_assert(sizeof(OsFileOperationResult) == sizeof(os::FileOperationResult));
static_assert(sizeof(OsTaskIdentifiers) == sizeof(os::TaskIdentifiers));
static_assert(sizeof(OsTaskInfo) == sizeof(os::TaskInfo));
static_assert(sizeof(OsSignalAction) == sizeof(os::SignalAction));
static_assert(sizeof(OsMessageQueueAttributes) ==
              sizeof(os::MessageQueueAttributes));
static_assert(sizeof(OsExecImageInfo) == sizeof(os::ExecImageInfo));

#pragma GCC visibility push(hidden)
extern "C" {

void __libc_os_bind_environment(char*** environment) {
    os::Runtime::bindEnvironment(environment);
}

void* __libc_file_adopt(uint64_t fs,uint64_t handle,const char* path,int flags,int kind) {
    auto* result=static_cast<os::File*>(malloc(sizeof(os::File)));
    if (!result) return nullptr;
    new (result) os::File;
    if (kind==DESCRIPTOR_FILE) *result=os::File::adopt(fs,handle,path,flags);
    else *result=os::File::adoptHandle(handle,
        kind==DESCRIPTOR_SHARED_MEMORY?os::File::Backend::SharedMemory:
        (kind==DESCRIPTOR_TERMINAL_READ || kind==DESCRIPTOR_TERMINAL_WRITE) && !handle?os::File::Backend::Terminal:os::File::Backend::Pipe,flags);
    return result;
}
uint64_t __libc_file_open(uint64_t fs,const char* path,int flags,mode_t mode,int* error) {
    os::File file(fs); const int result=file.open(path,flags,mode);
    if (error) *error=result;
    return result?0:file.releaseNativeHandle();
}
int __libc_file_close(void* object) { auto* file=static_cast<os::File*>(object); const int status=file->close(); file->~File(); free(file); return status==0; }
void __libc_file_rebind(void* object,uint64_t handle) { if (object) static_cast<os::File*>(object)->rebindNativeHandle(handle); }
uint64_t __libc_file_handle(void* object) { return object?static_cast<os::File*>(object)->getNativeHandle():0; }
void __libc_file_set_flags(void* object,int flags) { if (object) static_cast<os::File*>(object)->setStatusFlags(flags); }
ssize_t __libc_file_read(void* object,void* data,size_t size) { return object?static_cast<os::File*>(object)->read(data,size):-9; }
ssize_t __libc_file_write(void* object,const void* data,size_t size) { return object?static_cast<os::File*>(object)->write(data,size):-9; }
ssize_t __libc_file_pread(void* object,void* data,size_t size,off_t offset) { return object?static_cast<os::File*>(object)->pread(data,size,offset):-9; }
ssize_t __libc_file_pwrite(void* object,const void* data,size_t size,off_t offset) { return object?static_cast<os::File*>(object)->pwrite(data,size,offset):-9; }
off_t __libc_file_seek(void* object,off_t offset,int whence) { return object?static_cast<os::File*>(object)->lseek(offset,whence):-9; }
int __libc_file_fsync(void* object) { return object?static_cast<os::File*>(object)->fsync():-9; }

int __libc_file_stat(void* object, struct stat* result) {
    if (!object) return -EBADF;
    if (!result) return -EFAULT;
    auto& file = *static_cast<os::File*>(object);
    const int error = file.refresh();
    if (error < 0) return error;
    *result = {};
    result->st_mode = file.getMode();
    result->st_dev = file.getDevice();
    result->st_ino = file.getInode();
    result->st_nlink = file.getLinks();
    result->st_uid = file.getUid();
    result->st_gid = file.getGid();
    result->st_rdev = file.getSpecialDevice();
    result->st_size = file.getSize();
    result->st_blksize = 4096;
    result->st_blocks = (result->st_size + 511) / 512;
    result->st_atim.tv_sec = file.getAccessTime();
    result->st_mtim.tv_sec = file.getModificationTime();
    result->st_ctim.tv_sec = file.getChangeTime();
    return 0;
}

int __libc_os_is_terminal_handle(uint64_t handle) {
    os::TerminalAttributes attributes{};
    return handle != 0 && os::Pty::getPtyAttributes(handle, attributes);
}

uint64_t __libc_os_get_fs_service() {
    return os::ServiceRegistry::lookupService(os::getBootstrapHandle(), os::fsServiceName,
                             1000);
}

uint64_t __libc_os_open_file(uint64_t fs, const char* path) {
    return ([&]() -> uint64_t { os::File opened(fs); return opened.open(path, O_RDONLY) == 0 ? opened.releaseNativeHandle() : 0; }());
}

int __libc_os_close_file(uint64_t fs, uint64_t descriptor) {
    return (os::File::borrow(fs, descriptor).close() == 0);
}

int64_t __libc_os_read_file(uint64_t fs, uint64_t descriptor, void* buffer,
                     size_t size) {
    return os::File::borrow(fs, descriptor).read(buffer, size);
}

int64_t __libc_os_write_file(uint64_t fs, uint64_t descriptor, const void* buffer,
                      size_t size) {
    return os::File::borrow(fs, descriptor).write(buffer, size);
}

int __libc_os_seek_file(uint64_t fs, uint64_t descriptor, uint64_t offset) {
    return (os::File::borrow(fs, descriptor).lseek(offset, 0) >= 0);
}

int64_t __libc_os_seek_file_position(uint64_t fs, uint64_t descriptor,
                              int64_t offset, int whence) {
    return os::File::borrow(fs, descriptor).lseek(offset, whence);
}

int __libc_os_clone_file_descriptors(uint64_t fs, uint64_t parentTaskId,
                              uint64_t* oldDescriptors,
                              uint64_t* newDescriptors, size_t capacity) {
    return os::File(fs).cloneDescriptors(parentTaskId, oldDescriptors, newDescriptors, capacity);
}

int __libc_os_create_file(uint64_t fs, const char* path) {
    return (os::File(fs).open(path, O_WRONLY | O_CREAT | O_EXCL, 0666) == 0);
}

int __libc_os_make_directory(uint64_t fs, const char* path) {
    return (os::File(fs).mkdir(path, 0755) == 0);
}

int __libc_os_remove_path(uint64_t fs, const char* path) {
    return (os::File(fs).unlink(path) == 0);
}

int __libc_os_truncate_file(uint64_t fs, const char* path) {
    return (os::File(fs).truncate(path, 0) == 0);
}

int __libc_os_stat_file(uint64_t fs, const char* path, OsFileStat* status) {
    if (status == nullptr) return 0;
    os::File value(fs);
    if (!(value.refresh(path) == 0)) return 0;
    status->type = static_cast<uint64_t>(value.getType());
    status->size = value.getSize();
    status->mode = value.getMode();
    status->inode = value.getInode();
    status->links = value.getLinks();
    status->uid = value.getUid();
    status->gid = value.getGid();
    status->accessTime = value.getAccessTime();
    status->modificationTime = value.getModificationTime();
    status->changeTime = value.getChangeTime();
    status->device = value.getDevice();
    status->specialDevice = value.getSpecialDevice();
    return 1;
}

int __libc_os_link_file(uint64_t fs, const char* existing, const char* newPath) {
    return (os::File(fs).link(existing, newPath) == 0);
}
int __libc_os_change_file_mode(uint64_t fs, const char* path, uint32_t mode) {
    return (os::File(fs, path).chmod(mode) == 0);
}
int __libc_os_change_file_owner(uint64_t fs, const char* path, uint32_t uid,
                         uint32_t gid) {
    return (os::File(fs, path).chown(uid, gid) == 0);
}
int __libc_os_change_file_times(uint64_t fs, const char* path, uint64_t accessTime,
                         uint64_t modificationTime) {
    return ([&]() { const timespec times[2] = {{static_cast<time_t>(accessTime), 0}, {static_cast<time_t>(modificationTime), 0}}; return os::File(fs, path).utimens(times) == 0; }());
}

int64_t __libc_os_read_directory_batch(uint64_t fs, const char* path, uint64_t index,
                                OsDirectoryEntry* entries, size_t capacity) {
    return os::File(fs).readDirectoryBatch(path, index, reinterpret_cast<os::DirectoryEntry*>(entries), capacity);
}

uint64_t __libc_os_create_endpoint() { return os::Endpoint::create(); }

int __libc_os_read_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                       size_t size, uint64_t completionEndpoint,
                       uint64_t requestId) {
    return os::File::borrow(fs, descriptor).readAsync(offset, size, completionEndpoint, requestId)
               ? 1
               : 0;
}

int __libc_os_write_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                        const void* buffer, size_t size,
                        uint64_t completionEndpoint, uint64_t requestId) {
    return os::File::borrow(fs, descriptor).writeAsync(offset, buffer, size, completionEndpoint, requestId)
               ? 1
               : 0;
}

int __libc_os_wait_file_operation(uint64_t completionEndpoint,
                           OsFileOperationResult* result, void* buffer,
                           size_t capacity) {
    if (result == nullptr) return 0;
    return os::File::waitOperation(completionEndpoint, *reinterpret_cast<os::FileOperationResult*>(result), buffer, capacity)
               ? 1
               : 0;
}

int __libc_os_create_pipe(OsPipeHandles* handles) {
    if (handles == nullptr) return 0;
    os::PipeHandles value{};
    if (!os::Pipe::createPipe(value)) return 0;
    handles->readHandle = value.readHandle;
    handles->writeHandle = value.writeHandle;
    return 1;
}

int64_t __libc_os_read_pipe(uint64_t handle, void* buffer, size_t size) {
    return os::Pipe::readPipe(handle, buffer, size);
}

int64_t __libc_os_write_pipe(uint64_t handle, const void* buffer, size_t size) {
    return os::Pipe::writePipe(handle, buffer, size);
}

uint64_t __libc_os_get_standard_handle(uint32_t index) {
    return os::getStandardHandle(index);
}

int __libc_os_close_handle(uint64_t handle) { return os::closeHandle(handle); }

int64_t __libc_os_read_terminal(void* buffer, size_t size) {
    return os::Terminal::read(buffer, size);
}

int64_t __libc_os_write_terminal(const void* buffer, size_t size) {
    return os::Terminal::write(buffer, size);
}

uint32_t __libc_os_get_terminal_id() { return os::Terminal::getId(); }

void __libc_os_set_terminal_id(uint32_t terminalId) {
    os::Terminal::setId(terminalId);
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
    return os::Task::waitThread(threadId, *exitCode, noHang != 0);
}
int __libc_os_wait_address(volatile uint32_t* address, uint32_t expected) {
    return os::waitAddress(address, expected);
}
int __libc_os_wake_address(volatile uint32_t* address, uint32_t count) {
    return os::wakeAddress(address, count);
}
int __libc_os_set_signal_action(uint32_t signal, const OsSignalAction* action,
                         OsSignalAction* oldAction) {
    return os::Task::setSignalAction(
        signal, reinterpret_cast<const os::SignalAction*>(action),
        reinterpret_cast<os::SignalAction*>(oldAction));
}
int __libc_os_set_signal_mask(int how, uint64_t mask, uint64_t* oldMask) {
    if (oldMask == nullptr) return -1;
    return os::Task::setSignalMask(how, mask, *oldMask);
}
int __libc_os_get_pending_signals(uint64_t* mask) {
    if (mask == nullptr) return -1;
    return os::Task::getPendingSignals(*mask);
}
int __libc_os_wait_signal(uint64_t mask) { return os::Task::waitSignal(mask); }
void __libc_os_signal_return(void* frame) { os::Task::signalReturn(frame); }
uint64_t __libc_os_open_message_queue(const char* name, int flags,
                               uint32_t maximumMessages,
                               uint32_t messageSize) {
    return os::MessageQueue::openMessageQueue(name, flags, maximumMessages, messageSize);
}
int __libc_os_unlink_message_queue(const char* name) {
    return os::MessageQueue::unlinkMessageQueue(name);
}
int __libc_os_send_message_queue(uint64_t handle, const void* data, size_t size,
                          uint32_t priority) {
    return os::MessageQueue::sendMessageQueue(handle, data, size, priority);
}
int64_t __libc_os_receive_message_queue(uint64_t handle, void* data, size_t capacity,
                                 uint32_t* priority) {
    if (priority == nullptr) return -1;
    return os::MessageQueue::receiveMessageQueue(handle, data, capacity, *priority);
}
int __libc_os_get_message_queue_attributes(uint64_t handle,
                                    OsMessageQueueAttributes* attributes) {
    if (attributes == nullptr) return -1;
    return os::MessageQueue::getMessageQueueAttributes(
        handle, *reinterpret_cast<os::MessageQueueAttributes*>(attributes));
}
int __libc_os_set_message_queue_flags(uint64_t handle, uint64_t flags,
                               uint64_t* oldFlags) {
    if (oldFlags == nullptr) return -1;
    return os::MessageQueue::setMessageQueueFlags(handle, flags, *oldFlags);
}
uint64_t __libc_os_open_shared_memory(const char* name, int flags) {
    return os::VirtualMemory::openShared(name, flags);
}
int __libc_os_unlink_shared_memory(const char* name) {
    return os::VirtualMemory::unlinkShared(name);
}
int __libc_os_resize_shared_memory(uint64_t handle, uint64_t size) {
    return os::Memory::resizeSharedMemory(handle, size);
}
uint64_t __libc_os_map_shared_memory(uint64_t handle, uint64_t address,
                              uint64_t size, uint64_t offset,
                              uint64_t protection) {
    return os::Memory::mapSharedMemory(handle, address, size, offset, protection);
}
int __libc_os_send_thread_signal(uint64_t threadId, uint32_t signal) {
    return os::Task::sendThreadSignal(threadId, signal);
}
int __libc_os_register_message_queue_notification(uint64_t handle, int type,
                                           uint32_t signal) {
    return os::MessageQueue::registerMessageQueueNotification(handle, type, signal);
}
int64_t __libc_os_fork_task() { return os::forkTask(); }
int64_t __libc_os_wait_child(int64_t taskId, int* status, int options) {
    if (status == nullptr) return -1;
    return os::Task::waitChild(taskId, *status, options);
}
int __libc_os_replace_task_image(const OsExecImageInfo* info) {
    if (info == nullptr) return -1;
    return os::replaceTaskImage(
        *reinterpret_cast<const os::ExecImageInfo*>(info));
}
int __libc_os_image_interpreter(const void* image, size_t size, char* path, size_t capacity) {
    return os::Executable::interpreter(image, size, path, capacity);
}
int __libc_os_query_task(uint64_t taskId, OsTaskInfo* info) {
    if (!info) return -1;
    os::TaskInfo value{};
    const int result = os::Task::query(taskId, value);
    if (!result) os::Runtime::copyBytes(info, &value, sizeof(value));
    return result;
}
int __libc_os_set_credentials(uint64_t taskId, uint32_t uid, uint32_t gid, int effectiveOnly) {
    if (effectiveOnly < 0 || effectiveOnly > 1) return -1;
    return os::Task::setCredentials(taskId, uid, gid, effectiveOnly);
}
uint64_t __libc_os_get_task_id() { return os::Task::getId(); }
uint64_t __libc_os_get_thread_id() { return os::Task::getThreadId(); }
void __libc_os_exit_task(int status) { os::exitTask(status); }
void* __libc_os_allocate_memory(size_t size) { return os::Memory::allocateMemory(size); }
int __libc_os_free_memory(void* pointer) { return os::Memory::freeMemory(pointer); }
uint64_t __libc_os_map_memory(uint64_t address, uint64_t size, uint64_t protection) {
    return os::mapMemory(address, size, protection);
}
int __libc_os_unmap_memory(uint64_t address, uint64_t size) {
    return os::unmapMemory(address, size) ? 0 : -1;
}
int __libc_os_protect_memory(uint64_t address, uint64_t size, uint64_t protection) {
    return os::protectMemory(address, size, protection) ? 0 : -1;
}
uint64_t __libc_os_get_monotonic_time() { return os::Clock::getMonotonicTime(); }
uint64_t __libc_os_get_wall_clock_time() { return os::Clock::getWallClockTime(); }
uint64_t __libc_os_get_clock_resolution() { return os::Clock::getClockResolution(); }

int __libc_os_get_task_identifiers(OsTaskIdentifiers* identifiers, uint64_t taskId) {
    if (identifiers == nullptr) return -1;
    os::TaskIdentifiers value{};
    const int result = os::Task::getIdentifiers(value, taskId);
    if (result == 0) {
        identifiers->parentId = value.parentId;
        identifiers->taskGroupId = value.taskGroupId;
        identifiers->sessionId = value.sessionId;
    }
    return result;
}

int __libc_os_send_signal(uint64_t taskId, uint32_t signal) {
    return os::Task::sendSignal(taskId, signal);
}

int __libc_os_set_task_group(uint64_t taskGroupId, uint64_t taskId) {
    return os::Task::setGroup(taskGroupId, taskId);
}

int64_t __libc_os_create_session() { return os::Task::createSession(); }

uint64_t __libc_os_execute_task(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInput,
                            uint64_t standardOutput,
                            uint64_t standardError) {
    return os::Task::executeWithIo(path, argumentCount, arguments, standardInput,
                             standardOutput, standardError);
}

int __libc_os_get_task_state(uint64_t handle, int* exitCode) {
    if (exitCode == nullptr) return -1;
    return os::Task::getState(handle, *exitCode);
}

int __libc_os_get_task_info(uint64_t handle, OsTaskInfo* info) {
    if (info == nullptr) return -1;
    return os::Task::getInfo(handle,
                              *reinterpret_cast<os::TaskInfo*>(info));
}

int __libc_os_rename_file(uint64_t fs, const char* existing, const char* newPath) {
    os::IpcMessage request{}, reply{};
    if (!existing || !newPath || os::Runtime::stringLength(existing) >= 384 || os::Runtime::stringLength(newPath) >= 384) return 0;
    request.type = static_cast<uint64_t>(os::FsRequest::Rename);
    os::Runtime::copyString(reinterpret_cast<char*>(request.data), existing, 384);
    os::Runtime::copyString(reinterpret_cast<char*>(request.data) + 384, newPath, 384);
    request.length = 96;
    return os::callService(fs, request, reply) == 0 && reply.data[0] == 0;
}


void __libc_os_runtime_fini() { os::Runtime::runtimeFini(); }

}  // extern "C"
#pragma GCC visibility pop

namespace os {
File* File::fromDescriptor(int descriptor) {
    OpenDescription* value=libcDescriptor(descriptor);
    return value?static_cast<File*>(value->fileObject):nullptr;
}
int File::toDescriptor() {
    for (int i=0;i<LIBC_DESCRIPTOR_CAPACITY;++i) {
        FileDescriptor* slot=libcDescriptorSlot(i);
        if (slot && slot->description->fileObject==this) return i;
    }
    if (!getNativeHandle() && backend_!=Backend::Terminal) return -1;
    const int kind=backend_==Backend::Filesystem?DESCRIPTOR_FILE:backend_==Backend::SharedMemory?DESCRIPTOR_SHARED_MEMORY:
        backend_==Backend::Terminal?((getFlags()&O_ACCMODE)==O_RDONLY?DESCRIPTOR_TERMINAL_READ:DESCRIPTOR_TERMINAL_WRITE):
        ((getFlags()&O_ACCMODE)==O_RDONLY?DESCRIPTOR_PIPE_READ:DESCRIPTOR_PIPE_WRITE);
    const int result=libcAllocateDescriptor(getNativeHandle(),getPath(),getFlags(),static_cast<DescriptorKind>(kind));
    if (result>=0) {
        libcDescriptors[result].closeOnExec=(getFlags()&O_CLOEXEC)!=0;
        releaseNativeHandle();
    }
    return result;
}
}

extern "C" uint64_t __libc_os_mm_anonymous(uint64_t size, uint32_t protection, uint64_t hint) {
    return os::VirtualMemory::anonymous(size, protection, true, hint);
}
extern "C" uint64_t __libc_os_mm_shared(uint64_t handle, uint64_t size, uint32_t protection, uint64_t offset, uint64_t hint) {
    return os::VirtualMemory::shared(handle, size, protection, offset, hint);
}
extern "C" uint64_t __libc_os_mm_file(uint64_t descriptor, uint64_t size, uint32_t protection, uint64_t offset, uint64_t hint) {
    return os::VirtualMemory::file(descriptor, size, protection, offset, hint);
}
extern "C" int __libc_os_mm_unmap(uint64_t address, uint64_t size) { return os::VirtualMemory::unmap(address, size); }
extern "C" int __libc_os_mm_protect(uint64_t address, uint64_t size, uint32_t protection) { return os::VirtualMemory::protect(address, size, protection); }
extern "C" int __libc_os_mm_sync(uint64_t address, uint64_t size) { return os::VirtualMemory::sync(address, size); }
