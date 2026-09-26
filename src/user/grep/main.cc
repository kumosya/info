#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace {

int search(int descriptor, const char* pattern, const char* name,
           bool showName) {
    char line[256]{};
    size_t length = 0;
    int found     = 0;
    char buffer[256];
    while (true) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count < 0) return 2;
        for (ssize_t index = 0; index < count; ++index) {
            const char value = buffer[index];
            if (value != '\n' && length + 1 < sizeof(line)) {
                line[length++] = value;
            }
            if (value == '\n' || length + 1 == sizeof(line)) {
                line[length] = '\0';
                if (strstr(line, pattern) != nullptr) {
                    if (showName) {
                        fputs(name, stdout);
                        putchar(':');
                    }
                    puts(line);
                    found = 1;
                }
                length = 0;
            }
        }
        if (count == 0) break;
    }
    if (length != 0) {
        line[length] = '\0';
        if (strstr(line, pattern) != nullptr) {
            if (showName) {
                fputs(name, stdout);
                putchar(':');
            }
            puts(line);
            found = 1;
        }
    }
    return found ? 0 : 1;
}

}  // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount < 2) {
        fputs("usage: grep PATTERN [FILE...]\n", stderr);
        return 2;
    }
    if (argumentCount == 2)
        return search(STDIN_FILENO, arguments[1], "", false);
    int result = 1;
    for (int index = 2; index < argumentCount; ++index) {
        const int descriptor = open(arguments[index], O_RDONLY);
        if (descriptor < 0) {
            perror(arguments[index]);
            result = 2;
            continue;
        }
        const int current = search(descriptor, arguments[1], arguments[index],
                                   argumentCount > 3);
        close(descriptor);
        if (current == 0 && result != 2) result = 0;
        if (current == 2) result = 2;
    }
    return result;
}
