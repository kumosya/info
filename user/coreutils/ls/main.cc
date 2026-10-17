#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {

class Listing {
   public:
    ~Listing() { flush(); }
    bool option(const char* text) {
        if (strcmp(text, "--color") == 0 || strcmp(text, "--color=always") == 0)
            color_ = true;
        else if (strcmp(text, "--color=never") == 0) color_ = false;
        else if (strcmp(text, "--color=auto") == 0) color_ = isatty(STDOUT_FILENO);
        else if (text[1] != '-') {
            for (const char* value = text + 1; *value; ++value) {
                if (*value == 'l') long_ = true;
                else if (*value == 'a') all_ = true;
                else if (*value == '1') one_ = true;
                else return false;
            }
        } else return false;
        return true;
    }

    bool list(const char* path) {
        struct stat status{};
        if (stat(path, &status) != 0) { perror(path); return false; }
        if (!S_ISDIR(status.st_mode)) {
            print(path, status);
            finish();
            return true;
        }
        DIR* directory = opendir(path);
        if (!directory) { perror(path); return false; }
        bool success = true;
        // The VFS omits dot entries for all filesystems, including /dev.
        if (all_) {
            print(".", status);
            char parent[512];
            if (join(parent, path, "..") && stat(parent, &status) == 0)
                print("..", status);
        }
        errno = 0;
        while (dirent* entry = readdir(directory)) {
            if (!all_ && entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char full[512];
            if (!join(full, path, entry->d_name) || stat(full, &status) != 0) {
                perror(entry->d_name);
                success = false;
            } else print(entry->d_name, status);
            errno = 0;
        }
        if (errno) { perror(path); success = false; }
        if (closedir(directory) != 0) success = false;
        finish();
        return success && outputOk_;
    }

   private:
    static bool join(char* target, const char* directory, const char* name) {
        char path[512], cwd[512];
        if (directory[0] != '/' && !getcwd(cwd, sizeof(cwd))) return false;
        const int count = directory[0] == '/'
            ? snprintf(path, sizeof(path), "%s/%s", directory, name)
            : snprintf(path, sizeof(path), "%s/%s/%s", cwd, directory, name);
        if (count < 0 || count >= static_cast<int>(sizeof(path))) return false;
        size_t read = 1, write = 1;
        target[0] = '/';
        while (path[read]) {
            while (path[read] == '/') ++read;
            const size_t start = read;
            while (path[read] && path[read] != '/') ++read;
            const size_t length = read - start;
            if (!length || (length == 1 && path[start] == '.')) continue;
            if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
                while (write > 1 && target[write - 1] != '/') --write;
                if (write > 1) --write;
            } else {
                if (write > 1) target[write++] = '/';
                for (size_t i = 0; i < length; ++i) target[write++] = path[start + i];
            }
        }
        target[write] = 0;
        return true;
    }
    static char type(mode_t mode) {
        switch (mode & S_IFMT) {
            case S_IFDIR: return 'd';
            case S_IFBLK: return 'b';
            case S_IFCHR: return 'c';
            case 0120000: return 'l';
            case 0010000: return 'p';
            case 0140000: return 's';
            default: return '-';
        }
    }
    static void permissions(mode_t mode, char* text) {
        text[0] = type(mode);
        constexpr char letters[] = "rwxrwxrwx";
        for (uint32_t i = 0; i < 9; ++i)
            text[i + 1] = mode & (0400U >> i) ? letters[i] : '-';
        if (mode & 04000) text[3] = mode & 0100 ? 's' : 'S';
        if (mode & 02000) text[6] = mode & 0010 ? 's' : 'S';
        if (mode & 01000) text[9] = mode & 0001 ? 't' : 'T';
        text[10] = 0;
    }
    static void date(time_t seconds, char* text) {
        if (seconds <= 0) { strcpy(text, "-               "); return; }
        uint64_t days = static_cast<uint64_t>(seconds) / 86400;
        uint32_t year = 1970, month = 0;
        auto leap = [](uint32_t y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); };
        while (days >= (leap(year) ? 366U : 365U)) {
            days -= leap(year) ? 366 : 365;
            if (++year > 9999) { strcpy(text, "?               "); return; }
        }
        constexpr uint8_t lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        while (month < 11) {
            const uint32_t length = lengths[month] + (month == 1 && leap(year));
            if (days < length) break;
            days -= length;
            ++month;
        }
        snprintf(text, 24, "%04u-%02u-%02u %02u:%02u", year, month + 1,
            static_cast<uint32_t>(days + 1), static_cast<uint32_t>(seconds / 3600 % 24),
            static_cast<uint32_t>(seconds / 60 % 60));
    }
    void color(const struct stat& status) {
        const char kind = type(status.st_mode);
        const char* key = kind == 'd' ? "di" : kind == 'b' ? "bd" : kind == 'c' ? "cd"
            : kind == 'l' ? "ln" : kind == 'p' ? "pi" : kind == 's' ? "so"
            : (status.st_mode & 0111) ? "ex" : "fi";
        const char* code = kind == 'd' ? "01;34" : kind == 'b' || kind == 'c' ? "01;33"
            : kind == 'l' ? "01;36" : kind == 'p' || kind == 's' ? "35"
            : (status.st_mode & 0111) ? "01;32" : "0";
        char custom[64]{};
        const char* colors = getenv("LS_COLORS");
        for (const char* p = colors; p && *p;) {
            const char* end = p;
            while (*end && *end != ':') ++end;
            if (end - p >= 3 && p[0] == key[0] && p[1] == key[1] && p[2] == '=') {
                size_t length = static_cast<size_t>(end - p - 3);
                bool valid = length < sizeof(custom);
                for (size_t i = 0; i < length && valid; ++i) {
                    custom[i] = p[3 + i];
                    valid = (custom[i] >= '0' && custom[i] <= '9') || custom[i] == ';';
                }
                if (valid) { custom[length] = 0; code = custom; }
            }
            p = *end ? end + 1 : end;
        }
        printf("\033[%sm", code);
    }
    void print(const char* name, const struct stat& status) {
        const uint32_t length = strlen(name);
        if (long_) {
            char mode[11], modified[24];
            permissions(status.st_mode, mode);
            date(status.st_mtime, modified);
            printf("%s %3llu %5u %5u ", mode,
                static_cast<unsigned long long>(status.st_nlink), status.st_uid, status.st_gid);
            if (S_ISBLK(status.st_mode) || S_ISCHR(status.st_mode))
                printf("%3u,%3u ", major(status.st_rdev), minor(status.st_rdev));
            else printf("%7lld ", static_cast<long long>(status.st_size));
            printf("%s ", modified);
        } else if (column_ && (one_ || column_ + 2 + length > width_)) {
            putchar('\n');
            column_ = 0;
        } else if (column_) {
            fputs("  ", stdout);
            column_ += 2;
        }
        if (color_) color(status);
        // Keep filenames from injecting control sequences into the terminal.
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
            putchar(*p >= 32 && *p != 127 ? *p : '?');
        if (color_) fputs("\033[0m", stdout);
        if (long_) putchar('\n');
        else column_ += length;
    }
    void finish() { if (column_) putchar('\n'); column_ = 0; flush(); }
    void putchar(int value) {
        if (used_ == sizeof(output_)) flush();
        output_[used_++] = static_cast<char>(value);
    }
    void fputs(const char* text, FILE*) { while (*text) putchar(*text++); }
    void printf(const char* format, ...) {
        char text[512];
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(text, sizeof(text), format, arguments);
        va_end(arguments);
        fputs(text, stdout);
    }
    void flush() {
        size_t sent = 0;
        while (sent < used_) {
            const ssize_t count = write(STDOUT_FILENO, output_ + sent, used_ - sent);
            if (count <= 0) { outputOk_ = false; break; }
            sent += count;
        }
        used_ = 0;
    }

    bool long_ = false;
    bool all_ = false;
    bool one_ = false;
    bool color_ = isatty(STDOUT_FILENO);
    uint32_t column_ = 0;
    uint32_t width_ = 80;
    char output_[2048]{};
    size_t used_ = 0;
    bool outputOk_ = true;
};
}  // namespace

int main(int argc, char** argv) {
    Listing listing;
    bool options = true;
    int paths = 0;
    for (int i = 1; i < argc; ++i) {
        if (options && strcmp(argv[i], "--") == 0) { options = false; continue; }
        if (options && argv[i][0] == '-' && argv[i][1]) {
            if (!listing.option(argv[i])) {
                fputs("usage: ls [-la1] [--color=auto|always|never] [FILE ...]\n", stderr);
                return 2;
            }
        } else ++paths;
    }
    if (!paths) return listing.list(".") ? 0 : 1;
    int result = 0;
    options = true;
    for (int i = 1; i < argc; ++i) {
        if (options && strcmp(argv[i], "--") == 0) { options = false; continue; }
        if (options && argv[i][0] == '-' && argv[i][1]) continue;
        if (paths > 1) printf("%s:\n", argv[i]);
        if (!listing.list(argv[i])) result = 1;
    }
    return result;
}
