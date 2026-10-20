#pragma once

#include "os/users.h"
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace users {
class DirectoryManager {
   public:
    bool initialize() {
        if (!removeTree("/run", 0)) return false;
        const char* directories[] = {"/media", "/run", "/run/lock", "/run/user",
            "/lib", "/var", "/var/log", "/var/lib", "/var/lib/inpkg", "/var/cache", "/var/tmp",
            "/etc/xdg", "/usr/share", "/usr/local", "/usr/local/share"};
        for (const char* directory : directories)
            if (!ensure(directory, !strcmp(directory, "/var/tmp") ? 01777 : 0755, 0, 0)) return false;
        struct stat legacy{};
        if (!lstat("/etc/inpkg/system", &legacy) && !merge("/etc/inpkg/system", "/var/lib/inpkg", 0)) return false;
        return true;
    }

    bool prepare(const os::UserProfile& profile) {
        if (profile.home[0] != '/' || !strcmp(profile.home, "/") ||
            !ensure(profile.home, profile.uid ? 0755 : 0700, profile.uid, profile.gid)) return false;
        char old[512], current[512];
        if (!join(old, sizeof(old), profile.home, "app") ||
            !join(current, sizeof(current), profile.home, ".local")) return false;
        struct stat status{};
        if (!lstat(old, &status) && !merge(old, current, 0)) return false;
        const char* suffixes[] = {".local", ".local/bin", ".local/lib", ".local/include",
            ".local/share", ".local/state", ".config", ".cache", ".local/state/inpkg"};
        for (const char* suffix : suffixes) {
            if (!join(current, sizeof(current), profile.home, suffix) ||
                !ensure(current, 0700, profile.uid, profile.gid)) return false;
        }
        if (snprintf(old, sizeof(old), "/etc/inpkg/%s", profile.name) >= static_cast<int>(sizeof(old))) return false;
        if (!lstat(old, &status) && !merge(old, current, 0)) return false;
        return true;
    }

    bool runtime(const os::UserProfile& profile) {
        char path[64];
        snprintf(path, sizeof(path), "/run/user/%u", profile.uid);
        return ensure(path, 0700, profile.uid, profile.gid);
    }

    bool removeRuntime(uint32_t uid) {
        char path[64];
        snprintf(path, sizeof(path), "/run/user/%u", uid);
        return removeTree(path, 0);
    }

   private:
    static bool join(char* result, size_t capacity, const char* parent, const char* name) {
        return snprintf(result, capacity, "%s/%s", parent, name) < static_cast<int>(capacity);
    }
    static bool ensure(const char* path, mode_t mode, uid_t uid, gid_t gid) {
        if (mkdir(path, mode) && errno != EEXIST) return failure("mkdir", path);
        struct stat status{};
        if (lstat(path, &status) || !S_ISDIR(status.st_mode)) return failure("stat", path);
        if (chmod(path, mode) || chown(path, uid, gid)) return failure("permissions", path);
        return true;
    }
    static bool failure(const char* operation, const char* path) {
        char message[640];
        snprintf(message, sizeof(message), "[USERS] %s %s failed: %d\n", operation, path, errno);
        os::System::log(message);
        return false;
    }
    static bool childName(const char* name) {
        return *name && strcmp(name, ".") && strcmp(name, "..") && !strchr(name, '/');
    }
    static bool merge(const char* source, const char* target, uint32_t depth) {
        if (depth > 32) return false;
        struct stat before{}, after{};
        if (lstat(source, &before)) return errno == ENOENT;
        if (lstat(target, &after)) return errno == ENOENT && rename(source, target) == 0;
        if (!S_ISDIR(before.st_mode) || !S_ISDIR(after.st_mode)) {
            os::System::log("[USERS] preserved migration conflict: ");
            os::System::log(source);
            os::System::log("\n");
            return true;
        }
        DIR* directory = opendir(source);
        if (!directory) return false;
        bool ok = true;
        while (dirent* entry = readdir(directory)) {
            if (!childName(entry->d_name)) continue;
            char old[512], current[512];
            if (!join(old, sizeof(old), source, entry->d_name) ||
                !join(current, sizeof(current), target, entry->d_name) ||
                !merge(old, current, depth + 1)) { ok = false; break; }
            struct stat remaining{};
            if (lstat(old, &remaining) && errno == ENOENT) rewinddir(directory);
        }
        closedir(directory);
        if (ok && rmdir(source) && errno != ENOTEMPTY) ok = false;
        return ok;
    }
    static bool removeTree(const char* path, uint32_t depth) {
        if (depth > 32) return false;
        struct stat status{};
        if (lstat(path, &status)) return errno == ENOENT;
        if (!S_ISDIR(status.st_mode)) return unlink(path) == 0;
        DIR* directory = opendir(path);
        if (!directory) return false;
        bool ok = true;
        while (dirent* entry = readdir(directory)) {
            if (!childName(entry->d_name)) continue;
            char child[512];
            if (!join(child, sizeof(child), path, entry->d_name) ||
                !removeTree(child, depth + 1)) { ok = false; break; }
            rewinddir(directory);
        }
        closedir(directory);
        return ok && rmdir(path) == 0;
    }
};
}  // namespace users
