#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace {

constexpr size_t documentCapacity = 8192;
char document[documentCapacity];
size_t documentSize;

bool load(const char* path) {
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return true;
    while (true) {
        const ssize_t count = read(descriptor, document + documentSize,
                                   sizeof(document) - documentSize);
        if (count < 0) {
            close(descriptor);
            return false;
        }
        if (count == 0) break;
        documentSize += static_cast<size_t>(count);
        if (documentSize == sizeof(document)) break;
    }
    close(descriptor);
    return true;
}

bool save(const char* path) {
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (descriptor < 0) return false;
    const bool result = write(descriptor, document, documentSize) ==
                        static_cast<ssize_t>(documentSize);
    close(descriptor);
    return result;
}

}  // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount != 2) {
        fputs("usage: edit FILE\n", stderr);
        return 1;
    }
    if (!load(arguments[1])) {
        perror(arguments[1]);
        return 1;
    }
    if (documentSize != 0) write(STDOUT_FILENO, document, documentSize);
    puts("-- edit: enter text; :w saves, :q quits, :wq saves and quits --");
    char line[256];
    while (true) {
        fputs("> ", stdout);
        if (fgets(line, sizeof(line), stdin) == nullptr) break;
        if (strcmp(line, ":q\n") == 0) break;
        if (strcmp(line, ":w\n") == 0 || strcmp(line, ":wq\n") == 0) {
            if (!save(arguments[1])) {
                perror(arguments[1]);
                return 1;
            }
            if (line[2] == 'q') break;
            continue;
        }
        const size_t length = strlen(line);
        if (length > sizeof(document) - documentSize) {
            fputs("edit: document is full\n", stderr);
            continue;
        }
        memcpy(document + documentSize, line, length);
        documentSize += length;
    }
    return 0;
}
