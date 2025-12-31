/* Public domain.  */
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* Socket types */
#define SOCK_STREAM 1    /* Stream socket */
#define SOCK_DGRAM 2     /* Datagram socket */
#define SOCK_RAW 3       /* Raw socket */
#define SOCK_RDM 4       /* Reliably-delivered message */
#define SOCK_SEQPACKET 5 /* Sequenced packet stream */

/* Address families */
#define AF_UNSPEC 0 /* Unspecified */
#define AF_INET 2   /* Internet IP Protocol */
#define AF_INET6 10 /* IP version 6 */
#define AF_UNIX 1   /* Unix domain sockets */
#define AF_LOCAL AF_UNIX
#define AF_NETLINK 16 /* Kernel user interface device */
#define AF_PACKET 17  /* Packet family */

/* Protocol families (same as address families) */
#define PF_UNSPEC AF_UNSPEC
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define PF_UNIX AF_UNIX
#define PF_LOCAL AF_LOCAL
#define PF_NETLINK AF_NETLINK
#define PF_PACKET AF_PACKET

/* Socket protocol */
#define SOL_SOCKET 1

/* Socket options */
#define SO_DEBUG 1
#define SO_REUSEADDR 2
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_NO_CHECK 11
#define SO_PRIORITY 12
#define SO_LINGER 13
#define SO_BSDCOMPAT 14
#define SO_REUSEPORT 15

/* Socket level I/O control commands */
#define FIONREAD 0x541B
#define FIONBIO 0x5421

/* Socket address structure */
struct sockaddr {
    sa_family_t sa_family; /* Address family */
    char sa_data[14];      /* Protocol specific address */
};

/* Socket address storage structure */
struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_padding[128 - sizeof(sa_family_t)];
};

/* msghdr structure for sendmsg/recvmsg */
struct msghdr {
    void *msg_name;        /* Optional address */
    socklen_t msg_namelen; /* Size of address */
    struct iovec *msg_iov; /* Scatter/gather array */
    size_t msg_iovlen;     /* Number of elements in msg_iov */
    void *msg_control;     /* Ancillary data */
    size_t msg_controllen; /* Ancillary data buffer length */
    int msg_flags;         /* Receive flags */
};

/* iovec structure for scatter/gather I/O */
struct iovec {
    void *iov_base; /* Base address */
    size_t iov_len; /* Length */
};

/* Function declarations */
int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *addr, socklen_t *addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
int close(int sockfd);
int shutdown(int sockfd, int how);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval,
               socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval,
               socklen_t *optlen);

/* Send/recv flags */
#define MSG_OOB 0x0001
#define MSG_PEEK 0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_EOR 0x0008
#define MSG_TRUNC 0x0010
#define MSG_CTRUNC 0x0020
#define MSG_WAITALL 0x0040
#define MSG_DONTWAIT 0x0080
#define MSG_NOSIGNAL 0x0100

/* Shutdown how parameter */
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H */