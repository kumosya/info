#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* 在 OSInfo 中运行；子进程模式也由本测试映像执行。 */
#define CHECK(expression) do { if (!(expression)) { \
    printf("B03 FAIL line=%d errno=%d\n", __LINE__, errno); return __LINE__; } } while (0)
static char sequence[128];
static unsigned sequence_size;
static void prepare_first(void) { sequence[sequence_size++] = 'a'; }
static void prepare_second(void) { sequence[sequence_size++] = 'b'; }
static void parent_first(void) { sequence[sequence_size++] = 'c'; }
static void parent_second(void) { sequence[sequence_size++] = 'd'; }
static void child_first(void) { sequence[sequence_size++] = 'e'; }
static void child_second(void) { sequence[sequence_size++] = 'f'; }

static void *thread_identity(void *unused)
{
    (void)unused;
    return (void *)(uintptr_t)geteuid();
}

static int credentials(void)
{
    uid_t real, effective, saved;
    gid_t gr, ge, gs;
    CHECK(getresuid(&real, &effective, &saved) == 0 && effective == 0);
    CHECK(getresgid(&gr, &ge, &gs) == 0);
    CHECK(setresgid(101, 102, 103) == 0);
    CHECK(setegid(103) == 0);
    CHECK(getresgid(&gr, &ge, &gs) == 0 && gr == 101 && ge == 103 && gs == 103);
    CHECK(setregid((gid_t)-1, 102) == 0);
    CHECK(getresgid(&gr, &ge, &gs) == 0 && gr == 101 && ge == 102 && gs == 102);

    gid_t groups[] = {101, 102, 103};
    CHECK(setgroups(3, groups) == 0 && getgroups(0, NULL) == 3);
    CHECK(getgroups(2, groups) == -1 && errno == EINVAL);
    CHECK(getgroups(3, groups) == 3 && groups[2] == 103);
    CHECK(setresuid(1001, 1002, 0) == 0);
    CHECK(setresuid(1001, 1002, 9999) == -1 && errno == EPERM);
    CHECK(getresuid(&real, &effective, &saved) == 0);
    CHECK(real == 1001 && effective == 1002 && saved == 0);
    pthread_t thread;
    void *result;
    CHECK(pthread_create(&thread, NULL, thread_identity, NULL) == 0);
    CHECK(pthread_join(thread, &result) == 0 && (uintptr_t)result == 1002);
    CHECK(seteuid(0) == 0);
    CHECK(setreuid(0, 0) == 0 && setresgid(0, 0, 0) == 0);
    CHECK(getresuid(&real, &effective, &saved) == 0 && real == 0 && saved == 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "cwd"))
    {
        char cwd[384];
        return !getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/tmp");
    }
    if (argc > 1 && !strcmp(argv[1], "exec-child")) return 23;
    /* 新映像立即 fork，覆盖内存服务尚未定时清理旧映像记录的情况。 */
    if (argc > 1 && !strcmp(argv[1], "exec-fork-child"))
    {
        const pid_t nested = fork();
        if (nested < 0) return 90;
        if (!nested) _exit(0);
        int result;
        return waitpid(nested, &result, 0) != nested ||
               !WIFEXITED(result) || WEXITSTATUS(result) != 0;
    }
    CHECK(geteuid() == 0);
    CHECK(credentials() == 0);
    CHECK(pthread_atfork(prepare_first, parent_first, child_first) == 0);
    CHECK(pthread_atfork(prepare_second, parent_second, child_second) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(sequence_size == 4 && !memcmp(sequence, "baef", 4) ? 19 : 99);
    CHECK(sequence_size == 4 && !memcmp(sequence, "bacd", 4));
    siginfo_t first, second;
    CHECK(waitid(P_PID, child, &first, WEXITED | WNOWAIT) == 0);
    CHECK(waitid(P_PID, child, &second, WEXITED | WNOWAIT) == 0);
    CHECK(first.si_pid == child && second.si_pid == child && first.si_status == 19);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 19);
    CHECK(waitid(P_PID, child, &first, WEXITED | WNOHANG) == -1 && errno == ECHILD);

    int channel[2];
    CHECK(pipe(channel) == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child)
    {
        close(channel[1]);
        char byte;
        _exit(read(channel[0], &byte, 1) == 1 ? 17 : 99);
    }
    close(channel[0]);
    memset(&first, 0x5a, sizeof(first));
    CHECK(waitid(P_PID, child, &first, WEXITED | WNOHANG) == 0 && first.si_pid == 0);
    CHECK(kill(child, SIGSTOP) == 0);
    CHECK(waitid(P_PID, child, &first, WSTOPPED | WNOWAIT) == 0);
    CHECK(first.si_code == CLD_STOPPED && first.si_status == SIGSTOP);
    CHECK(waitid(P_PID, child, &second, WSTOPPED) == 0 && second.si_pid == child);
    CHECK(kill(child, SIGCONT) == 0);
    CHECK(waitid(P_PID, child, &first, WCONTINUED) == 0 && first.si_code == CLD_CONTINUED);
    CHECK(write(channel[1], "x", 1) == 1 && close(channel[1]) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 17);

    char executable[384];
    CHECK(argv[0][0] == '/' && strlen(argv[0]) < sizeof(executable));
    strcpy(executable, argv[0]);
    posix_spawn_file_actions_t actions;
    CHECK(posix_spawn_file_actions_init(&actions) == 0);
    char directory[] = "/tmp";
    CHECK(posix_spawn_file_actions_addchdir(&actions, directory) == 0);
    directory[1] = 'X';
    char *arguments[] = {executable, "cwd", NULL};
    char *environment[] = {NULL};
    char *fork_arguments[] = {executable, "exec-fork-child", NULL};
    for (unsigned iteration = 0; iteration < 8; ++iteration)
    {
        CHECK(posix_spawn(&child, executable, NULL, NULL, fork_arguments, environment) == 0);
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(posix_spawn(&child, executable, &actions, NULL, arguments, environment) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0);
    int fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(fd >= 0 && posix_spawn_file_actions_init(&actions) == 0);
    CHECK(posix_spawn_file_actions_addfchdir(&actions, fd) == 0);
    CHECK(posix_spawn(&child, executable, &actions, NULL, arguments, environment) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, fd) == 0);
    CHECK(posix_spawn_file_actions_addfchdir(&actions, fd) == 0);
    CHECK(posix_spawn(&child, executable, &actions, NULL, arguments, environment) == EBADF);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0 && close(fd) == 0);

    /* PATH 的相对目录必须在子进程完成 chdir 后解释，且不能改写父进程 errno。 */
    const char* old_path = getenv("PATH");
    char* saved_path = old_path ? strdup(old_path) : NULL;
    CHECK(!old_path || saved_path);
    CHECK(setenv("PATH", ".", 1) == 0);
    CHECK(posix_spawn_file_actions_init(&actions) == 0);
    CHECK(posix_spawn_file_actions_addchdir(&actions, "/bin") == 0);
    char* search_arguments[] = {executable, "exec-child", NULL};
    errno = 123;
    CHECK(posix_spawnp(&child, "posix-tests", &actions, NULL, search_arguments, environment) == 0 && errno == 123);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 23);
    CHECK(posix_spawnp(&child, "no-such-b03-program", &actions, NULL, search_arguments, environment) == ENOENT);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0);
    CHECK(saved_path ? setenv("PATH", saved_path, 1) == 0 : unsetenv("PATH") == 0);
    free(saved_path);

    fd = open(executable, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    child = fork();
    CHECK(child >= 0);
    if (!child)
    {
        char *exec_arguments[] = {executable, "exec-child", NULL};
        fexecve(fd, exec_arguments, environment);
        _exit(99);
    }
    CHECK(ftruncate(fd, 0) == -1 && errno == EBADF);
    CHECK(close(fd) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 23);

    /* 从已删除的打开对象执行，证明没有按描述符里的旧路径重新打开。 */
    char copied[80], renamed[80];
    snprintf(copied, sizeof(copied), "/tmp/b03-exec-%d", (int)getpid());
    snprintf(renamed, sizeof(renamed), "/tmp/b03-renamed-%d", (int)getpid());
    int input = open(executable, O_RDONLY);
    int output = open(copied, O_WRONLY | O_CREAT | O_EXCL, 0700);
    CHECK(input >= 0 && output >= 0);
    char bytes[4096];
    ssize_t count;
    while ((count = read(input, bytes, sizeof(bytes))) > 0)
    {
        ssize_t done = 0;
        while (done < count)
        {
            ssize_t written = write(output, bytes + done, (size_t)(count - done));
            CHECK(written > 0);
            done += written;
        }
    }
    CHECK(count == 0 && close(input) == 0 && close(output) == 0);
    fd = open(copied, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0 && rename(copied, renamed) == 0 && unlink(renamed) == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child)
    {
        char *exec_arguments[] = {executable, "exec-child", NULL};
        fexecve(fd, exec_arguments, environment);
        _exit(99);
    }
    CHECK(close(fd) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 23);
    char truncate_path[80];
    snprintf(truncate_path, sizeof(truncate_path), "/tmp/b03-truncate-%d", (int)getpid());
    fd = open(truncate_path, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0 && write(fd, "preserve", 8) == 8 && unlink(truncate_path) == 0);
    CHECK(ftruncate(fd, 0) == 0 && lseek(fd, 0, SEEK_CUR) == 8);
    struct stat truncated;
    CHECK(fstat(fd, &truncated) == 0 && truncated.st_size == 0 && close(fd) == 0);
    puts("B03 PROCESS PASS");
    return 0;
}
