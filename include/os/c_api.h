#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OsFileStat {
    uint64_t type;
    uint64_t size;
    uint32_t mode;
    uint64_t inode;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t accessTime;
    uint64_t modificationTime;
    uint64_t changeTime;
} OsFileStat;

typedef struct OsDirectoryEntry {
    uint64_t type;
    char name[48];
} OsDirectoryEntry;

typedef struct OsPipeHandles {
    uint64_t readHandle;
    uint64_t writeHandle;
} OsPipeHandles;

typedef struct OsFileOperationResult {
    uint64_t requestId;
    int64_t status;
    uint64_t transferred;
} OsFileOperationResult;

typedef struct OsProcessIdentifiers {
    uint64_t parentId;
    uint64_t processGroupId;
    uint64_t sessionId;
} OsProcessIdentifiers;

typedef struct OsProcessInfo {
    uint64_t id;
    uint32_t state;
    int32_t exitCode;
    char name[48];
} OsProcessInfo;

typedef struct OsSignalAction {
    uint64_t handler;
    uint64_t mask;
    uint64_t flags;
    uint64_t restorer;
} OsSignalAction;

typedef struct OsMessageQueueAttributes {
    uint64_t flags;
    uint64_t maximumMessages;
    uint64_t messageSize;
    uint64_t currentMessages;
} OsMessageQueueAttributes;

typedef struct OsExecImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
    uint32_t argumentCount;
    uint32_t environmentCount;
    const char* const* arguments;
    const char* const* environment;
    const void* runtimeState;
    uint64_t runtimeStateSize;
    uint32_t sharedImageCount;
    uint32_t sharedReserved;
    const struct OsSharedImageInfo* sharedImages;
} OsExecImageInfo;

typedef struct OsSharedImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
} OsSharedImageInfo;

enum {
    OS_FILE_TYPE_NONE = 0,
    OS_FILE_TYPE_FILE = 1,
    OS_FILE_TYPE_DIRECTORY = 2
};

typedef enum OsPosixOperation {
    OS_POSIX_FORK,
    OS_POSIX_EXEC,
    OS_POSIX_WAIT,
    OS_POSIX_THREAD_LIFECYCLE,
    OS_POSIX_THREAD_CANCELLATION,
    OS_POSIX_SIGNAL_ACTION,
    OS_POSIX_REALTIME_SIGNALS,
    OS_POSIX_FILE_LINKS_AND_METADATA,
    OS_POSIX_FILE_BACKED_MEMORY,
    OS_POSIX_SHARED_MEMORY,
    OS_POSIX_MESSAGE_QUEUE,
    OS_POSIX_NAMED_SEMAPHORE,
    OS_POSIX_ASYNC_IO,
    OS_POSIX_POLL,
    OS_POSIX_SOCKET,
    OS_POSIX_NETWORK,
    OS_POSIX_TIMER,
    OS_POSIX_SCHEDULING,
    OS_POSIX_RESOURCE_LIMITS,
    OS_POSIX_CREDENTIALS,
    OS_POSIX_LOCALE,
    OS_POSIX_INTERNATIONALIZATION,
    OS_POSIX_REGULAR_EXPRESSION,
    OS_POSIX_WORD_EXPANSION,
    OS_POSIX_DYNAMIC_LOADING,
    OS_POSIX_SPAWN,
    OS_POSIX_TERMINAL_JOB_CONTROL
} OsPosixOperation;

uint64_t os_get_fs_service(void);
uint64_t os_open_file(uint64_t fs, const char* path);
int os_close_file(uint64_t fs, uint64_t descriptor);
int64_t os_read_file(uint64_t fs, uint64_t descriptor, void* buffer,
                     size_t size);
int64_t os_write_file(uint64_t fs, uint64_t descriptor, const void* buffer,
                      size_t size);
int os_seek_file(uint64_t fs, uint64_t descriptor, uint64_t offset);
int64_t os_seek_file_position(uint64_t fs, uint64_t descriptor,
                              int64_t offset, int whence);
int os_clone_file_descriptors(uint64_t fs, uint64_t parent_process_id,
                              uint64_t* old_descriptors,
                              uint64_t* new_descriptors, size_t capacity);
int os_create_file(uint64_t fs, const char* path);
int os_make_directory(uint64_t fs, const char* path);
int os_remove_path(uint64_t fs, const char* path);
int os_truncate_file(uint64_t fs, const char* path);
int os_stat_file(uint64_t fs, const char* path, OsFileStat* status);
int os_link_file(uint64_t fs, const char* existing, const char* new_path);
int os_change_file_mode(uint64_t fs, const char* path, uint32_t mode);
int os_change_file_owner(uint64_t fs, const char* path, uint32_t uid,
                         uint32_t gid);
