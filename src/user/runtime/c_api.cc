#include "os/c_api.h"

#include "os/abi.h"
#include "os/file.h"
#include "os/process.h"
#include "os/service.h"
#include "os/terminal.h"

static_assert(sizeof(OsFileStat) == sizeof(os::FileStat));
static_assert(sizeof(OsDirectoryEntry) == sizeof(os::DirectoryEntry));
static_assert(sizeof(OsPipeHandles) == sizeof(os::PipeHandles));
static_assert(sizeof(OsFileOperationResult) == sizeof(os::FileOperationResult));
static_assert(sizeof(OsProcessIdentifiers) == sizeof(os::ProcessIdentifiers));
static_assert(sizeof(OsProcessInfo) == sizeof(os::ProcessInfo));
static_assert(sizeof(OsSignalAction) == sizeof(os::SignalAction));
static_assert(sizeof(OsMessageQueueAttributes) ==
              sizeof(os::MessageQueueAttributes));
static_assert(sizeof(OsExecImageInfo) == sizeof(os::ExecImageInfo));
static_assert(OS_POSIX_TERMINAL_JOB_CONTROL ==
              static_cast<uint64_t>(os::PosixOperation::TerminalJobControl));

extern "C" {

uint64_t os_get_fs_service() {
    return os::lookupService(os::getBootstrapHandle(), os::fsServiceName,
                             1000);
}

uint64_t os_open_file(uint64_t fs, const char* path) {
    return os::openFile(fs, path);
}

int os_close_file(uint64_t fs, uint64_t descriptor) {
    return os::closeFile(fs, descriptor);
}

int64_t os_read_file(uint64_t fs, uint64_t descriptor, void* buffer,
                     size_t size) {
    return os::readFile(fs, descriptor, buffer, size);
}

int64_t os_write_file(uint64_t fs, uint64_t descriptor, const void* buffer,
                      size_t size) {
    return os::writeFile(fs, descriptor, buffer, size);
}

int os_seek_file(uint64_t fs, uint64_t descriptor, uint64_t offset) {
    return os::seekFile(fs, descriptor, offset);
}

int64_t os_seek_file_position(uint64_t fs, uint64_t descriptor,
                              int64_t offset, int whence) {
    return os::seekFilePosition(fs, descriptor, offset, whence);
}

int os_clone_file_descriptors(uint64_t fs, uint64_t parentProcessId,
                              uint64_t* oldDescriptors,
                              uint64_t* newDescriptors, size_t capacity) {
    return os::cloneFileDescriptors(fs, parentProcessId, oldDescriptors,
                                    newDescriptors, capacity);
}

int os_create_file(uint64_t fs, const char* path) {
    return os::createFile(fs, path);
}

int os_make_directory(uint64_t fs, const char* path) {
    return os::makeDirectory(fs, path);
}

int os_remove_path(uint64_t fs, const char* path) {
    return os::removePath(fs, path);
}

int os_truncate_file(uint64_t fs, const char* path) {
    return os::truncateFile(fs, path);
}

int os_stat_file(uint64_t fs, const char* path, OsFileStat* status) {
    if (status == nullptr) return 0;
    os::FileStat value{};
    if (!os::statFile(fs, path, value)) return 0;
    status->type = static_cast<uint64_t>(value.type);
    status->size = value.size;
    status->mode = value.mode;
    status->inode = value.inode;
    status->links = value.links;
    status->uid = value.uid;
    status->gid = value.gid;
    status->accessTime = value.accessTime;
    status->modificationTime = value.modificationTime;
    status->changeTime = value.changeTime;
    return 1;
}

int os_link_file(uint64_t fs, const char* existing, const char* newPath) {
    return os::linkFile(fs, existing, newPath);
}
int os_change_file_mode(uint64_t fs, const char* path, uint32_t mode) {
    return os::changeFileMode(fs, path, mode);
}
int os_change_file_owner(uint64_t fs, const char* path, uint32_t uid,
                         uint32_t gid) {
    return os::changeFileOwner(fs, path, uid, gid);
}
int os_change_file_times(uint64_t fs, const char* path, uint64_t accessTime,
                         uint64_t modificationTime) {
    return os::changeFileTimes(fs, path, accessTime, modificationTime);
}

int64_t os_read_directory_batch(uint64_t fs, const char* path, uint64_t index,
                                OsDirectoryEntry* entries, size_t capacity) {
    return os::readDirectoryBatch(
        fs, path, index, reinterpret_cast<os::DirectoryEntry*>(entries),
        capacity);
}

uint64_t os_create_endpoint() { return os::createEndpoint(); }

int os_read_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                       size_t size, uint64_t completionEndpoint,
                       uint64_t requestId) {
    return os::readFileAsync(fs, descriptor, offset, size, completionEndpoint,
                             requestId)
               ? 1
               : 0;
}

