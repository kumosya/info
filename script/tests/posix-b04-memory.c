#define _GNU_SOURCE 1
#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#define CHECK(expression) do { if (!(expression)) { \
    printf("B04 FAIL line=%d errno=%d\n", __LINE__, errno); return __LINE__; } } while (0)

int main(void)
{
    char* memory = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED);
    memory[4096] = 23;
    CHECK(mlock(memory + 1, 4096) == 0);
    CHECK(mlock(memory, 8192) == 0 && munlock(memory, 8192) == 0);
    errno = 123;
    CHECK(posix_madvise(memory, 8192, POSIX_MADV_DONTNEED) == 0 && errno == 123);
    CHECK(memory[4096] == 23);
    CHECK(posix_madvise(memory + 1, 4096, POSIX_MADV_NORMAL) == EINVAL && errno == 123);
    CHECK(mlockall(0) == -1 && errno == EINVAL);
    CHECK(mlockall(MCL_FUTURE) == 0 && munlockall() == 0);
    CHECK(munmap(memory, 8192) == 0);

    int descriptor = posix_typed_mem_open("/memory/ram", O_RDWR,
                                           POSIX_TYPED_MEM_ALLOCATE_CONTIG);
    CHECK(descriptor >= 0);
    struct posix_typed_mem_info before, during, after;
    CHECK(posix_typed_mem_get_info(descriptor, &before) == 0 && before.posix_tmi_length >= 8192);
    memory = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    CHECK(memory != MAP_FAILED);
    memory[0] = 42;
    CHECK(posix_typed_mem_get_info(descriptor, &during) == 0);
    CHECK(during.posix_tmi_length < before.posix_tmi_length);
    off_t offset;
    size_t length;
    int shared;
    CHECK(posix_mem_offset(memory + 4096, 4096, &offset, &length, &shared) == 0);
    CHECK(offset == 4096 && length >= 4096);
    char* alias = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, shared, 0);
    CHECK(alias != MAP_FAILED && alias[0] == 42);
    alias[1] = 17;
    CHECK(memory[1] == 17);
    CHECK(close(shared) == 0 && munmap(alias, 8192) == 0 && munmap(memory, 8192) == 0);
    CHECK(posix_typed_mem_get_info(descriptor, &after) == 0);
    CHECK(after.posix_tmi_length == before.posix_tmi_length && close(descriptor) == 0);
    struct osinfo_devctl_tty_size size;
    int status;
    errno = 123;
    CHECK(posix_devctl(-1, OSINFO_DEVCTL_TTY_GET_SIZE, &size, sizeof(size), &status) == EBADF && errno == 123);
    return 0;
}
