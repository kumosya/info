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
    uint64_t device;
    uint64_t specialDevice;
} OsFileStat;

typedef struct OsDirectoryEntry {
    uint64_t type;
    char name[256];
} OsDirectoryEntry;
int __libc_os_rename_file(uint64_t fs, const char* existing, const char* newPath);

typedef struct OsPipeHandles {
    uint64_t readHandle;
    uint64_t writeHandle;
} OsPipeHandles;

typedef struct OsFileOperationResult {
    uint64_t requestId;
    int64_t status;
    uint64_t transferred;
} OsFileOperationResult;

typedef struct OsTaskIdentifiers {
    uint64_t parentId;
    uint64_t taskGroupId;
    uint64_t sessionId;
} OsTaskIdentifiers;

typedef struct OsTaskInfo {
    uint64_t id;
    uint32_t state;
    int32_t exitCode;
    char name[48];
    uint32_t uid, gid, effectiveUid, effectiveGid;
    uint64_t parentId;
    uint32_t trustedDeviceService;
    uint32_t reserved;
} OsTaskInfo;

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
    OS_FILE_TYPE_NONE      = 0,
    OS_FILE_TYPE_FILE      = 1,
    OS_FILE_TYPE_DIRECTORY = 2
};


uint64_t __libc_os_get_fs_service(void);
uint64_t __libc_os_open_file(uint64_t fs, const char* path);
int __libc_os_close_file(uint64_t fs, uint64_t descriptor);
int64_t __libc_os_read_file(uint64_t fs, uint64_t descriptor, void* buffer,
                     size_t size);
int64_t __libc_os_write_file(uint64_t fs, uint64_t descriptor, const void* buffer,
                      size_t size);
int __libc_os_seek_file(uint64_t fs, uint64_t descriptor, uint64_t offset);
int64_t __libc_os_seek_file_position(uint64_t fs, uint64_t descriptor, int64_t offset,
                              int whence);
int __libc_os_clone_file_descriptors(uint64_t fs, uint64_t parent_task_id,
                              uint64_t* old_descriptors,
                              uint64_t* new_descriptors, size_t capacity);
int __libc_os_create_file(uint64_t fs, const char* path);
int __libc_os_make_directory(uint64_t fs, const char* path);
int __libc_os_remove_path(uint64_t fs, const char* path);
int __libc_os_truncate_file(uint64_t fs, const char* path);
int __libc_os_stat_file(uint64_t fs, const char* path, OsFileStat* status);
int __libc_os_link_file(uint64_t fs, const char* existing, const char* new_path);
int __libc_os_change_file_mode(uint64_t fs, const char* path, uint32_t mode);
int __libc_os_change_file_owner(uint64_t fs, const char* path, uint32_t uid,
                         uint32_t gid);
int __libc_os_change_file_times(uint64_t fs, const char* path, uint64_t access_time,
                         uint64_t modification_time);
int64_t __libc_os_read_directory_batch(uint64_t fs, const char* path, uint64_t index,
                                OsDirectoryEntry* entries, size_t capacity);
uint64_t __libc_os_create_endpoint(void);
int __libc_os_read_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                       size_t size, uint64_t completion_endpoint,
                       uint64_t request_id);
int __libc_os_write_file_async(uint64_t fs, uint64_t descriptor, uint64_t offset,
                        const void* buffer, size_t size,
                        uint64_t completion_endpoint, uint64_t request_id);
int __libc_os_wait_file_operation(uint64_t completion_endpoint,
                           OsFileOperationResult* result, void* buffer,
                           size_t capacity);

int __libc_os_create_pipe(OsPipeHandles* handles);
int64_t __libc_os_read_pipe(uint64_t handle, void* buffer, size_t size);
int64_t __libc_os_write_pipe(uint64_t handle, const void* buffer, size_t size);
uint64_t __libc_os_get_standard_handle(uint32_t index);
int __libc_os_close_handle(uint64_t handle);
int64_t __libc_os_read_terminal(void* buffer, size_t size);
int64_t __libc_os_write_terminal(const void* buffer, size_t size);
uint32_t __libc_os_get_terminal_id(void);
void __libc_os_set_terminal_id(uint32_t terminal_id);

