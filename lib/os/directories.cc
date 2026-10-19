#include "os/directories.h"
#include "os/abi.h"
#include "os/file.h"
#include "os/task.h"
#include "os/users.h"

namespace os {
bool Directories::userId(uint32_t& uid) {
    TaskInfo info{};
    if (Process::queryTask(Process::getTaskId(), info)) return false;
    uid = info.effectiveUid;
    return true;
}

bool Directories::validApplication(const char* application) {
    if (!application || !*application) return true;
    if (Runtime::stringsEqual(application, ".") ||
        Runtime::stringsEqual(application, "..")) return false;
    for (const char* cursor = application; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\' ||
            static_cast<unsigned char>(*cursor) < 32 || *cursor == 127) return false;
    return true;
}

char* Directories::append(const char* base, const char* application,
                           char* buffer, size_t capacity) {
    if (!buffer || !capacity || !base || base[0] != '/') return nullptr;
    size_t length = Runtime::stringLength(base);
    while (length > 1 && base[length - 1] == '/') --length;
    const size_t suffix = application ? Runtime::stringLength(application) : 0;
    const size_t separator = suffix && length > 1 ? 1 : 0;
    if (length >= capacity || separator + suffix >= capacity - length) return nullptr;
    // Permit the caller to append to the same buffer.
    for (size_t index = 0; index < length; ++index) buffer[index] = base[index];
    if (separator) buffer[length++] = '/';
    for (size_t index = 0; index < suffix; ++index) buffer[length++] = application[index];
    buffer[length] = 0;
    return buffer;
}

char* Directories::home(char* buffer, size_t capacity) {
    const char* configured = Runtime::environmentValue("HOME");
    if (configured && configured[0] == '/') return append(configured, nullptr, buffer, capacity);
    uint32_t uid;
    if (!userId(uid)) return nullptr;
    if (!uid) return append("/root", nullptr, buffer, capacity);
    UserCredentials credentials{};
    credentials.id = uid;
    UserProfile profile{};
    if (UserClient::request(UserOperation::Lookup, credentials, profile)) return nullptr;
    return append(profile.home, nullptr, buffer, capacity);
}

char* Directories::path(DirectoryKind kind, DirectoryScope scope, char* buffer,
                         size_t capacity, const char* application) {
    if (!validApplication(application)) return nullptr;
    if (scope == DirectoryScope::System) {
        const char* base = nullptr;
        switch (kind) {
            case DirectoryKind::Data: base = "/usr/share"; break;
            case DirectoryKind::State: base = "/var/lib"; break;
            case DirectoryKind::Config: base = "/etc/xdg"; break;
            case DirectoryKind::Cache: base = "/var/cache"; break;
            case DirectoryKind::Runtime: base = "/run"; break;
            case DirectoryKind::Log: base = "/var/log"; break;
        }
        return append(base, application, buffer, capacity);
    }
    if (scope != DirectoryScope::User) return nullptr;
    const char* variable = nullptr;
    const char* fallback = nullptr;
    switch (kind) {
        case DirectoryKind::Data: variable = "XDG_DATA_HOME"; fallback = ".local/share"; break;
        case DirectoryKind::State:
        case DirectoryKind::Log: variable = "XDG_STATE_HOME"; fallback = ".local/state"; break;
        case DirectoryKind::Config: variable = "XDG_CONFIG_HOME"; fallback = ".config"; break;
        case DirectoryKind::Cache: variable = "XDG_CACHE_HOME"; fallback = ".cache"; break;
        case DirectoryKind::Runtime: variable = "XDG_RUNTIME_DIR"; break;
        default: return nullptr;
    }
    char base[512];
    const char* configured = Runtime::environmentValue(variable);
    if (configured && configured[0] == '/') {
        if (!append(configured, nullptr, base, sizeof(base))) return nullptr;
    } else if (kind == DirectoryKind::Runtime) {
        uint32_t uid;
        if (!userId(uid)) return nullptr;
        char digits[11];
        size_t count = 0;
        do { digits[count++] = static_cast<char>('0' + uid % 10); uid /= 10; } while (uid);
        char decimal[11];
        for (size_t index = 0; index < count; ++index) decimal[index] = digits[count - index - 1];
        decimal[count] = 0;
        if (!append("/run/user", decimal, base, sizeof(base))) return nullptr;
    } else {
        if (!home(base, sizeof(base)) || !append(base, fallback, base, sizeof(base))) return nullptr;
    }
    if (kind == DirectoryKind::Runtime) {
        uint32_t uid;
        File directory;
        if (!userId(uid) || directory.refresh(base) ||
            directory.getType() != FileType::Directory || directory.getUid() != uid ||
            (directory.getMode() & 07777) != 0700) return nullptr;
    }
    if (!append(base, application, buffer, capacity)) return nullptr;
    if (kind == DirectoryKind::Log) return append(buffer, "log", buffer, capacity);
    return buffer;
}

char* Directories::search(bool configuration, uint32_t index, char* buffer,
                           size_t capacity, const char* application) {
    if (!index) return path(configuration ? DirectoryKind::Config : DirectoryKind::Data,
                            DirectoryScope::User, buffer, capacity, application);
    if (!validApplication(application)) return nullptr;
    const char* entries = Runtime::environmentValue(configuration ? "XDG_CONFIG_DIRS" : "XDG_DATA_DIRS");
    if (!entries || !*entries) entries = configuration ? "/etc/xdg" : "/usr/local/share:/usr/share";
    while (*entries) {
        const char* end = entries;
        while (*end && *end != ':') ++end;
        if (entries[0] == '/' && --index == 0) {
            const size_t length = static_cast<size_t>(end - entries);
            char base[512];
            if (length >= sizeof(base)) return nullptr;
            Runtime::copyBytes(base, entries, length);
            base[length] = 0;
            return append(base, application, buffer, capacity);
        }
        entries = *end ? end + 1 : end;
    }
    return nullptr;
}

char* Directories::dataSearch(uint32_t index, char* buffer, size_t capacity,
                               const char* application) {
    return search(false, index, buffer, capacity, application);
}
char* Directories::configSearch(uint32_t index, char* buffer, size_t capacity,
                                 const char* application) {
    return search(true, index, buffer, capacity, application);
}
}  // namespace os
