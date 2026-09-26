#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argumentCount, char** arguments) {
    if (argumentCount < 2) {
        fputs("usage: touch FILE...\n", stderr);
        return 1;
    }
    int status = 0;
    for (int index = 1; index < argumentCount; ++index) {
        const int descriptor = open(arguments[index], O_WRONLY | O_CREAT);
        if (descriptor < 0) {
            perror(arguments[index]);
            status = 1;
        } else {
            close(descriptor);
        }
    }
    return status;
}
