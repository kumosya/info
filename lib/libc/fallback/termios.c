/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <termios.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) speed_t cfgetispeed(const struct termios * arg0) { (void)arg0; errno = ENOSYS; return (speed_t)-1; }
__attribute__((weak)) speed_t cfgetospeed(const struct termios * arg0) { (void)arg0; errno = ENOSYS; return (speed_t)-1; }
__attribute__((weak)) int cfsetispeed(struct termios * arg0, speed_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int cfsetospeed(struct termios * arg0, speed_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcdrain(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcflow(int arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcflush(int arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcgetattr(int arg0, struct termios * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) pid_t tcgetsid(int arg0) { (void)arg0; errno = ENOSYS; return (pid_t)-1; }
__attribute__((weak)) int tcgetwinsize(int arg0, struct winsize * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcsendbreak(int arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcsetattr(int arg0, int arg1, const struct termios * arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int tcsetwinsize(int arg0, const struct winsize * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
