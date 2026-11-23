/*
 * System V 共享内存。
 */

#ifndef _POSIX_BITS_SHM_H
#define _POSIX_BITS_SHM_H
#define SHMLBA 4096

/* 保存共享内存段的权限、大小、附着数量和最近操作信息。 */
struct shmid_ds {
        struct ipc_perm shm_perm;
        size_t shm_segsz;
        time_t shm_atime;
        time_t shm_dtime;
        time_t shm_ctime;
        pid_t shm_cpid;
        pid_t shm_lpid;
        unsigned long shm_nattch;
        unsigned long __pad1;
        unsigned long __pad2;
};

/* 保存 System V 共享内存实现的大小与数量上限。 */
struct shminfo {
        unsigned long shmmax, shmmin, shmmni, shmseg, shmall, __unused[4];
};

/* 汇总 System V 共享内存的页面和使用统计。 */
struct shm_info {
        int __used_ids;
        unsigned long shm_tot, shm_rss, shm_swp;
        unsigned long __swap_attempts, __swap_successes;
};

#endif
