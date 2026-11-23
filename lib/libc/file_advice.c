#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include "internal.h"

/* 建议可被缓存层忽略；仍须校验对象和区间，且不改变 errno 或文件偏移。 */
int posix_fadvise(int descriptor, off_t offset, off_t length, int advice)
{
    OpenDescription* file = libcDescriptor(descriptor);
    if (!file) return EBADF;
    if (offset < 0 || length < 0 || advice < POSIX_FADV_NORMAL ||
        advice > POSIX_FADV_NOREUSE) return EINVAL;
    if (length && offset > INT64_MAX - length) return EOVERFLOW;
    if (file->kind == DESCRIPTOR_PIPE_READ || file->kind == DESCRIPTOR_PIPE_WRITE) return ESPIPE;
    if (file->kind != DESCRIPTOR_FILE) return ENODEV;
    return 0;
}

/* 当前 close 完成后才返回，不产生 EINTR；不得在失败后盲目重试已释放的 FD。 */
int posix_close(int descriptor, int flags)
{
    if (flags & ~POSIX_CLOSE_RESTART) { errno = EINVAL; return -1; }
    return close(descriptor);
}

/* sync 是全系统刷新，不等同于遍历本进程当前打开的 FD。 */
void sync(void)
{
    int error = __libc_os_sync_all();
    if (error) errno = error;
}
