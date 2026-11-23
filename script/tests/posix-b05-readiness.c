#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>

#define CHECK(expression)                                           \
    do {                                                            \
        if (!(expression)) {                                        \
            printf("B05 FAIL line=%d errno=%d\n", __LINE__, errno); \
            return __LINE__;                                        \
        }                                                           \
    } while (0)

static volatile sig_atomic_t caught;
static void receive_signal(int number) { caught = number; }
static uint64_t monotonic(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000000000ULL + value.tv_nsec;
}
struct Worker {
        int fd, operation, other;
        pthread_t target;
};
static void* change_state(void* argument) {
    struct Worker* worker = argument;
    usleep(30000);
    long status;
    if (worker->operation == 0)
        status = write(worker->fd, "x", 1) == 1 ? 0 : -1;
    else if (worker->operation == 1)
        status = close(worker->fd);
    else if (worker->operation == 2)
        status = pthread_kill(worker->target, SIGUSR1);
    else if (worker->operation == 3) {
        char bytes[512];
        status = read(worker->fd, bytes, sizeof(bytes)) > 0 ? 0 : -1;
    } else
        status = dup2(worker->other, worker->fd) == worker->fd ? 0 : -1;
    return (void*)status;
}
static int changed(struct Worker* worker, struct pollfd* item, short expected) {
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, change_state, worker));
    CHECK(poll(item, 1, 3000) == 1 && (item->revents & expected));
    void* result;
    CHECK(!pthread_join(thread, &result) && !result);
    return 0;
}

