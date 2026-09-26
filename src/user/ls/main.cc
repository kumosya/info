#include <dirent.h>
#include <stdio.h>

int main(int argumentCount, char** arguments) {
    const char* path = argumentCount > 1 ? arguments[1] : ".";
    DIR* directory = opendir(path);
    if (directory == nullptr) {
        perror(path);
        return 1;
    }
    while (dirent* entry = readdir(directory)) {
        puts(entry->d_name);
    }
    return closedir(directory) == 0 ? 0 : 1;
}
