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
/* Task, thread and synchronization mechanisms */
#define OS_SYS_EXIT_TASK 11U
#define OS_SYS_CREATE_THREAD 12U
#define OS_SYS_EXIT_THREAD 13U
#define OS_SYS_YIELD 14U
#define OS_SYS_SLEEP 15U
#define OS_SYS_GET_THREAD_ID 16U
#define OS_SYS_FORK_TASK 17U
#define OS_SYS_REPLACE_TASK_IMAGE 18U
#define OS_SYS_WAIT_ADDRESS 19U
#define OS_SYS_WAKE_ADDRESS 20U
#define OS_SYS_SIGNAL_RETURN 21U

#define OS_SYS_COUNT 22U

#endif
