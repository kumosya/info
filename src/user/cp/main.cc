#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

namespace {

bool copyFile(const char* source, const char* destination) {
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
    char buffer[512];
    bool success = true;
    while (true) {
        const ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0) {
            perror(source);
            success = false;
            break;
        }
        if (count == 0) break;
        if (write(output, buffer, static_cast<size_t>(count)) != count) {
            perror(destination);
            success = false;
            break;
        }
    }
    success = close(input) == 0 && close(output) == 0 && success;
    return success;
}

}  // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount != 3) {
        fputs("usage: cp SOURCE DESTINATION\n", stderr);
        return 1;
    }
    return copyFile(arguments[1], arguments[2]) ? 0 : 1;
}
