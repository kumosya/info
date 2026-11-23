/* 服务对象的成员函数实现。 */
#include "service/users/users.h"

namespace users {

    /* 创建系统目录并迁移旧包管理数据。 */
    bool DirectoryManager::init() {
        const char* directories[] = {
            "/media",         "/run",       "/run/lock",       "/run/user",
            "/lib",           "/var",       "/var/log",        "/var/lib",
            "/var/lib/inpkg", "/var/cache", "/var/tmp",        "/etc/xdg",
            "/usr/share",     "/usr/local", "/usr/local/share"};
        for (const char* directory : directories) {
            if (!ensure(directory,
                        !strcmp(directory, "/var/tmp") ? 01777 : 0755, 0, 0)) {
                return false;
            }
        }

        struct stat legacy{};
        if (!lstat("/etc/inpkg/system", &legacy) &&
            !merge("/etc/inpkg/system", "/var/lib/inpkg", 0)) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    /* 准备用户主目录与配置目录。 */
    bool DirectoryManager::prepare(const services::wire::UserProfile& profile) {
        if (!initialized_) {
            return false;
        }

        if (profile.home[0] != '/' || !strcmp(profile.home, "/") ||
            !ensure(profile.home, profile.uid ? 0755 : 0700, profile.uid,
                    profile.gid)) {
            return false;
        }

        char old[512], current[512];
        if (!join(old, sizeof(old), profile.home, "app") ||
            !join(current, sizeof(current), profile.home, ".local")) {
            return false;
        }

        struct stat status{};
        if (!lstat(old, &status) && !merge(old, current, 0)) {
            return false;
        }

        const char* suffixes[] = {
            ".local",         ".local/bin",   ".local/lib",
            ".local/include", ".local/share", ".local/state",
            ".config",        ".cache",       ".local/state/inpkg"};
        for (const char* suffix : suffixes) {
            if (!join(current, sizeof(current), profile.home, suffix) ||
                !ensure(current, 0700, profile.uid, profile.gid)) {
                return false;
            }
        }

        if (std::snprintf(old, sizeof(old), "/etc/inpkg/%s", profile.name) >=
            static_cast<int>(sizeof(old))) {
            return false;
        }

        if (!lstat(old, &status) && !merge(old, current, 0)) {
            return false;
        }

        return true;
    }

    /* 创建用户本次登录使用的运行目录。 */
    bool DirectoryManager::runtime(const services::wire::UserProfile& profile) {
        char path[64];
        std::snprintf(path, sizeof(path), "/run/user/%u", profile.uid);
        return ensure(path, 0700, profile.uid, profile.gid);
    }

    /* 清理已退出用户的运行目录。 */
    bool DirectoryManager::removeRuntime(uint32_t uid) {
        char path[64];
        std::snprintf(path, sizeof(path), "/run/user/%u", uid);
        return removeTree(path, 0);
    }

    /* 创建目录并设置所属用户和权限。 */
    bool DirectoryManager::ensure(const char* path, mode_t mode, uid_t uid,
                                  gid_t gid) {
        if (mkdir(path, mode) && errno != EEXIST) {
            return failure("mkdir", path);
        }

        struct stat status{};
        if (lstat(path, &status) || !S_ISDIR(status.st_mode)) {
            return failure("stat", path);
        }

        const bool modeMatches = (status.st_mode & 07777) == mode;
        const bool ownerMatches = status.st_uid == uid && status.st_gid == gid;
        if ((!modeMatches && chmod(path, mode)) ||
            (!ownerMatches && chown(path, uid, gid))) {
            return failure("permissions", path);
        }

        return true;
    }

    /* 记录目录操作失败的路径。 */
    bool DirectoryManager::failure(const char* operation, const char* path) {
        char message[640];
        std::snprintf(message, sizeof(message), "[USERS] %s %s failed: %d\n",
                      operation, path, errno);
        os::Sys().log(message);
        return false;
    }

    /* 按目录树合并旧配置和数据。 */
    bool DirectoryManager::merge(const char* source, const char* target,
                                 uint32_t depth) {
        if (depth > 32) {
            return false;
        }

        struct stat before{}, after{};
        if (lstat(source, &before)) {
            return errno == ENOENT;
        }

        if (lstat(target, &after)) {
            return errno == ENOENT && rename(source, target) == 0;
        }

        if (!S_ISDIR(before.st_mode) || !S_ISDIR(after.st_mode)) {
            os::Sys().log("[USERS] preserved migration conflict: ");
            os::Sys().log(source);
            os::Sys().log("\n");
            return true;
        }

        DIR* directory = opendir(source);
        if (!directory) {
            return false;
        }

        bool ok = true;
        while (dirent* entry = readdir(directory)) {
            if (!childName(entry->d_name)) {
                continue;
            }

            char old[512], current[512];
            if (!join(old, sizeof(old), source, entry->d_name) ||
                !join(current, sizeof(current), target, entry->d_name) ||
                !merge(old, current, depth + 1)) {
                ok = false;
                break;
            }

            struct stat remaining{};
            if (lstat(old, &remaining) && errno == ENOENT) {
                rewinddir(directory);
            }
        }

        closedir(directory);
        if (ok && rmdir(source) && errno != ENOTEMPTY) {
            ok = false;
        }

        return ok;
    }

    /* 逐层删除用户运行目录的内容。 */
    bool DirectoryManager::removeTree(const char* path, uint32_t depth) {
        if (depth > 32) {
            return false;
        }

        struct stat status{};
        if (lstat(path, &status)) {
            return errno == ENOENT;
        }

        if (!S_ISDIR(status.st_mode)) {
            return unlink(path) == 0;
        }

        DIR* directory = opendir(path);
        if (!directory) {
            return false;
        }

        bool ok = true;
        while (dirent* entry = readdir(directory)) {
            if (!childName(entry->d_name)) {
                continue;
            }

            char child[512];
            if (!join(child, sizeof(child), path, entry->d_name) ||
                !removeTree(child, depth + 1)) {
                ok = false;
                break;
            }

            rewinddir(directory);
        }

        closedir(directory);
        return ok && rmdir(path) == 0;
    }

}  // namespace users
