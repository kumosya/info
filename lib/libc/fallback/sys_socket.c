/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>

__attribute__((weak)) int accept(int arg0, struct sockaddr *restrict arg1, socklen_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int accept4(int arg0, struct sockaddr *restrict arg1, socklen_t *restrict arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int bind(int arg0, const struct sockaddr * arg1, socklen_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int connect(int arg0, const struct sockaddr * arg1, socklen_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getpeername(int arg0, struct sockaddr *restrict arg1, socklen_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getsockname(int arg0, struct sockaddr *restrict arg1, socklen_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getsockopt(int arg0, int arg1, int arg2, void *restrict arg3, socklen_t *restrict arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int listen(int arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) ssize_t recv(int arg0, void * arg1, size_t arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) ssize_t recvfrom(int arg0, void *restrict arg1, size_t arg2, int arg3, struct sockaddr *restrict arg4, socklen_t *restrict arg5) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) ssize_t recvmsg(int arg0, struct msghdr * arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) ssize_t send(int arg0, const void * arg1, size_t arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) ssize_t sendmsg(int arg0, const struct msghdr * arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) ssize_t sendto(int arg0, const void * arg1, size_t arg2, int arg3, const struct sockaddr * arg4, socklen_t arg5) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) int setsockopt(int arg0, int arg1, int arg2, const void * arg3, socklen_t arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int shutdown(int arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sockatmark(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int socket(int arg0, int arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int socketpair(int arg0, int arg1, int arg2, int * arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
