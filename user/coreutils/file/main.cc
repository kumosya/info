#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) { fputs("usage: file FILE ...\n", stderr); return 2; }
    int result = 0;
    for (int i = 1; i < argc; ++i) {
        struct stat status{};
        if (stat(argv[i], &status)) { perror(argv[i]); result = 1; continue; }
        const char* type = "special file";
        if (S_ISDIR(status.st_mode)) type = "directory";
        else if (S_ISCHR(status.st_mode)) type = "character device";
        else if (S_ISBLK(status.st_mode)) type = "block device";
        else if (S_ISREG(status.st_mode)) {
            unsigned char bytes[512];
            const int fd = open(argv[i], O_RDONLY);
            if (fd < 0) { perror(argv[i]); result = 1; continue; }
            const ssize_t count = read(fd, bytes, sizeof(bytes)); close(fd);
            if (count < 0) { perror(argv[i]); result = 1; continue; }
            type = count == 0 ? "empty" : "text";
            if (count >= 4 && !memcmp(bytes, "\177ELF", 4)) type = "ELF executable or shared object";
            else if (count >= 8 && !memcmp(bytes, "\211PNG\r\n\032\n", 8)) type = "PNG image";
            else if (count >= 2 && bytes[0] == '#' && bytes[1] == '!') type = "script";
            else for (ssize_t n = 0; n < count; ++n)
                if (bytes[n] < 32 && bytes[n] != '\n' && bytes[n] != '\t' && bytes[n] != '\r') { type = "data"; break; }
        }
        printf("%s: %s\n", argv[i], type);
    }
    return result;
}
