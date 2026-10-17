#include <cstdio>
#include <unistd.h>

int main(int argumentCount, char** arguments) {
    if (argumentCount < 2) {
        fputs("usage: rm FILE...\n", stderr);
        return 1;
    }
    int status = 0;
    for (int index = 1; index < argumentCount; ++index) {
        if (unlink(arguments[index]) != 0) {
            perror(arguments[index]);
            status = 1;
        }
    }
    return status;
}
