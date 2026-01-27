#ifndef INFO_KERNEL_SYSCALL_H_
#define INFO_KERNEL_SYSCALL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_SEND 0
#define SYS_RECEIVE 1

/* IPC system calls */
#define SYS_BLOCK 2
#define SYS_FS 3
#define SYS_CHAR 4
#define SYS_MM 5
#define SYS_TASK 6

/* Memory management */
#define SYS_MM_FORK 0x2
#define SYS_MM_EXIT 0x3
#define SYS_MM_WAITPID 0x4
#define SYS_MM_MMAP 0x5
#define SYS_MM_MUNMAP 0x6

/* Block device */
#define SYS_BLOCK_GET 0x10

/* File system */
#define SYS_FS_OPEN 0x20
#define SYS_FS_READ 0x21
#define SYS_FS_WRITE 0x22
#define SYS_FS_CLOSE 0x23
#define SYS_FS_LSEEK 0x24
#define SYS_FS_DUP 0x25
#define SYS_FS_DUP2 0x26
#define SYS_FS_OPENDIR 0x27
#define SYS_FS_READDIR 0x28
#define SYS_FS_CLOSEDIR 0x29

/* Task */
#define SYS_TASK_EXIT 0x30
#define SYS_TASK_KILL 0x31
#define SYS_TASK_EXECVE 0x32
#define SYS_TASK_WAITPID 0x33
#define SYS_TASK_YIELD 0x34
#define SYS_TASK_SLEEP 0x35
#define SYS_TASK_GETPID 0x36
#define SYS_TASK_GETPPID 0x37
#define SYS_TASK_WAIT 0x38

/* Character device */
#define SYS_CHAR_PUTCHAR 0x40
#define SYS_CHAR_GETCHAR 0x41
#define SYS_CHAR_PUTS 0x42

#include <stdint.h>

#include <sys/types.h>

typedef union _message {
    uint64_t num[32];
    char data[256];
    struct {
        char str[240];
        uint64_t arg;
    } s;
} MESSAGE;

int msgSend(pid_t dst_pid, uint64_t type, MESSAGE *msg);
int msgRecv(pid_t *src_pid, uint64_t type, MESSAGE *msg);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* INFO_KERNEL_SYSCALL_H_ */