int os_write_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                        const void* buffer, size_t size,
                        uint64_t completionEndpoint, uint64_t requestId) {
    return os::writeFileAsync(fs, descriptor, offset, buffer, size,
                              completionEndpoint, requestId)
               ? 1
               : 0;
}

int os_wait_file_operation(uint64_t completionEndpoint,
                           OsFileOperationResult* result, void* buffer,
                           size_t capacity) {
    if (result == nullptr) return 0;
    return os::waitFileOperation(
               completionEndpoint,
               *reinterpret_cast<os::FileOperationResult*>(result), buffer,
               capacity)
               ? 1
               : 0;
}

int os_create_pipe(OsPipeHandles* handles) {
    if (handles == nullptr) return 0;
    os::PipeHandles value{};
    if (!os::createPipe(value)) return 0;
    handles->readHandle = value.readHandle;
    handles->writeHandle = value.writeHandle;
    return 1;
}

int64_t os_read_pipe(uint64_t handle, void* buffer, size_t size) {
    return os::readPipe(handle, buffer, size);
}

int64_t os_write_pipe(uint64_t handle, const void* buffer, size_t size) {
    return os::writePipe(handle, buffer, size);
}

uint64_t os_get_standard_handle(uint32_t index) {
    return os::getStandardHandle(index);
}

int os_close_handle(uint64_t handle) { return os::closeHandle(handle); }

int64_t os_read_terminal(void* buffer, size_t size) {
    return os::readTerminal(buffer, size);
}

int64_t os_write_terminal(const void* buffer, size_t size) {
    return os::writeTerminal(buffer, size);
}

