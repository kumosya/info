#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argumentCount, char** arguments) {
    const int pathCount = argumentCount > 1 ? argumentCount - 1 : 1;
    for (int pathIndex = 0; pathIndex < pathCount; ++pathIndex) {
        const char* path =
            argumentCount > 1 ? arguments[pathIndex + 1] : nullptr;
        const int descriptor = path == nullptr ? STDIN_FILENO
                                                : open(path, O_RDONLY);
        if (descriptor < 0) {
            perror(path);
            return 1;
        }
        char content[4096];
        while (true) {
            const ssize_t count = read(descriptor, content, sizeof(content));
            if (count < 0) {
                perror(path == nullptr ? "cat" : path);
                if (descriptor != STDIN_FILENO) close(descriptor);
                return 1;
            }
            if (count == 0) {
                break;
            }
            if (write(STDOUT_FILENO, content, static_cast<size_t>(count)) !=
                count) {
                if (descriptor != STDIN_FILENO) close(descriptor);
                return 1;
            }
        }
        if (descriptor != STDIN_FILENO) close(descriptor);
    }
    return 0;
}