void __libc_os_yield(void);
void __libc_os_sleep(uint64_t milliseconds);
void __libc_os_exit_thread(int status) __attribute__((noreturn));
uint64_t __libc_os_create_thread(void (*entry)(void*), void* argument,
                          uint64_t stack_pointer);
int __libc_os_wait_thread(uint64_t thread_id, int* exit_code, int no_hang);
int __libc_os_wait_address(volatile uint32_t* address, uint32_t expected);
int __libc_os_wake_address(volatile uint32_t* address, uint32_t count);
int __libc_os_set_signal_action(uint32_t signal, const OsSignalAction* action,
                         OsSignalAction* old_action);
int __libc_os_set_signal_mask(int how, uint64_t mask, uint64_t* old_mask);
int __libc_os_get_pending_signals(uint64_t* mask);
int __libc_os_wait_signal(uint64_t mask);
void __libc_os_signal_return(void* frame) __attribute__((noreturn));
uint64_t __libc_os_open_message_queue(const char* name, int flags,
                               uint32_t maximum_messages,
                               uint32_t message_size);
int __libc_os_unlink_message_queue(const char* name);
int __libc_os_send_message_queue(uint64_t handle, const void* data, size_t size,
                          uint32_t priority);
int64_t __libc_os_receive_message_queue(uint64_t handle, void* data, size_t capacity,
                                 uint32_t* priority);
int __libc_os_get_message_queue_attributes(uint64_t handle,
                                    OsMessageQueueAttributes* attributes);
int __libc_os_set_message_queue_flags(uint64_t handle, uint64_t flags,
                               uint64_t* old_flags);
uint64_t __libc_os_open_shared_memory(const char* name, int flags);
int __libc_os_unlink_shared_memory(const char* name);
int __libc_os_resize_shared_memory(uint64_t handle, uint64_t size);
uint64_t __libc_os_map_shared_memory(uint64_t handle, uint64_t address, uint64_t size,
                              uint64_t offset, uint64_t protection);
int __libc_os_send_thread_signal(uint64_t thread_id, uint32_t signal);
int __libc_os_register_message_queue_notification(uint64_t handle, int type,
                                           uint32_t signal);
int64_t __libc_os_fork_task(void);
int64_t __libc_os_wait_child(int64_t task_id, int* status, int options);
int __libc_os_replace_task_image(const OsExecImageInfo* info);
int __libc_os_is_terminal_handle(uint64_t handle);
uint64_t __libc_os_get_task_id(void);
uint64_t __libc_os_get_thread_id(void);
void __libc_os_exit_task(int status) __attribute__((noreturn));
void* __libc_os_allocate_memory(size_t size);
int __libc_os_free_memory(void* pointer);
uint64_t __libc_os_map_memory(uint64_t address, uint64_t size, uint64_t protection);
int __libc_os_unmap_memory(uint64_t address, uint64_t size);
int __libc_os_protect_memory(uint64_t address, uint64_t size, uint64_t protection);
uint64_t __libc_os_get_monotonic_time(void);
uint64_t __libc_os_get_wall_clock_time(void);
uint64_t __libc_os_get_clock_resolution(void);
int __libc_os_get_task_identifiers(OsTaskIdentifiers* identifiers);
int __libc_os_send_signal(uint64_t taskId, uint32_t signal);
int __libc_os_set_task_group(uint64_t taskGroupId);
int64_t __libc_os_create_session(void);
uint64_t __libc_os_execute_task(const char* path, int argument_count,
                         const char* const* arguments, uint64_t standard_input,
                         uint64_t standard_output, uint64_t standard_error);
int __libc_os_get_task_state(uint64_t handle, int* exit_code);
int __libc_os_get_task_info(uint64_t handle, OsTaskInfo* info);
void __libc_os_runtime_fini(void);

#ifdef __cplusplus
}
#endif
