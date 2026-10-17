#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool moveFile(const char* source, const char* destination) {
    if (rename(source, destination) == 0) return true;
    if (errno != EXDEV) return false;
    struct stat status{};
    if (stat(source, &status) || !S_ISREG(status.st_mode)) return false;
    const int input = open(source, O_RDONLY);
    if (input < 0) {
        perror(source);
        return false;
    }
    const int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0) {
        perror(destination);
        close(input);
        return false;
    }
    char buffer[4096];
    bool success = true;
    while (true) {
        const ssize_t count = read(input, buffer, sizeof(buffer));
        if (count <= 0) {
            success = count == 0;
            break;
        }
        if (write(output, buffer, static_cast<size_t>(count)) != count) {
            success = false;
            break;
        }
    }
    close(input);
    close(output);
    return success && unlink(source) == 0;
}

}  // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount != 3) {
        fputs("usage: mv SOURCE DESTINATION\n", stderr);
        return 1;
    }
    char destination[384];
    struct stat target{};
    const char* path = arguments[2];
    if (stat(path, &target) == 0 && S_ISDIR(target.st_mode)) {
        const char* name = strrchr(arguments[1], '/');
        name = name ? name + 1 : arguments[1];
        if (snprintf(destination, sizeof(destination), "%s/%s", path, name) >= static_cast<int>(sizeof(destination))) return 1;
        path = destination;
    }
    if (!moveFile(arguments[1], path)) {
        fputs("mv: operation failed\n", stderr);
        return 1;
    }
    return 0;
}
