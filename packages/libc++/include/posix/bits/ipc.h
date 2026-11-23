/*
 * 进程通信类型和接口。
 */

#ifndef _POSIX_BITS_IPC_H
#define _POSIX_BITS_IPC_H
/* 保存 System V 通信对象的所有者、访问权限及身份字段。 */
struct ipc_perm {
        key_t __ipc_perm_key;
        uid_t uid;
        gid_t gid;
        uid_t cuid;
        gid_t cgid;
        mode_t mode;
        int __ipc_perm_seq;
        long __pad1;
        long __pad2;
};

#endif
