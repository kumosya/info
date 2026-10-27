#ifndef _POSIX_BITS_SEM_H
#define _POSIX_BITS_SEM_H
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
