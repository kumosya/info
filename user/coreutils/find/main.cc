#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

namespace {

constexpr size_t pathCapacity = 256;

const char* baseName(const char* path) {
    const char* result = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' && cursor[1] != '\0') result = cursor + 1;
    }
    return result;
}

bool matches(const char* path, const char* name) {
    return name == nullptr || strcmp(baseName(path), name) == 0;
}

int walk(const char* path, const char* name) {
    struct stat status{};
    if (stat(path, &status) != 0) {
        perror(path);
        return 1;
    }
    if (matches(path, name)) puts(path);
    if (!S_ISDIR(status.st_mode)) return 0;
    DIR* directory = opendir(path);
    if (directory == nullptr) {
        perror(path);
        return 1;
    }
    int result = 0;
    while (dirent* entry = readdir(directory)) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[pathCapacity]{};
        const size_t pathLength = strlen(path);
        const size_t nameLength = strlen(entry->d_name);
        const bool root         = pathLength == 1 && path[0] == '/';
        if (pathLength + (root ? 0 : 1) + nameLength + 1 > sizeof(child)) {
            fputs("find: path too long\n", stderr);
            result = 1;
            continue;
        }
        strcpy(child, path);
        if (!root) strcat(child, "/");
        strcat(child, entry->d_name);
        if (walk(child, name) != 0) result = 1;
    }
    closedir(directory);
    return result;
}

}  // namespace

int main(int argumentCount, char** arguments) {
    const char* path = argumentCount > 1 ? arguments[1] : ".";
    const char* name = nullptr;
    if (argumentCount == 4 && strcmp(arguments[2], "-name") == 0) {
        name = arguments[3];
    } else if (argumentCount != 1 && argumentCount != 2) {
        fputs("usage: find [PATH] [-name NAME]\n", stderr);
        return 1;
    }
    return walk(path, name);
}
