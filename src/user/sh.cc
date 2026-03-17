#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>

extern "C" pid_t clone(int (*fn)(void *), void *stack, int flags, void *arg);

char path[256] = "/";

int func(void *arg) {
    char *argv[2] = {"cat", nullptr};
    execv("/bin/cat", argv);
    return 1;
}

int main(int argc, char *argv[]) {
    char *buf = new char[256];
    char *path = new char[256];
    std::strcpy(path, "/");
    std::printf("OS info shell version 0.1\n");
    while (true) {
        std::printf("[root@localhost %s] # ", path);
        std::gets(buf);
        if (std::strcmp(buf, "exit") == 0) {
            break;
        } else if (std::strcmp(buf, "version") == 0) {
            std::printf("OS info version 0.1\n");
        } else if (std::strcmp(buf, "sh") == 0) {
            char *argv[2] = {"sh", nullptr};
            execv("/bin/sh", argv);
        } else if (std::strcmp(buf, "cat") == 0) {
            pid_t pid = clone(func, nullptr, 0, nullptr);
            int status;
            wait(&status);
            std::printf("cat (pid %d) exit with status %d\n", pid, status);
            //char *argv[2] = {"cat", nullptr};
            //execv("/bin/cat", argv);
        } else {
            std::printf("sh: %s: command not found.\n", buf);
        }
    }
    delete[] buf;
    delete[] path;
    return 0;
}
