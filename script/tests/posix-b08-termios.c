#define _GNU_SOURCE 1
#include <devctl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <termios.h>

static int transport_error, device_error;
static struct osinfo_devctl_tty_size payload;

/* 替代服务传输，仅验证 libc 的转换；真实服务仍需 OS 测试。 */
int posix_devctl(int fd, int command, void *data, size_t length, int *status)
{
    if (fd != 7 || length != sizeof(payload)) return EBADF;
    *status = device_error;
    if (command == OSINFO_DEVCTL_TTY_GET_SIZE) memcpy(data, &payload, length);
    else memcpy(&payload, data, length);
    return transport_error;
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void)
{
    struct termios value = {0};
    value.c_cflag = CS8 | PARENB;
    CHECK(cfsetspeed(&value, B115200) == 0);
    CHECK(cfgetispeed(&value) == B115200 && cfgetospeed(&value) == B115200);
    CHECK((value.c_cflag & (CSIZE | PARENB)) == (CS8 | PARENB));
    struct termios previous = value;
    CHECK(cfsetspeed(&value, 115200) == -1 && errno == EINVAL);
    CHECK(memcmp(&previous, &value, sizeof(value)) == 0);
    CHECK(cfsetispeed(&value, B0) == 0);
    CHECK(cfsetospeed(&value, B9600) == 0 && cfgetispeed(&value) == B9600);

    struct winsize size = {25, 80, 640, 480};
    CHECK(tcsetwinsize(7, &size) == 0);
    memset(&size, 0, sizeof(size));
    CHECK(tcgetwinsize(7, &size) == 0 && size.ws_row == 25 && size.ws_col == 80);
    CHECK(size.ws_xpixel == 640 && size.ws_ypixel == 480);
    payload.rows = (unsigned int)USHRT_MAX + 1;
    CHECK(tcgetwinsize(7, &size) == -1 && errno == EOVERFLOW && size.ws_row == 25);
    transport_error = EIO;
    CHECK(tcgetwinsize(7, &size) == -1 && errno == EIO);
    transport_error = 0;
    device_error = EACCES;
    CHECK(tcsetwinsize(7, &size) == -1 && errno == EACCES);
    CHECK(tcgetwinsize(7, 0) == -1 && errno == EFAULT);
    return 0;
}
