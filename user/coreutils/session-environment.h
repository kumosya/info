#pragma once

#include "os/directories.h"
#include "os/users.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace coreutils {
class SessionEnvironment {
   public:
    static bool apply(const os::UserProfile& profile, bool replace,
                       bool changeDirectory = true) {
        os::UserCredentials credentials{};
        credentials.id = profile.uid;
        os::UserProfile current{};
        if (os::UserClient::request(os::UserOperation::BeginSession, credentials, current)) return false;
        const char* previous = getenv("USER");
        replace = replace || (previous && strcmp(previous, profile.name));
        char terminal[64] = "osinfo";
        if (const char* value = getenv("TERM"))
            os::Runtime::copyString(terminal, value, sizeof(terminal));
        if (replace && clearenv()) return false;
        const char* home = getenv("HOME");
        if (replace || !home || *home != '/')
            if (setenv("HOME", profile.home, 1)) return false;
        if (setenv("USER", profile.name, 1) || setenv("LOGNAME", profile.name, 1) ||
            setenv("SHELL", profile.shell, 1) || setenv("TERM", terminal, 1)) return false;
        char path[512];
        if (snprintf(path, sizeof(path), "%s/.local/bin:/usr/bin:/usr/sbin:/bin:/sbin", getenv("HOME")) >= static_cast<int>(sizeof(path)) ||
            !defaultValue("PATH", path, false) || !defaultValue("LANG", "C.UTF-8", false)) return false;
        const char* names[] = {"XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"};
        const char* suffixes[] = {".local/share", ".local/state", ".config", ".cache"};
        for (size_t index = 0; index < 4; ++index) {
            if (snprintf(path, sizeof(path), "%s/%s", getenv("HOME"), suffixes[index]) >= static_cast<int>(sizeof(path)) ||
                !defaultValue(names[index], path, true)) return false;
        }
        snprintf(path, sizeof(path), "/run/user/%u", profile.uid);
        if (!defaultValue("XDG_RUNTIME_DIR", path, true) ||
            !defaultValue("XDG_DATA_DIRS", "/usr/local/share:/usr/share", false) ||
            !defaultValue("XDG_CONFIG_DIRS", "/etc/xdg", false) ||
            !os::Directories::runtime(os::DirectoryScope::User, path, sizeof(path))) return false;
        return !changeDirectory || chdir(getenv("HOME")) == 0;
    }
   private:
    static bool defaultValue(const char* name, const char* fallback, bool absolute) {
        const char* value = getenv(name);
        if (value && *value && (!absolute || *value == '/')) return true;
        return setenv(name, fallback, 1) == 0;
    }
};
}  // namespace coreutils
