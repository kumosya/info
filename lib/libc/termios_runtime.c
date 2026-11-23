#define _GNU_SOURCE 1
#include <devctl.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

/* 波特率使用 Bxxx 编码，不能把每秒位数直接写入属性。 */
static int valid_speed(speed_t speed)
{
    return speed <= B38400 || (speed >= B57600 && speed <= B4000000);
}

/* 只读取属性；是否能应用到硬件由 tcsetattr 检查。 */
speed_t cfgetospeed(const struct termios *attributes)
{
    return attributes->c_cflag & CBAUD;
}

/* 输入速率为零时沿用输出速率。 */
speed_t cfgetispeed(const struct termios *attributes)
{
    return attributes->__c_ispeed ? attributes->__c_ispeed : cfgetospeed(attributes);
}

/* 保留字符长度、校验等其他位；失败时不修改原属性。 */
int cfsetospeed(struct termios *attributes, speed_t speed)
{
    if (!valid_speed(speed))
    {
        errno = EINVAL;
        return -1;
    }

    attributes->c_cflag = (attributes->c_cflag & ~CBAUD) | speed;
    attributes->__c_ospeed = speed;
    return 0;
}

/* 零表示跟随输出速率，保留这一约定供后续输出速率修改使用。 */
int cfsetispeed(struct termios *attributes, speed_t speed)
{
    if (!valid_speed(speed))
    {
        errno = EINVAL;
        return -1;
    }

    attributes->__c_ispeed = speed;
    return 0;
}

/* 先校验再同时修改两个方向，避免错误参数造成半更新。 */
int cfsetspeed(struct termios *attributes, speed_t speed)
{
    if (cfsetospeed(attributes, speed) != 0)
    {
        return -1;
    }

    return cfsetispeed(attributes, speed);
}

/* 使用与 B04 相同的真实 TTY 服务入口，统一转换两层错误码。 */
static int window_control(int descriptor, int command,
                          struct osinfo_devctl_tty_size *size)
{
    int device_error = 0;
    int error = posix_devctl(descriptor, command, size, sizeof(*size), &device_error);
    if (error || device_error)
    {
        errno = error ? error : device_error;
        return -1;
    }

    return 0;
}

/* 服务采用 32 位字段；越界时不截断，也不部分覆盖调用者结果。 */
int tcgetwinsize(int descriptor, struct winsize *size)
{
    struct osinfo_devctl_tty_size value;
    if (!size)
    {
        errno = EFAULT;
        return -1;
    }

    if (window_control(descriptor, OSINFO_DEVCTL_TTY_GET_SIZE, &value))
    {
        return -1;
    }

    if (value.rows > USHRT_MAX || value.columns > USHRT_MAX ||
        value.pixel_width > USHRT_MAX || value.pixel_height > USHRT_MAX)
    {
        errno = EOVERFLOW;
        return -1;
    }

    *size = (struct winsize){value.rows, value.columns,
                             value.pixel_width, value.pixel_height};
    return 0;
}

/* 修改真实终端尺寸，权限检查及窗口变化通知由 TTY 服务负责。 */
int tcsetwinsize(int descriptor, const struct winsize *size)
{
    if (!size)
    {
        errno = EFAULT;
        return -1;
    }

    struct osinfo_devctl_tty_size value = {
        size->ws_row, size->ws_col, size->ws_xpixel, size->ws_ypixel
    };
    return window_control(descriptor, OSINFO_DEVCTL_TTY_SET_SIZE, &value);
}

/* 将常用 Unix 控制请求转到真实的终端和描述符接口。 */
int ioctl(int descriptor, int request, ...)
{
    if (fcntl(descriptor, F_GETFD) < 0)
    {
        return -1;
    }

    /* 这两个请求没有第三个参数。 */
    if (request == FIOCLEX || request == FIONCLEX)
    {
        int flags = fcntl(descriptor, F_GETFD);
        return fcntl(descriptor, F_SETFD,
                     request == FIOCLEX ? flags | FD_CLOEXEC : flags & ~FD_CLOEXEC);
    }

    /* 未支持的请求直接返回，避免读取调用者没有传入的可变参数。 */
    if (request != TIOCGWINSZ && request != TIOCSWINSZ && request != FIONBIO)
    {
        errno = ENOTTY;
        return -1;
    }

    va_list arguments;
    va_start(arguments, request);
    void *value = va_arg(arguments, void *);
    va_end(arguments);
    if (!value)
    {
        errno = EFAULT;
        return -1;
    }

    if (request == TIOCGWINSZ)
    {
        return tcgetwinsize(descriptor, value);
    }
    if (request == TIOCSWINSZ)
    {
        return tcsetwinsize(descriptor, value);
    }

    int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0)
    {
        return -1;
    }
    return fcntl(descriptor, F_SETFL,
                 *(int *)value ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}
