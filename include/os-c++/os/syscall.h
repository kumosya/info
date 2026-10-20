#ifndef _OS_SYSCALL_H
#define _OS_SYSCALL_H

/* Shared Ring 3 syscall ABI. 0 is invalid. */
#define OS_SYS_BASE 1U

/* Endpoint IPC */
#define OS_SYS_SEND_MESSAGE 1U
#define OS_SYS_RECEIVE_MESSAGE 2U
#define OS_SYS_CALL_SERVICE 3U
#define OS_SYS_REPLY_MESSAGE 4U
/* Handle operations */
#define OS_SYS_GET_STANDARD_HANDLE 5U
#define OS_SYS_GET_BOOTSTRAP_HANDLE 6U
#define OS_SYS_CLOSE_HANDLE 7U
/* Address-space mappings */
#define OS_SYS_MAP_MEMORY 8U
#define OS_SYS_UNMAP_MEMORY 9U
#define OS_SYS_PROTECT_MEMORY 10U
/* map(address, size, protection | kind, handle, offset, target).
 * Target 0 is self; other targets require the fixed MM role and capability.
 * unmap(address, size, target); protect(address, size, protection, target).
 */
#define OS_MAP_ANONYMOUS 0U
#define OS_MAP_PAGE_OBJECT 0x100U
#define OS_MAP_SHARED_OBJECT 0x200U
#define OS_PROTECT_COPY_ON_WRITE 4U
/* Task, thread and synchronization mechanisms */
#define OS_SYS_EXIT_TASK 11U
#define OS_SYS_CREATE_THREAD 12U
#define OS_SYS_EXIT_THREAD 13U
#define OS_SYS_YIELD 14U
#define OS_SYS_SLEEP 15U
#define OS_SYS_FORK_TASK 16U
#define OS_SYS_REPLACE_TASK_IMAGE 17U
#define OS_SYS_WAIT_ADDRESS 18U
#define OS_SYS_WAKE_ADDRESS 19U

#define OS_SYS_COUNT 20U

#endif