int main(void) {
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);

    /* 非阻塞状态属于共享的管道描述，空管道返回 EAGAIN，恢复阻塞后沿用正常就绪通知。 */
    int alias = dup(pipefd[0]);
    int original_flags = fcntl(pipefd[0], F_GETFL);
    CHECK(alias >= 3 && original_flags >= 0);
    CHECK(fcntl(pipefd[0], F_SETFL, original_flags | O_NONBLOCK) == 0);
    CHECK(fcntl(alias, F_GETFL) & O_NONBLOCK);
    char nonblocking_byte;
    errno = 0;
    CHECK(read(alias, &nonblocking_byte, 1) == -1 && errno == EAGAIN);
    CHECK(write(pipefd[1], "n", 1) == 1);
    CHECK(read(alias, &nonblocking_byte, 1) == 1 && nonblocking_byte == 'n');
    CHECK(fcntl(alias, F_SETFL, original_flags) == 0);
    CHECK(!(fcntl(pipefd[0], F_GETFL) & O_NONBLOCK));
    CHECK(close(alias) == 0);

    struct pollfd items[] = {
        {pipefd[0], POLLIN, 0}, {pipefd[1], POLLOUT, 0}, {-1, POLLIN, -1}};
    CHECK(poll(items, 3, 0) == 1 && !items[0].revents &&
          (items[1].revents & POLLOUT) && !items[2].revents);
    CHECK(write(pipefd[1], "z", 1) == 1);
    CHECK(poll(items, 1, 0) == 1 && (items[0].revents & POLLIN));
    CHECK(poll(items, 1, 0) == 1);
    char byte;
    CHECK(read(pipefd[0], &byte, 1) == 1 && byte == 'z');
    struct Worker worker = {pipefd[1], 0, 0, pthread_self()};
    CHECK(changed(&worker, &items[0], POLLIN) == 0);
    CHECK(read(pipefd[0], &byte, 1) == 1 && byte == 'x');

    /* 精确写满管道，写等待只在实际腾出空间后才返回。 */
    char buffer[512] = {0};
    for (int index = 0; index < 32; ++index)
        CHECK(write(pipefd[1], buffer, sizeof(buffer)) == sizeof(buffer));
    CHECK(poll(&items[1], 1, 0) == 0);
    worker.fd        = pipefd[0];
    worker.operation = 3;
    CHECK(changed(&worker, &items[1], POLLOUT) == 0);
    for (int index = 0; index < 31; ++index)
        CHECK(read(pipefd[0], buffer, sizeof(buffer)) == sizeof(buffer));
    worker.fd        = pipefd[1];
    worker.operation = 1;
    CHECK(changed(&worker, &items[0], POLLHUP) == 0);
    CHECK(read(pipefd[0], &byte, 1) == 0);
    items[0].events = 0;
    CHECK(poll(items, 1, 0) == 1 && (items[0].revents & POLLHUP));
    CHECK(close(pipefd[0]) == 0);
    CHECK(poll(items, 1, 0) == 1 && items[0].revents == POLLNVAL);

    CHECK(pipe(pipefd) == 0);
    items[0]         = (struct pollfd){pipefd[0], POLLIN, 0};
    worker.fd        = pipefd[0];
    worker.operation = 1;
    CHECK(changed(&worker, &items[0], POLLNVAL) == 0);
    CHECK(close(pipefd[1]) == 0);
    CHECK(pipe(pipefd) == 0);
    int replacement[2];
    CHECK(pipe(replacement) == 0);
    items[0]         = (struct pollfd){pipefd[0], POLLIN, 0};
    worker.fd        = pipefd[0];
    worker.operation = 4;
    worker.other     = replacement[0];
    CHECK(changed(&worker, &items[0], POLLNVAL) == 0);
    CHECK(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    CHECK(close(replacement[0]) == 0 && close(replacement[1]) == 0);

    const char* path = "/tmp/posix-b05-file";
    int file         = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(file >= 0 && write(file, "data", 4) == 4);
    off_t offset = lseek(file, 0, SEEK_CUR);
    items[0]     = (struct pollfd){file, POLLIN | POLLOUT, 0};
    items[1]     = items[0];
    CHECK(poll(items, 2, 0) == 2 && lseek(file, 0, SEEK_CUR) == offset);
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(file, &reads);
    FD_SET(file, &writes);
    struct timeval zero = {0};
    CHECK(select(file + 1, &reads, &writes, NULL, &zero) == 2);
    CHECK(FD_ISSET(file, &reads) && FD_ISSET(file, &writes));
    CHECK(close(file) == 0 && unlink(path) == 0);
    CHECK(select(file + 1, &reads, NULL, NULL, &zero) == -1 && errno == EBADF);
    CHECK(FD_ISSET(file, &reads));
    uint64_t start = monotonic();
    CHECK(poll(NULL, 0, 10) == 0 && monotonic() - start >= 10000000ULL);
    struct timespec invalid = {0, 1000000000};
    CHECK(ppoll(NULL, 0, &invalid, NULL) == -1 && errno == EINVAL);
    CHECK(poll(NULL, 1, 0) == -1 && errno == EFAULT);

    struct sigaction action = {0};
    action.sa_handler       = receive_signal;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
    sigset_t blocked, original, current;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    CHECK(sigprocmask(SIG_BLOCK, &blocked, &original) == 0);
    CHECK(raise(SIGUSR1) == 0 && !caught);
    struct timespec limit = {1, 123};
    CHECK(pselect(0, NULL, NULL, NULL, &limit, &original) == -1 &&
          errno == EINTR);
    CHECK(caught == SIGUSR1 && limit.tv_sec == 1 && limit.tv_nsec == 123);
    CHECK(sigprocmask(SIG_SETMASK, NULL, &current) == 0 &&
          sigismember(&current, SIGUSR1));
    caught = 0;
    CHECK(raise(SIGUSR1) == 0 && !caught);
    struct timespec zero_limit = {0};
    CHECK(ppoll(NULL, 0, &zero_limit, &original) == -1 && errno == EINTR);
    CHECK(caught == SIGUSR1);
    CHECK(sigprocmask(SIG_SETMASK, &original, NULL) == 0);

    /* 临时阻塞必须贯穿多次查询和虚假唤醒，直到退出等待才恢复。 */
    caught           = 0;
    worker.target    = pthread_self();
    worker.operation = 2;
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, change_state, &worker) == 0);
    limit = (struct timespec){0, 100000000};
    start = monotonic();
    CHECK(ppoll(NULL, 0, &limit, &blocked) == 0 &&
          monotonic() - start >= 100000000ULL);
    const int join_status = pthread_join(thread, NULL);
    if (join_status != 0 || caught != SIGUSR1) {
        extern void test_report_signal_join(int, int, int) __attribute__((weak));
        if (test_report_signal_join) {
            test_report_signal_join(join_status, (int)caught, SIGUSR1);
        }
        printf("B05 signal join=%d caught=%d expected=%d\n",
               join_status, (int)caught, SIGUSR1);
    }
    CHECK(join_status == 0);
    CHECK(caught == SIGUSR1);
    CHECK(sigprocmask(SIG_SETMASK, NULL, &current) == 0 && current == original);

    caught = 0;
    CHECK(pthread_create(&thread, NULL, change_state, &worker) == 0);
    struct timeval remaining = {1, 0};
    CHECK(select(0, NULL, NULL, NULL, &remaining) == -1 && errno == EINTR);
    CHECK(caught == SIGUSR1 && remaining.tv_sec == 0 && remaining.tv_usec > 0);
    CHECK(pthread_join(thread, NULL) == 0);
    return 0;
}

int readiness_console_test(void) {
    struct pollfd input = {STDIN_FILENO, POLLIN, 0};
    extern void test_report_console_poll(int, int, int);
    const int result = poll(&input, 1, 15000);
    test_report_console_poll(result, input.revents, errno);
    CHECK(result == 1 && (input.revents & POLLIN));
    CHECK(poll(&input, 1, 0) == 1);
    char value[8] = {0};
    CHECK(read(STDIN_FILENO, value, sizeof(value)) >= 1 && value[0] == 'q');
    return 0;
}
