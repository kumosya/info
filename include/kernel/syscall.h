#ifndef INFO_KERNEL_SYSCALL_H_
#define INFO_KERNEL_SYSCALL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_SEND_NO 0
#define SYS_RECEIVE_NO 1

/* IPC system calls */

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
#define SYS_FS_ISEEK 0x24
#define SYS_FS_READDIR 0x26

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* INFO_KERNEL_SYSCALL_H_ */