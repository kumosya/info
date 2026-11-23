/*
 * System V 信号量。
 */

#ifndef _POSIX_BITS_SEM_H
#define _POSIX_BITS_SEM_H
/* 保存 System V 信号量集合的权限、数量和修改时间。 */
struct semid_ds {
        struct ipc_perm sem_perm;
        time_t sem_otime;
        long __unused1;
        time_t sem_ctime;
        long __unused2;
        unsigned short sem_nsems;
        char __sem_nsems_pad[sizeof(long) - sizeof(short)];
        long __unused3;
        long __unused4;
};

#endif
