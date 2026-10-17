#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class Tree {
   public:
    void showAll() { all_ = true; }
    void onlyDirectories() { directoriesOnly_ = true; }
    void setLimit(unsigned value) { limit_ = value; }
    bool walk(const char* path, unsigned depth = 0) {
        if (depth >= limit_) return true;
        DIR* directory = opendir(path);
        if (!directory) { perror(path); return false; }
        bool ok = true;
        while (dirent* entry = readdir(directory)) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || (!all_ && entry->d_name[0] == '.')) continue;
            char child[384];
            if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= static_cast<int>(sizeof(child))) { ok = false; continue; }
            struct stat status{};
            if (stat(child, &status)) { perror(child); ok = false; continue; }
            const bool folder = S_ISDIR(status.st_mode);
            if (directoriesOnly_ && !folder) continue;
            for (unsigned i = 0; i < depth; ++i) fputs("|   ", stdout);
            printf("|-- %s%s%s\n", isatty(1) && folder ? "\033[01;34m" : "", entry->d_name,
                   isatty(1) && folder ? "\033[0m" : "");
            if (folder) { ++directories_; ok = walk(child, depth + 1) && ok; } else ++files_;
        }
        closedir(directory); return ok;
    }
    void summary() const { printf("\n%u directories, %u files\n", directories_, files_); }
   private:
    bool all_ = false, directoriesOnly_ = false;
    unsigned limit_ = 32;
    unsigned directories_ = 0, files_ = 0;
};
}
int main(int argc, char** argv) {
    Tree tree; const char* path = ".";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-a")) tree.showAll();
        else if (!strcmp(argv[i], "-d")) tree.onlyDirectories();
        else if (!strcmp(argv[i], "-L") && i + 1 < argc) {
            const int depth = atoi(argv[++i]); if (depth < 1 || depth > 64) return 2;
            tree.setLimit(depth);
        } else if (argv[i][0] == '-') { fputs("usage: tree [-a] [-d] [-L DEPTH] [PATH]\n", stderr); return 2; }
        else path = argv[i];
    }
    puts(path); const bool ok = tree.walk(path); tree.summary(); return ok ? 0 : 1;
}
