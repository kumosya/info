#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

char path[256] = "/";

int main(int argc, char *argv[]) {
    char buf[16] = {0};
    std::puts("OS info shell version 0.1\n");
    while (true) {
        std::printf("[root@localhost %s] # ", path);
        std::gets(buf);
        if (std::strcmp(buf, "exit") == 0) {
            break;
        } else if (std::strcmp(buf, "version") == 0) {
            std::puts("OS info version 0.1\n");
        } else if (std::strcmp(buf, "sh") == 0) {
            char *argv[2] = {"sh", nullptr};
            execv("/bin/sh", argv);
        } else {
            std::printf("sh: %s: command not found.\n", buf);
        }
    }
    return 0;
}
