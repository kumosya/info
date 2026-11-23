#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#ifndef OSINFO_HOST_FIXTURE
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>
static volatile unsigned locked_worker;
static sem_t worker_started;
static void* contend_stream(void* argument)
{
    FILE* stream = argument;
    locked_worker = ftrylockfile(stream) != 0;
    sem_post(&worker_started);
    flockfile(stream);
    fputc('T', stream);
    funlockfile(stream);
    return NULL;
}
#endif

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)
#ifdef OSINFO_HOST_FIXTURE
FILE* stdin;
FILE* stdout;
FILE* stderr;
uint64_t __libc_os_get_thread_id(void) { return 1; }
int __libc_os_wait_address(volatile uint32_t* address, uint32_t value) { (void)address; (void)value; return 0; }
int __libc_os_wake_address(volatile uint32_t* address, uint32_t count) { (void)address; (void)count; return 0; }
#endif

int main(void)
{
    char* result = NULL;
    size_t size = 99;
    FILE* stream = open_memstream(&result, &size);
    CHECK(stream && fflush(stream) == 0 && size == 0 && result && !*result);
    CHECK(fwrite("hello", 1, 5, stream) == 5);
    CHECK(ftello(stream) == 5 && fflush(stream) == 0 && size == 5 && !strcmp(result, "hello"));
    CHECK(fseeko(stream, 2, SEEK_SET) == 0 && fwrite("XY", 1, 2, stream) == 2);
    CHECK(fflush(stream) == 0 && size == 4 && !memcmp(result, "heXYo", 5));
    CHECK(fseeko(stream, 8, SEEK_SET) == 0 && fputc('!', stream) == '!');
    CHECK(fflush(stream) == 0 && size == 9 && result[5] == 0 && result[7] == 0 && result[8] == '!');
    CHECK(fclose(stream) == 0 && result[9] == 0);
    free(result);

    char memory[16] = "abcd";
    stream = fmemopen(memory, sizeof(memory), "r+");
    CHECK(stream && fgetc(stream) == 'a' && ungetc('X', stream) == 'X');
    char bytes[4];
    CHECK(fread(bytes, 1, 3, stream) == 3 && !memcmp(bytes, "Xbc", 3));
    CHECK(fseeko(stream, 4, SEEK_SET) == 0 && fwrite("EF", 1, 2, stream) == 2);
    CHECK(fflush(stream) == 0 && memory[4] == 'E' && memory[5] == 'F');
    CHECK(fclose(stream) == 0);

    char existing[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    stream = fmemopen(existing, sizeof(existing), "r+");
    CHECK(stream && fseeko(stream, 2, SEEK_SET) == 0);
    CHECK(fwrite("XY", 1, 2, stream) == 2 && fflush(stream) == 0);
    CHECK(!memcmp(existing, "abXYefgh", sizeof(existing)) && fclose(stream) == 0);

    char buffer[4];
    memset(memory, '?', sizeof(memory));
    stream = fmemopen(memory, sizeof(memory), "w+");
    CHECK(stream && setvbuf(stream, buffer, _IOFBF, sizeof(buffer)) == 0);
    CHECK(fwrite("ab", 1, 2, stream) == 2 && memory[0] == 0);
    flockfile(stream);
    CHECK(ftrylockfile(stream) == 0);
    funlockfile(stream);
    CHECK(fflush(stream) == 0 && !strcmp(memory, "ab"));
    funlockfile(stream);
    CHECK(fseeko(stream, 0, SEEK_SET) == 0 && fgetc(stream) == 'a');
    CHECK(ftello(stream) == 1 && fgetc(stream) == 'b' && fgetc(stream) == EOF && feof(stream));
    CHECK(ungetc('Z', stream) == 'Z' && !feof(stream) && fgetc(stream) == 'Z');
    CHECK(fclose(stream) == 0);

    stream = fmemopen(memory, 2, "w");
    CHECK(stream && setvbuf(stream, NULL, _IONBF, 0) == 0);
    CHECK(fwrite("XYZ", 1, 3, stream) == 2 && ferror(stream));
    CHECK(fclose(stream) == 0 && memory[0] == 'X' && memory[1] == 0);
    stream = fmemopen(NULL, 0, "w+");
    CHECK(stream && fgetc(stream) == EOF && fclose(stream) == 0);
    CHECK(!fmemopen(memory, sizeof(memory), "r??") && errno == EINVAL);
    wchar_t wide[32];
    CHECK(swprintf(wide, 32, L"%ls %d %.2f", L"中文", 17, 2.5) == 10);
    CHECK(!wcscmp(wide, L"中文 17 2.50"));
    CHECK(swprintf(wide, 4, L"abcdef") == -1 && wide[3] == 0);
    int integer = 0, consumed = 0;
    wchar_t word[16];
    CHECK(swscanf(L"17 中文", L"%d %ls%n", &integer, word, &consumed) == 2);
    CHECK(integer == 17 && !wcscmp(word, L"中文") && consumed == 5);
    CHECK(swscanf(L"中文17", L"中文%d", &integer) == 1 && integer == 17);
    wchar_t* wide_result = NULL;
    stream = open_wmemstream(&wide_result, &size);
    CHECK(stream && fwprintf(stream, L"%ls:%d", L"中文", 17) == 5);
    CHECK(fflush(stream) == 0 && size == 5 && !wcscmp(wide_result, L"中文:17"));
    CHECK(fclose(stream) == 0 && wide_result[5] == 0);
    free(wide_result);
    char locale_name[32];
    const char* locale = setlocale(LC_CTYPE, NULL);
    CHECK(locale && strlen(locale) < sizeof(locale_name));
    strcpy(locale_name, locale);
    CHECK(setlocale(LC_CTYPE, "C.UTF-8"));
    stream = fmemopen((void*)"中\n", 4, "r");
    CHECK(stream && fgetws(wide, 1, stream) == wide && wide[0] == 0 && fwide(stream, 0) > 0);
    CHECK(fgetwc(stream) == L'中' && ungetwc(L'中', stream) == L'中');
    CHECK(fgetws(wide, 32, stream) == wide && !wcscmp(wide, L"中\n"));
    CHECK(fclose(stream) == 0 && setlocale(LC_CTYPE, locale_name));
    char reopened[] = "/tmp/osinfo-b07-reopen-XXXXXX";
    int reopened_fd = mkstemp(reopened);
    CHECK(reopened_fd >= 0 && close(reopened_fd) == 0);
    stream = fopen(reopened, "w+");
    CHECK(stream && fwrite("stale", 1, 5, stream) == 5);
    CHECK(freopen(reopened, "w+", stream) == stream);
    CHECK(fseeko(stream, 0, SEEK_END) == 0 && ftello(stream) == 0 && fclose(stream) == 0);
    stream = fopen(reopened, "r");
    CHECK(stream);
    reopened_fd = fileno(stream);
    CHECK(!freopen("/no-such-b07-parent/file", "w", stream));
    CHECK(fcntl(reopened_fd, F_GETFL) < 0 && errno == EBADF);
    CHECK(unlink(reopened) == 0);
#ifndef OSINFO_HOST_FIXTURE
    char terminal[L_ctermid];
    errno = 123;
    CHECK(ctermid(terminal) == terminal && errno == 123 && ctermid(NULL));
    if (*terminal)
    {
        FILE* controlling = fopen(terminal, "r+");
        CHECK(controlling && isatty(fileno(controlling)) && fclose(controlling) == 0);
    }
    char name[L_tmpnam];
    CHECK(tmpnam(name) == name && name[0] == '/');
    stream = tmpfile();
    CHECK(stream && fwrite("temporary", 1, 9, stream) == 9);
    CHECK(fseeko(stream, 0, SEEK_SET) == 0);
    char temporary[11] = {0};
    CHECK(fread(temporary, 1, 9, stream) == 9 && !strcmp(temporary, "temporary"));
    CHECK(fclose(stream) == 0);
    stream = popen("/bin/echo pipe-data", "r");
    CHECK(stream);
    memset(temporary, 0, sizeof(temporary));
    size_t received = fread(temporary, 1, sizeof(temporary) - 1, stream);
    int status = pclose(stream);
    CHECK(received == 10 && !strcmp(temporary, "pipe-data\n"));
    CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);

    result = NULL;
    stream = open_memstream(&result, &size);
    CHECK(stream);
    flockfile(stream);
    pthread_t thread;
    CHECK(sem_init(&worker_started, 0, 0) == 0);
    CHECK(pthread_create(&thread, NULL, contend_stream, stream) == 0);
    struct timespec started_limit;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &started_limit) == 0);
    started_limit.tv_sec += 5;
    CHECK(sem_clockwait(&worker_started, CLOCK_MONOTONIC, &started_limit) == 0);
    CHECK(locked_worker == 1);
    funlockfile(stream);
    CHECK(pthread_join(thread, NULL) == 0 && fflush(stream) == 0 && size == 1 && result[0] == 'T');
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child)
    {
        int okay = fputc('B', stream) == 'B' && fflush(stream) == 0 && size == 2 && !strcmp(result, "TB");
        _exit(okay ? 0 : 99);
    }
    CHECK(fputc('C', stream) == 'C' && fflush(stream) == 0 && !strcmp(result, "TC"));
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(fclose(stream) == 0);
    free(result);
    errno = 0;
    sync();
    CHECK(errno == 0);
#endif
    return 0;
}
