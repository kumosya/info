#ifndef _STDC_ERRNO_H
#define _STDC_ERRNO_H

#define E2BIG 7
#define EACCES 13
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define EAFNOSUPPORT 97
#define EAGAIN 11
#define EALREADY 114
#define EPERM 1
#define ESRCH 3
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EFAULT 14
#define ENXIO 6
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define ENOMEM 12
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOLCK 37
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define ENOMSG 42
#define EIDRM 43
#define ENOSTR 60
#define ENODATA 61
#define ETIME 62
#define ENOSR 63
#define EOVERFLOW 75
#define EPROTO 71
#define EILSEQ 84
#define EBADMSG 74
#define ENOTSOCK 88
#define EDESTADDRREQ 89
#define EMSGSIZE 90
#define EPROTOTYPE 91
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP 95
#define ECONNABORTED 103
#define ECONNRESET 104
#define ENOBUFS 105
#define EISCONN 106
#define ENOTCONN 107
#define ETIMEDOUT 110
#define ECONNREFUSED 111
#define EINPROGRESS 115
#define ECANCELED 125
#define EOWNERDEAD 130
#define ENOTRECOVERABLE 131

#define EWOULDBLOCK EAGAIN
#define ENOTSUP EOPNOTSUPP

#ifdef __cplusplus
extern "C" {
#endif
int* __errno_location(void);
#define errno (*__errno_location())
#ifdef __cplusplus
}
#endif

#endif
