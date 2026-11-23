#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lock.h"
#include "stdio_internal.h"

extern char** environ;
extern char* __randname(char*);
static volatile uint32_t process_stream_lock;

/* 管道关闭和等待属于同一 popen 生命周期，使用独立锁保护关联表的快照。 */
FILE* popen(const char* command, const char* mode)
{
    if (!command || !mode || (*mode != 'r' && *mode != 'w') ||
        (mode[1] && (mode[1] != 'e' || mode[2]))) { errno = EINVAL; return NULL; }
    libcWaitLock(&process_stream_lock);
    int descriptors[LIBC_DESCRIPTOR_CAPACITY], pipe_fds[2];
    int count = __libc_popen_descriptors(descriptors, LIBC_DESCRIPTOR_CAPACITY);
    if (count < 0 || pipe2(pipe_fds, O_CLOEXEC)) { libcWaitUnlock(&process_stream_lock); return NULL; }
    int read_mode = *mode == 'r';
    int parent = pipe_fds[read_mode ? 0 : 1], child = pipe_fds[read_mode ? 1 : 0];
    int target = read_mode ? STDOUT_FILENO : STDIN_FILENO;
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, parent);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, child, target);
    if (!error && child != target) error = posix_spawn_file_actions_addclose(&actions, child);
    for (int index = 0; !error && index < count; ++index)
    {
        if (descriptors[index] != target) error = posix_spawn_file_actions_addclose(&actions, descriptors[index]);
    }
    pid_t pid = 0;
    char* arguments[] = {"sh", "-c", (char*)command, NULL};
    if (!error) error = posix_spawn(&pid, "/bin/sh", &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(child);
    FILE* stream = NULL;
    if (!error)
    {
        if (!mode[1] && fcntl(parent, F_SETFD, 0) < 0) error = errno;
        if (!error) stream = fdopen(parent, mode);
        if (!stream && !error) error = errno;
    }
    if (stream) stream->state->child = pid;
    else
    {
        close(parent);
        if (pid > 0)
        {
            kill(pid, SIGKILL);
            int status;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }
        errno = error;
    }
    libcWaitUnlock(&process_stream_lock);
    return stream;
}

int pclose(FILE* stream)
{
    libcWaitLock(&process_stream_lock);
    FileState* state = stream ? stream->state : NULL;
    if (!state || state->child <= 0) { libcWaitUnlock(&process_stream_lock); errno = EINVAL; return -1; }
    pid_t child = state->child;
    int closed = fclose(stream), saved = errno;
    libcWaitUnlock(&process_stream_lock);
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result < 0) return -1;
    if (closed) { errno = saved; return -1; }
    return status;
}

/* 临时文件采用 O_EXCL 创建并立即去掉目录项，关闭最后引用时回收。 */
FILE* tmpfile(void)
{
    char name[] = "/tmp/osinfo-XXXXXX";
    int descriptor = mkstemp(name);
    if (descriptor < 0) return NULL;
    if (unlink(name)) { int saved = errno; close(descriptor); errno = saved; return NULL; }
    FILE* stream = fdopen(descriptor, "w+");
    if (!stream) { int saved = errno; close(descriptor); errno = saved; }
    return stream;
}

/* tmpnam 仅给出当前未占用的名字；原子创建临时文件应使用 tmpfile/mkstemp。 */
char* tmpnam(char* output)
{
    static char storage[L_tmpnam];
    if (!output) output = storage;
    static const char prefix[] = "/tmp/osinfo-";
    for (size_t index = 0; index < sizeof(prefix) - 1; ++index) output[index] = prefix[index];
    for (unsigned attempt = 0; attempt < TMP_MAX; ++attempt)
    {
        /* 与 mkstemp 共用候选名生成器；这里仍由调用者负责后续原子创建。 */
        __randname(output + sizeof(prefix) - 1);
        output[sizeof(prefix) - 1 + 6] = 0;
        struct stat status;
        if (lstat(output, &status) < 0)
        {
            if (errno == ENOENT) return output;
            return NULL;
        }
    }
    errno = EEXIST;
    return NULL;
}

char* ctermid(char* output)
{
    static char storage[L_ctermid];
    const int saved = errno;
    struct stat status;
    /* 尚无控制终端设备入口时按规范返回空串，不能给出一个无法打开的路径。 */
    const char* name = !stat("/dev/tty", &status) && S_ISCHR(status.st_mode) ? "/dev/tty" : "";
    if (!output) output = storage;
    size_t index = 0;
    do { output[index] = name[index]; } while (name[index++]);
    errno = saved;
    return output;
}
