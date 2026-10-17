/*
 * main.cc - ustar archiver with gzip compression and decompression.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../archive/gzip.h"

namespace {
class Tar {
   public:
    ~Tar() { free(transfer_); }
    int run(int argc, char** argv) {
        transfer_ = static_cast<unsigned char*>(malloc(8192));
        if (!transfer_) return 1;
        char operation = 0; bool gzip = false;
        const char* archive = nullptr; const char* directory = nullptr;
        auto** names = static_cast<const char**>(calloc(argc, sizeof(char*)));
        if (!names) return 1;
        unsigned count = 0;
        bool options = true;
        for (int i = 1; i < argc; ++i) {
            const char* option = argv[i];
            if (options && !strcmp(option, "--")) { options = false; continue; }
            if (options && (option[0] == '-' || (i == 1 && option[0] && option[0] != '/'))) {
                if (*option == '-') ++option;
                for (; *option; ++option) {
                    const char key = *option;
                    if (key == 'c' || key == 't' || key == 'x') {
                        if (operation && operation != key) { free(names); return usage(); } operation = key;
                    } else if (key == 'z') gzip = true;
                    else if (key == 'v') verbose_ = true;
                    else if (key == 'f' || key == 'C') {
                        const char* value;
                        if (option[1]) { value = option + 1; option += strlen(option + 1); }
                        else if (++i < argc) value = argv[i];
                        else { free(names); return usage(); }
                        if (key == 'f') archive = value; else directory = value;
                    } else { free(names); return usage(); }
                }
            } else names[count++] = argv[i];
        }
        if (!operation || !archive || (operation == 'c' && !count)) { free(names); return usage(); }
        FILE* file = !strcmp(archive, "-") ? (operation == 'c' ? stdout : stdin) : fopen(archive, operation == 'c' ? "wb" : "rb");
        if (!file) { perror(archive); free(names); return 1; }
        if (directory && chdir(directory)) { perror(directory); if (file != stdin && file != stdout) fclose(file); free(names); return 1; }
        bool ok;
        if (operation == 'c') {
            struct stat status{};
            if (!fstat(fileno(file), &status)) { outputDevice_ = status.st_dev; outputInode_ = status.st_ino; }
            auto* output = new (std::nothrow) archive::Output(file, gzip);
            ok = output != nullptr;
            for (unsigned i = 0; i < count && ok; ++i) {
                const char* name = names[i]; while (*name == '/') ++name;
                ok = add(*output, names[i], *name ? name : ".", 0);
            }
            memset(transfer_, 0, 1024);
            ok = ok && output->write(transfer_, 1024) && output->finish();
            delete output;
        } else {
            auto* input = new (std::nothrow) archive::Input(file, gzip);
            ok = input && scan(*input, operation == 'x', names, count) && input->finish();
            delete input;
        }
        if (file != stdin && file != stdout && fclose(file)) ok = false;
        free(names);
        if (!ok) fputs("tar: archive operation failed (invalid data, unsupported entry or I/O error)\n", stderr);
        return ok ? 0 : 1;
    }
   private:
    static int usage() { fputs("usage: tar -c|-t|-x [-zv] -f ARCHIVE [-C DIR] [FILE ...]\n", stderr); return 2; }
    static bool number(char* field, size_t width, uint64_t value) {
        memset(field, '0', width); field[width - 1] = 0;
        for (size_t i = width - 1; i && value; value >>= 3) field[--i] = '0' + (value & 7);
        return value == 0;
    }
    static bool parseNumber(const unsigned char* field, size_t width, uint64_t& value) {
        value = 0; bool ended = false;
        for (size_t i = 0; i < width; ++i) {
            if (!field[i] || field[i] == ' ') { if (value) ended = true; continue; }
            if (ended || field[i] < '0' || field[i] > '7' || value > (UINT64_MAX >> 3)) return false;
            value = value * 8 + field[i] - '0';
        }
        return true;
    }
    static bool safe(const char* name) {
        if (!*name || *name == '/' || strlen(name) >= 384) return false;
        for (const char* p = name; *p;) {
            const char* start = p; while (*p && *p != '/') { if (static_cast<unsigned char>(*p) < 32 || *p == '\\') return false; ++p; }
            if (p - start == 2 && start[0] == '.' && start[1] == '.') return false;
            while (*p == '/') ++p;
        }
        return true;
    }
    static bool parents(const char* name) {
        char path[384]; strcpy(path, name);
        for (size_t i = 1; path[i]; ++i) if (path[i] == '/') {
            path[i] = 0; struct stat status{};
            if (fileStatus(path, status) ? mkdir(path, 0755) != 0 : !S_ISDIR(status.st_mode)) return false;
            path[i] = '/';
        }
        return true;
    }
    static int fileStatus(const char* path, struct stat& status) {
        const int result = fstatat(AT_FDCWD, path, &status, AT_SYMLINK_NOFOLLOW);
        // The current OS has no symlink filesystem backend; Linux tests use nofollow.
        return result && (errno == ENOTSUP || errno == ENOSYS) ? stat(path, &status) : result;
    }
    static bool headerName(unsigned char* header, const char* name) {
        const size_t length = strlen(name);
        if (length <= 100) { memcpy(header, name, length); return true; }
        for (size_t i = length; i; --i) if (name[i - 1] == '/' && i - 1 <= 155 && length - i <= 100) {
            memcpy(header + 345, name, i - 1); memcpy(header, name + i, length - i); return true;
        }
        return false;
    }
    bool add(archive::Output& output, const char* path, const char* name, unsigned depth) {
        if (depth > 64 || !safe(name)) return false;
        struct stat status{};
        if (fileStatus(path, status)) { perror(path); return false; }
        if (status.st_dev == outputDevice_ && status.st_ino == outputInode_) return true;
        if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)) { fprintf(stderr, "tar: unsupported file: %s\n", path); return false; }
        unsigned char* header = header_;
        memset(header, 0, 512);
        if (!headerName(header, name) || !number(reinterpret_cast<char*>(header + 100), 8, status.st_mode & 0777) ||
            !number(reinterpret_cast<char*>(header + 108), 8, status.st_uid) || !number(reinterpret_cast<char*>(header + 116), 8, status.st_gid) ||
            !number(reinterpret_cast<char*>(header + 124), 12, S_ISDIR(status.st_mode) ? 0 : status.st_size) ||
            !number(reinterpret_cast<char*>(header + 136), 12, status.st_mtime < 0 ? 0 : status.st_mtime)) return false;
        header[156] = S_ISDIR(status.st_mode) ? '5' : '0'; memcpy(header + 257, "ustar", 5); memcpy(header + 263, "00", 2);
        memset(header + 148, ' ', 8); unsigned checksum = 0; for (unsigned i = 0; i < 512; ++i) checksum += header[i];
        if (!number(reinterpret_cast<char*>(header + 148), 7, checksum)) return false;
        header[155] = ' ';
        if (!output.write(header, 512)) return false;
        if (verbose_) fprintf(stderr, "%s\n", name);
        if (S_ISDIR(status.st_mode)) {
            DIR* directory = opendir(path); if (!directory) { perror(path); return false; }
            struct Paths { char child[384], stored[384]; };
            auto* paths = new (std::nothrow) Paths;
            if (!paths) { closedir(directory); return false; }
            bool ok = true;
            while (dirent* entry = readdir(directory)) {
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
                if (snprintf(paths->child, sizeof(paths->child), "%s/%s", path, entry->d_name) >= 384 ||
                    snprintf(paths->stored, sizeof(paths->stored), "%s/%s", name, entry->d_name) >= 384 || !add(output, paths->child, paths->stored, depth + 1)) { ok = false; break; }
            }
            if (closedir(directory)) ok = false;
            delete paths;
            return ok;
        }
        FILE* input = fopen(path, "rb"); if (!input) { perror(path); return false; }
        unsigned char* buffer = transfer_; uint64_t remaining = status.st_size; bool ok = true;
        while (remaining) {
            const size_t size = remaining > 8192 ? 8192 : static_cast<size_t>(remaining);
            if (fread(buffer, 1, size, input) != size || !output.write(buffer, size)) { ok = false; break; } remaining -= size;
        }
        if (fclose(input)) ok = false;
        memset(buffer, 0, 512); const size_t padding = (512 - (status.st_size % 512)) % 512;
        return ok && output.write(buffer, padding);
    }
    bool scan(archive::Input& input, bool extract, const char* const* names, unsigned count) {
        unsigned char header[512]; unsigned char* data = transfer_;
        struct Directory { Directory* next; char name[384]; mode_t mode; };
        Directory* directories = nullptr;
        bool ok = true;
        while (ok) {
            if (input.read(header, 512) != 512) { ok = false; break; }
            unsigned sum = 0; for (unsigned byte : header) sum += byte;
            if (!sum) {
                if (input.read(header, 512) != 512) { ok = false; break; }
                for (unsigned byte : header) if (byte) ok = false;
                break;
            }
            uint64_t checksum, size, mode;
            for (unsigned i = 148; i < 156; ++i) sum += ' ' - header[i];
            if (!parseNumber(header + 148, 8, checksum) || checksum != sum || !parseNumber(header + 124, 12, size) ||
                !parseNumber(header + 100, 8, mode) || (header[156] != 0 && header[156] != '0' && header[156] != '5')) { ok = false; break; }
            char name[384]{}; size_t used = 0;
            if (!memcmp(header + 257, "ustar", 5) && header[345]) {
                while (used < 155 && header[345 + used]) { name[used] = header[345 + used]; ++used; } name[used++] = '/';
            }
            for (unsigned i = 0; i < 100 && header[i]; ++i) name[used++] = header[i];
            while (used > 1 && name[used - 1] == '/') name[--used] = 0;
            if (!safe(name)) { ok = false; break; }
            bool selected = count == 0;
            for (unsigned i = 0; i < count; ++i) {
                const size_t length = strlen(names[i]);
                if (!strncmp(name, names[i], length) && (!name[length] || name[length] == '/')) selected = true;
            }
            const bool directory = header[156] == '5';
            if (directory && size) { ok = false; break; }
            if (selected && (!extract || verbose_)) printf("%s%s\n", name, directory ? "/" : "");
            FILE* output = nullptr;
            if (selected && extract) {
                if (!parents(name)) { ok = false; break; }
                struct stat status{};
                if (directory) {
                    if (fileStatus(name, status) ? mkdir(name, 0755) != 0 : !S_ISDIR(status.st_mode)) { ok = false; break; }
                    auto* entry = static_cast<Directory*>(malloc(sizeof(Directory)));
                    if (!entry) { ok = false; break; }
                    strcpy(entry->name, name); entry->mode = mode & 0777; entry->next = directories; directories = entry;
                } else {
                    if (!fileStatus(name, status) && !S_ISREG(status.st_mode)) { ok = false; break; }
                    output = fopen(name, "wb"); if (!output) { perror(name); ok = false; break; }
                }
            }
            uint64_t remaining = size;
            while (remaining && ok) {
                const size_t chunk = remaining > 8192 ? 8192 : static_cast<size_t>(remaining);
                if (input.read(data, chunk) != chunk || (output && fwrite(data, 1, chunk, output) != chunk)) ok = false;
                remaining -= chunk;
            }
            if (output) { if (fclose(output) || chmod(name, mode & 0777)) ok = false; }
            const size_t padding = (512 - (size % 512)) % 512;
            if (ok && input.read(data, padding) != padding) ok = false;
        }
        while (directories) { Directory* entry = directories; directories = entry->next; if (ok && chmod(entry->name, entry->mode)) ok = false; free(entry); }
        return ok && !input.failed();
    }
    bool verbose_ = false;
    unsigned char* transfer_ = nullptr;
    unsigned char header_[512]{};
    dev_t outputDevice_ = static_cast<dev_t>(-1);
    ino_t outputInode_ = 0;
};
}

int main(int argc, char** argv) { Tar command; return command.run(argc, argv); }
