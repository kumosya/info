#define _GNU_SOURCE 1
#include <strings.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <unistd.h>

int ffs(int value) { return value ? __builtin_ctz((unsigned)value) + 1 : 0; }
int ffsl(long value) { return value ? __builtin_ctzl((unsigned long)value) + 1 : 0; }
int ffsll(long long value) { return value ? __builtin_ctzll((unsigned long long)value) + 1 : 0; }
void swab(const void* restrict source, void* restrict destination, ssize_t size) {
    const unsigned char* input = source; unsigned char* output = destination;
    while (size > 1) { unsigned char a = input[0], b = input[1]; output[0] = b; output[1] = a; input += 2; output += 2; size -= 2; }
}
key_t ftok(const char* path, int project) {
    struct stat status; if (stat(path, &status)) return (key_t)-1;
    return (key_t)(((uint32_t)status.st_ino & 0xffffU) | (((uint32_t)status.st_dev & 0xffU) << 16) | (((uint32_t)project & 0xffU) << 24));
}