void os_yield() { os::yield(); }
void os_sleep(uint64_t milliseconds) { os::sleep(milliseconds); }
void os_exit_thread(int status) { os::exitThread(status); }
uint64_t os_create_thread(void (*entry)(void*), void* argument,
                          uint64_t stackPointer) {
    return os::createThread(entry, stackPointer, argument);
}
int os_wait_thread(uint64_t threadId, int* exitCode, int noHang) {
    if (exitCode == nullptr) return -1;
    return os::waitThread(threadId, *exitCode, noHang != 0);
}
int os_wait_address(volatile uint32_t* address, uint32_t expected) {
    return os::waitAddress(address, expected);
}
int os_wake_address(volatile uint32_t* address, uint32_t count) {
    return os::wakeAddress(address, count);
}
int os_set_signal_action(uint32_t signal, const OsSignalAction* action,
                         OsSignalAction* oldAction) {
    return os::setSignalAction(
        signal, reinterpret_cast<const os::SignalAction*>(action),
        reinterpret_cast<os::SignalAction*>(oldAction));
}
int os_set_signal_mask(int how, uint64_t mask, uint64_t* oldMask) {
    if (oldMask == nullptr) return -1;
    return os::setSignalMask(how, mask, *oldMask);
}
int os_get_pending_signals(uint64_t* mask) {
    if (mask == nullptr) return -1;
    return os::getPendingSignals(*mask);
}
int os_wait_signal(uint64_t mask) { return os::waitSignal(mask); }
void os_signal_return(void* frame) { os::signalReturn(frame); }
uint64_t os_open_message_queue(const char* name, int flags,
                               uint32_t maximumMessages,
                               uint32_t messageSize) {
    return os::openMessageQueue(name, flags, maximumMessages, messageSize);
}
int os_unlink_message_queue(const char* name) {
    return os::unlinkMessageQueue(name);
}
int os_send_message_queue(uint64_t handle, const void* data, size_t size,
                          uint32_t priority) {
    return os::sendMessageQueue(handle, data, size, priority);
}
int64_t os_receive_message_queue(uint64_t handle, void* data, size_t capacity,
                                 uint32_t* priority) {
    if (priority == nullptr) return -1;
    return os::receiveMessageQueue(handle, data, capacity, *priority);
}
int os_get_message_queue_attributes(uint64_t handle,
                                    OsMessageQueueAttributes* attributes) {
    if (attributes == nullptr) return -1;
    return os::getMessageQueueAttributes(
        handle, *reinterpret_cast<os::MessageQueueAttributes*>(attributes));
}
int os_set_message_queue_flags(uint64_t handle, uint64_t flags,
                               uint64_t* oldFlags) {
    if (oldFlags == nullptr) return -1;
    return os::setMessageQueueFlags(handle, flags, *oldFlags);
}
uint64_t os_open_shared_memory(const char* name, int flags) {
    return os::openSharedMemory(name, flags);
}
int os_unlink_shared_memory(const char* name) {
    return os::unlinkSharedMemory(name);
}
int os_resize_shared_memory(uint64_t handle, uint64_t size) {
    return os::resizeSharedMemory(handle, size);
}
uint64_t os_map_shared_memory(uint64_t handle, uint64_t address,
                              uint64_t size, uint64_t offset,
                              uint64_t protection) {
    return os::mapSharedMemory(handle, address, size, offset, protection);
}
int os_send_thread_signal(uint64_t threadId, uint32_t signal) {
    return os::sendThreadSignal(threadId, signal);
}
int os_register_message_queue_notification(uint64_t handle, int type,
                                           uint32_t signal) {
    return os::registerMessageQueueNotification(handle, type, signal);
}
int64_t os_fork_process() { return os::forkProcess(); }
int64_t os_wait_child(int64_t processId, int* status, int options) {
    if (status == nullptr) return -1;
    return os::waitChild(processId, *status, options);
}
int os_replace_process_image(const OsExecImageInfo* info) {
    if (info == nullptr) return -1;
    return os::replaceProcessImage(
        *reinterpret_cast<const os::ExecImageInfo*>(info));
}
uint64_t os_get_process_id() { return os::getProcessId(); }
uint64_t os_get_thread_id() { return os::getThreadId(); }
void os_exit_process(int status) { os::exitProcess(status); }
void* os_allocate_memory(size_t size) { return os::allocateMemory(size); }
int os_free_memory(void* pointer) { return os::freeMemory(pointer); }
uint64_t os_map_memory(uint64_t address, uint64_t size, uint64_t protection) {
    return os::mapMemory(address, size, protection);
}
int os_unmap_memory(uint64_t address, uint64_t size) {
    return os::unmapMemory(address, size) ? 0 : -1;
}
int os_protect_memory(uint64_t address, uint64_t size, uint64_t protection) {
    return os::protectMemory(address, size, protection) ? 0 : -1;
}
uint64_t os_get_monotonic_time() { return os::getMonotonicTime(); }
uint64_t os_get_wall_clock_time() { return os::getWallClockTime(); }
uint64_t os_get_clock_resolution() { return os::getClockResolution(); }

int os_get_process_identifiers(OsProcessIdentifiers* identifiers) {
    if (identifiers == nullptr) return -1;
    os::ProcessIdentifiers value{};
    const int result = os::getProcessIdentifiers(value);
    if (result == 0) {
        identifiers->parentId = value.parentId;
        identifiers->processGroupId = value.processGroupId;
        identifiers->sessionId = value.sessionId;
    }
    return result;
}

int os_send_signal(uint64_t processId, uint32_t signal) {
    return os::sendSignal(processId, signal);
}

int64_t os_create_session() { return os::createSession(); }

uint64_t os_execute_process(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInput,
                            uint64_t standardOutput,
                            uint64_t standardError) {
    return os::executeWithIo(path, argumentCount, arguments, standardInput,
                             standardOutput, standardError);
}

int os_get_process_state(uint64_t handle, int* exitCode) {
    if (exitCode == nullptr) return -1;
    return os::getProcessState(handle, *exitCode);
}

int os_get_process_info(uint64_t handle, OsProcessInfo* info) {
    if (info == nullptr) return -1;
    return os::getProcessInfo(handle,
                              *reinterpret_cast<os::ProcessInfo*>(info));
}

int os_posix_unsupported(uint64_t operation) {
    return os::posixUnsupported(static_cast<os::PosixOperation>(operation));
}

void os_runtime_fini() { os::runtimeFini(); }

}  // extern "C"