int os_change_file_times(uint64_t fs, const char* path, uint64_t access_time,
                         uint64_t modification_time);
int64_t os_read_directory_batch(uint64_t fs, const char* path, uint64_t index,
                                OsDirectoryEntry* entries, size_t capacity);
uint64_t os_create_endpoint(void);
int os_read_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                       size_t size, uint64_t completion_endpoint,
                       uint64_t request_id);
int os_write_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                        const void* buffer, size_t size,
                        uint64_t completion_endpoint, uint64_t request_id);
int os_wait_file_operation(uint64_t completion_endpoint,
                           OsFileOperationResult* result, void* buffer,
                           size_t capacity);

int os_create_pipe(OsPipeHandles* handles);
int64_t os_read_pipe(uint64_t handle, void* buffer, size_t size);
int64_t os_write_pipe(uint64_t handle, const void* buffer, size_t size);
uint64_t os_get_standard_handle(uint32_t index);
int os_close_handle(uint64_t handle);
int64_t os_read_terminal(void* buffer, size_t size);
int64_t os_write_terminal(const void* buffer, size_t size);

void os_yield(void);
void os_sleep(uint64_t milliseconds);
void os_exit_thread(int status) __attribute__((noreturn));
uint64_t os_create_thread(void (*entry)(void*), void* argument,
                          uint64_t stack_pointer);
int os_wait_thread(uint64_t thread_id, int* exit_code, int no_hang);
int os_wait_address(volatile uint32_t* address, uint32_t expected);
int os_wake_address(volatile uint32_t* address, uint32_t count);
int os_set_signal_action(uint32_t signal, const OsSignalAction* action,
                         OsSignalAction* old_action);
int os_set_signal_mask(int how, uint64_t mask, uint64_t* old_mask);
int os_get_pending_signals(uint64_t* mask);
int os_wait_signal(uint64_t mask);
void os_signal_return(void* frame) __attribute__((noreturn));
uint64_t os_open_message_queue(const char* name, int flags,
                               uint32_t maximum_messages,
                               uint32_t message_size);
int os_unlink_message_queue(const char* name);
int os_send_message_queue(uint64_t handle, const void* data, size_t size,
                          uint32_t priority);
int64_t os_receive_message_queue(uint64_t handle, void* data, size_t capacity,
                                 uint32_t* priority);
int os_get_message_queue_attributes(uint64_t handle,
                                    OsMessageQueueAttributes* attributes);
int os_set_message_queue_flags(uint64_t handle, uint64_t flags,
                               uint64_t* old_flags);
uint64_t os_open_shared_memory(const char* name, int flags);
int os_unlink_shared_memory(const char* name);
int os_resize_shared_memory(uint64_t handle, uint64_t size);
uint64_t os_map_shared_memory(uint64_t handle, uint64_t address,
                              uint64_t size, uint64_t offset,
                              uint64_t protection);
int os_send_thread_signal(uint64_t thread_id, uint32_t signal);
int os_register_message_queue_notification(uint64_t handle, int type,
                                           uint32_t signal);
int64_t os_fork_process(void);
int64_t os_wait_child(int64_t process_id, int* status, int options);
int os_replace_process_image(const OsExecImageInfo* info);
uint64_t os_get_process_id(void);
uint64_t os_get_thread_id(void);
void os_exit_process(int status) __attribute__((noreturn));
void* os_allocate_memory(size_t size);
int os_free_memory(void* pointer);
uint64_t os_map_memory(uint64_t address, uint64_t size, uint64_t protection);
int os_unmap_memory(uint64_t address, uint64_t size);
int os_protect_memory(uint64_t address, uint64_t size, uint64_t protection);
uint64_t os_get_monotonic_time(void);
uint64_t os_get_wall_clock_time(void);
uint64_t os_get_clock_resolution(void);
int os_get_process_identifiers(OsProcessIdentifiers* identifiers);
int os_send_signal(uint64_t processId, uint32_t signal);
int64_t os_create_session(void);
uint64_t os_execute_process(const char* path, int argument_count,
                            const char* const* arguments,
                            uint64_t standard_input,
                            uint64_t standard_output,
                            uint64_t standard_error);
int os_get_process_state(uint64_t handle, int* exit_code);
int os_get_process_info(uint64_t handle, OsProcessInfo* info);
int os_posix_unsupported(uint64_t operation);
void os_runtime_fini(void);

#ifdef __cplusplus
}
#endif
