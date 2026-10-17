#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../coreutils/user-prompt.h"

extern "C" char** environ;

namespace {
class PackageManager {
   public:
    ~PackageManager() {
        if (locked_) { char owner[384]; snprintf(owner, sizeof(owner), "%s/owner", lock_); unlink(owner); rmdir(lock_); }
        if (*temporary_) removeTree(temporary_, 0);
    }
    int run(int argc, char** argv) {
        const char* operation = nullptr; const char* argument = nullptr;
        const char* scope = nullptr;
        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "-y") || !strcmp(argv[i], "--yes")) yes_ = true;
            else if (!strcmp(argv[i], "--system")) scope = "system";
            else if (!strcmp(argv[i], "--root")) scope = "root";
            else if (!strcmp(argv[i], "--local")) scope = "local";
            else if (argv[i][0] == '-') return usage();
            else if (!operation) operation = argv[i];
            else if (!argument) argument = argv[i];
            else return usage();
        }
        if (!operation || !selectRoot(scope, !strcmp(operation, "list") || !strcmp(operation, "info"))) return usage();
        if (!strcmp(operation, "list")) return list();
        if (!argument) return usage();
        if (!strcmp(operation, "install")) return install(argument) ? 0 : 1;
        if (!validName(argument)) { fputs("inpkg: invalid package name\n", stderr); return 1; }
        snprintf(record_, sizeof(record_), "%s/%s.conf", database_, argument);
        if (!strcmp(operation, "info")) return show() ? 0 : 1;
        if (!strcmp(operation, "remove") || !strcmp(operation, "uninstall")) return remove() ? 0 : 1;
        return usage();
    }
   private:
    static int usage() { fputs("usage: inpkg install FILE.inpk|DIR | remove NAME | list | info NAME [--local|--system|--root] [-y]\n", stderr); return 2; }
    bool lock() {
        snprintf(lock_, sizeof(lock_), "%s/.lock", database_);
        char owner[384]; snprintf(owner, sizeof(owner), "%s/owner", lock_);
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            if (mkdir(lock_, 0700) == 0) {
                FILE* output = fopen(owner, "w");
                if (!output) { rmdir(lock_); return false; }
                bool ok = fprintf(output, "%d\n", getpid()) > 0;
                ok = fclose(output) == 0 && ok;
                if (!ok) { unlink(owner); rmdir(lock_); return false; }
                locked_ = true; return true;
            }
            FILE* input = fopen(owner, "r"); char text[32]{};
            if (!input) break;
            const bool read = fgets(text, sizeof(text), input) != nullptr; fclose(input);
            const uint64_t pid = strtoul(text, nullptr, 10);
            os::TaskInfo info{};
            if (!read || !pid || (!os::invokeSyscall(os::SyscallNumber::QueryTask, pid, reinterpret_cast<uint64_t>(&info)) && info.state != 2)) break;
            if (unlink(owner) || rmdir(lock_)) break;
        }
        fputs("inpkg: another package operation holds this scope\n", stderr); return false;
    }
    static bool validName(const char* value) {
        if (!*value || strlen(value) > 63 || !strcmp(value, ".") || !strcmp(value, "..")) return false;
        for (; *value; ++value)
            if (!(*value >= 'a' && *value <= 'z') && !(*value >= 'A' && *value <= 'Z') &&
                !(*value >= '0' && *value <= '9') && *value != '_' && *value != '-' && *value != '+' && *value != '.') return false;
        return true;
    }
    static bool relative(const char* path) {
        if (strncmp(path, "bin/", 4) && strncmp(path, "sbin/", 5) && strncmp(path, "lib/", 4) && strncmp(path, "include/", 8)) return false;
        const char* start = path;
        for (const char* p = path;; ++p) {
            if (*p && *p != '/') { if (static_cast<unsigned char>(*p) < 32 || *p == '\\') return false; continue; }
            const size_t length = p - start;
            if (!length || (length == 1 && *start == '.') || (length == 2 && start[0] == '.' && start[1] == '.')) return false;
            if (!*p) break;
            start = p + 1;
        }
        return strlen(path) < sizeof(files_[0]);
    }
    bool selectRoot(const char* scope, bool listing) {
        if (!scope && geteuid() == 0 && getenv("SUDO_USER") && !listing) {
            puts("Installation scope: 1) system (configured absolute paths)  2) /root/app (root only)");
            if (yes_) scope = "system";
            else { char choice[16]; fputs("Choose [1/2]: ", stdout); if (!fgets(choice, sizeof(choice), stdin)) return false;
                if (choice[0] == '1') scope = "system"; else if (choice[0] == '2') scope = "root"; else return false; }
        }
        if (!scope) scope = geteuid() == 0 ? "system" : "local";
        if (!strcmp(scope, "system")) {
            if (geteuid() && !listing) { fputs("inpkg: use sudo for system packages\n", stderr); return false; }
            strcpy(root_, "/"); strcpy(database_, "/etc/inpkg/system"); system_ = true;
        } else {
            const uint32_t uid = !strcmp(scope, "root") ? 0 : getuid();
            if (!strcmp(scope, "root") && geteuid()) return false;
            os::UserProfile profile{};
            if (!coreutils::account(uid, profile)) return false;
            if (snprintf(root_, sizeof(root_), "%s/app", profile.home) >= static_cast<int>(sizeof(root_))) return false;
            snprintf(database_, sizeof(database_), "/etc/inpkg/%s", profile.name);
        }
        return true;
    }
    bool confirm(const char* operation) const {
        printf("%s package %s (%u files) in %s\n", operation, name_, count_, root_);
        if (yes_) return true;
        char answer[16]; fputs("Continue? [y/N]: ", stdout);
        return fgets(answer, sizeof(answer), stdin) && (answer[0] == 'y' || answer[0] == 'Y');
    }
    static char* trim(char* text) {
        while (*text == ' ' || *text == '\t') ++text;
        size_t length = strlen(text);
        while (length && (text[length-1] == ' ' || text[length-1] == '\t' || text[length-1] == '\r')) text[--length] = 0;
        return text;
    }
    static bool member(const char* path) {
        if (!*path || *path == '/' || strlen(path) >= 256) return false;
        const char* start = path;
        for (const char* p = path;; ++p) {
            if (*p && *p != '/') { if (static_cast<unsigned char>(*p) < 32 || *p == '\\') return false; continue; }
            const size_t length = p - start;
            if (!length || (length == 1 && *start == '.') || (length == 2 && start[0] == '.' && start[1] == '.')) return false;
            if (!*p) return true;
            start = p + 1;
        }
    }
    bool addMapping(char* text) {
        char* arrow = strstr(text, "->"); if (!arrow) return false; *arrow = 0;
        char* source = trim(text); char* target = trim(arrow + 2);
        if (!member(source) || *target != '/' || !member(target + 1)) return false;
        const char* portable = !strncmp(target, "/usr/", 5) ? target + 5 : target + 1;
        if ((!system_ && !relative(portable)) || count_ == 128) return false;
        const char* destination = system_ ? target + 1 : portable;
        for (unsigned i = 0; i < count_; ++i)
            if (!strcmp(files_[i], destination) || !strcmp(sources_[i], source)) return false;
        strcpy(files_[count_], destination); strcpy(sources_[count_], source); ++count_; return true;
    }
    static bool directories(const char* path) {
        char prefix[384];
        if (strlen(path) >= sizeof(prefix)) return false;
        strcpy(prefix, path);
        for (size_t i = 1; prefix[i]; ++i) if (prefix[i] == '/') {
            prefix[i] = 0;
            struct stat status{};
            if (stat(prefix, &status) ? mkdir(prefix, 0755) != 0 : !S_ISDIR(status.st_mode)) return false;
            prefix[i] = '/';
        }
        return true;
    }
    bool paths(unsigned index, char* source, char* destination) const {
        return snprintf(source, 384, "%s/%s", source_, sources_[index]) < 384 &&
               snprintf(destination, 384, "%s/%s", root_, files_[index]) < 384;
    }
    bool writeRecord() const {
        char temporary[384];
        if (snprintf(temporary, sizeof(temporary), "%s.tmp", record_) >= static_cast<int>(sizeof(temporary))) return false;
        char manifest[384];
        if (snprintf(manifest, sizeof(manifest), "%s/package.conf", source_) >= static_cast<int>(sizeof(manifest))) return false;
        bool ok = copy(manifest, temporary);
        if (ok) ok = rename(temporary, record_) == 0;
        if (!ok) unlink(temporary);
        return ok;
    }
    bool install(const char* argument) {
        if (!lock()) return false;
        if (strlen(argument) >= sizeof(source_)) return false;
        struct stat argumentStatus{};
        if (stat(argument, &argumentStatus)) { perror(argument); return false; }
        if (S_ISREG(argumentStatus.st_mode)) {
            snprintf(temporary_, sizeof(temporary_), "/tmp/inpkg-%d", getpid());
            if (mkdir(temporary_, 0700)) return false;
            const pid_t child = fork();
            if (child == 0) {
                char* arguments[] = {const_cast<char*>("tar"), const_cast<char*>("-xzf"), const_cast<char*>(argument),
                                     const_cast<char*>("-C"), temporary_, nullptr};
                execve("/bin/tar", arguments, environ); _exit(127);
            }
            int status = 0;
            if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
                fputs("inpkg: failed to unpack tar.gz package\n", stderr); return false;
            }
            strcpy(source_, temporary_);
        } else if (S_ISDIR(argumentStatus.st_mode)) strcpy(source_, argument);
        else return false;
        char manifest[384];
        if (snprintf(manifest, sizeof(manifest), "%s/package.conf", source_) >= static_cast<int>(sizeof(manifest))) return false;
        FILE* input = fopen(manifest, "r");
        if (!input) { perror(manifest); return false; }
        char line[384]; bool valid = true;
        while (fgets(line, sizeof(line), input)) {
            char* end = strchr(line, '\n'); if (end) *end = 0;
            if (!*line || *line == '#') continue;
            char* value = strchr(line, '='); if (!value) { valid = false; break; } *value++ = 0;
            value = trim(value);
            if (!strcmp(line, "name")) { if (!validName(value)) { valid = false; break; } strcpy(name_, value); }
            else if (!strcmp(line, "version")) { if (!validName(value)) { valid = false; break; } strcpy(version_, value); }
            else if (!strcmp(line, "install")) { if (!addMapping(value)) { valid = false; break; } }
            else if (!strcmp(line, "file") || !strcmp(line, "bin") || !strcmp(line, "include")) { if (*value != '/' || !member(value + 1)) { valid = false; break; } }
            else { valid = false; break; }
        }
        fclose(input);
        if (!valid || !*name_ || !count_) { fputs("inpkg: invalid manifest\n", stderr); return false; }
        snprintf(record_, sizeof(record_), "%s/%s.conf", database_, name_);
        struct stat status{};
        if (!stat(record_, &status)) { fputs("inpkg: package is already registered; remove it first\n", stderr); return false; }
        for (unsigned i = 0; i < count_; ++i) {
            char source[384], destination[384];
            if (!paths(i, source, destination) || stat(source, &status) || !S_ISREG(status.st_mode)) { fputs("inpkg: missing package file\n", stderr); return false; }
            if (!stat(destination, &status)) { fputs("inpkg: destination already exists\n", stderr); return false; }
        }
        if (!confirm("Install")) return false;
        unsigned copied = 0;
        for (; copied < count_; ++copied) {
            char source[384], destination[384]; paths(copied, source, destination);
            if (!directories(destination) || !copy(source, destination)) break;
        }
        if (copied == count_ && writeRecord()) { puts("Package installed."); return true; }
        for (unsigned i = 0; i < count_ && i <= copied; ++i) {
            char source[384], destination[384]; paths(i, source, destination);
            unlink(destination);
        }
        unlink(record_); fputs("inpkg: installation failed and was rolled back\n", stderr); return false;
    }
    static bool copy(const char* source, const char* destination) {
        struct stat status{};
        if (stat(source, &status)) return false;
        const int input = open(source, O_RDONLY); if (input < 0) return false;
        const int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, status.st_mode & 0777);
        if (output < 0) { close(input); return false; }
        char buffer[4096]; bool ok = true;
        while (true) {
            const ssize_t count = read(input, buffer, sizeof(buffer));
            if (count <= 0) { ok = count == 0; break; }
            size_t written = 0;
            while (written < static_cast<size_t>(count)) {
                const ssize_t result = write(output, buffer + written, count - written);
                if (result <= 0) { ok = false; break; } written += result;
            }
            if (!ok) break;
        }
        close(input); if (close(output)) ok = false; return ok;
    }
    bool remove() {
        if (!lock()) return false;
        FILE* input = fopen(record_, "r");
        if (!input) { perror(record_); return false; }
        char line[384]; bool valid = true;
        while (fgets(line, sizeof(line), input)) {
            char* end = strchr(line, '\n'); if (end) *end = 0;
            if (!strncmp(line, "name=", 5)) { if (!validName(line + 5)) valid = false; else strcpy(name_, line + 5); }
            else if (!strncmp(line, "install=", 8) && !addMapping(line + 8)) valid = false;
        }
        fclose(input);
        if (!valid || !count_ || !*name_ || !confirm("Remove")) return false;
        bool ok = true;
        for (unsigned i = count_; i; --i) {
            char destination[384]; struct stat status{};
            if (snprintf(destination, sizeof(destination), "%s/%s", root_, files_[i-1]) >= static_cast<int>(sizeof(destination))) { ok = false; continue; }
            if (!stat(destination, &status) && unlink(destination)) { perror(destination); ok = false; }
        }
        if (ok) ok = unlink(record_) == 0;
        puts(ok ? "Package removed." : "inpkg: some files remain; registration kept for retry");
        return ok;
    }
    int list() const {
        DIR* directory = opendir(database_); if (!directory) { perror(database_); return 1; }
        while (dirent* entry = readdir(directory)) {
            const size_t length = strlen(entry->d_name);
            if (length > 5 && !strcmp(entry->d_name + length - 5, ".conf")) printf("%s\n", entry->d_name);
        }
        closedir(directory); return 0;
    }
    bool show() const {
        FILE* input = fopen(record_, "r"); if (!input) { perror(record_); return false; }
        char line[384]; while (fgets(line, sizeof(line), input)) fputs(line, stdout);
        fclose(input); return true;
    }
    static bool removeTree(const char* path, unsigned depth) {
        if (depth > 64) return false;
        struct stat status{}; if (stat(path, &status)) return false;
        if (!S_ISDIR(status.st_mode)) return unlink(path) == 0;
        DIR* directory = opendir(path); if (!directory) return false;
        bool ok = true;
        while (dirent* entry = readdir(directory)) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char child[384];
            if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= 384 || !removeTree(child, depth + 1)) { ok = false; break; }
        }
        closedir(directory); return ok && rmdir(path) == 0;
    }
    bool yes_ = false;
    char root_[128]{}, database_[128]{}, record_[384]{}, source_[384]{};
    char name_[64]{}, version_[64] = "1";
    char files_[128][256]{};
    char sources_[128][256]{};
    unsigned count_ = 0;
    char lock_[256]{};
    bool locked_ = false;
    bool system_ = false;
    char temporary_[128]{};
};
PackageManager manager;
}
int main(int argc, char** argv) { return manager.run(argc, argv); }
