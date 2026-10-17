/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) void endhostent(void) { errno = ENOSYS;  }
__attribute__((weak)) void endnetent(void) { errno = ENOSYS;  }
__attribute__((weak)) void endprotoent(void) { errno = ENOSYS;  }
__attribute__((weak)) void endservent(void) { errno = ENOSYS;  }
__attribute__((weak)) void freeaddrinfo(struct addrinfo * arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) const char * gai_strerror(int arg0) { (void)arg0; errno = ENOSYS; return "Function not implemented"; }
__attribute__((weak)) int getaddrinfo(const char *restrict arg0, const char *restrict arg1, const struct addrinfo *restrict arg2, struct addrinfo **restrict arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return EAI_SYSTEM; }
__attribute__((weak)) struct hostent * gethostent(void) { errno = ENOSYS; return (struct hostent *)0; }
__attribute__((weak)) int getnameinfo(const struct sockaddr *restrict arg0, socklen_t arg1, char *restrict arg2, socklen_t arg3, char *restrict arg4, socklen_t arg5, int arg6) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6; errno = ENOSYS; return EAI_SYSTEM; }
__attribute__((weak)) struct netent * getnetbyaddr(uint32_t arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (struct netent *)0; }
__attribute__((weak)) struct netent * getnetbyname(const char * arg0) { (void)arg0; errno = ENOSYS; return (struct netent *)0; }
__attribute__((weak)) struct netent * getnetent(void) { errno = ENOSYS; return (struct netent *)0; }
__attribute__((weak)) struct protoent * getprotobyname(const char * arg0) { (void)arg0; errno = ENOSYS; return (struct protoent *)0; }
__attribute__((weak)) struct protoent * getprotobynumber(int arg0) { (void)arg0; errno = ENOSYS; return (struct protoent *)0; }
__attribute__((weak)) struct protoent * getprotoent(void) { errno = ENOSYS; return (struct protoent *)0; }
__attribute__((weak)) struct servent * getservbyname(const char * arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (struct servent *)0; }
__attribute__((weak)) struct servent * getservbyport(int arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (struct servent *)0; }
__attribute__((weak)) struct servent * getservent(void) { errno = ENOSYS; return (struct servent *)0; }
__attribute__((weak)) void sethostent(int arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) void setnetent(int arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) void setprotoent(int arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) void setservent(int arg0) { (void)arg0; errno = ENOSYS;  }
