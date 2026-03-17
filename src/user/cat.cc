#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::printf("Usage: cat <filename>\n");
        return 1;
    }

    const char *filename = argv[1];
    int file = open(filename, O_RDONLY, 0);
    if (file < 0) {
        std::printf("Failed to open file: %s\n", filename);
        return 1;
    }

    char buffer[256];
    while (read(file, buffer, sizeof(buffer)) > 0) {
        std::printf("%s", buffer);
    }

    close(file);
    return 0;
}
