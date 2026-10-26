#include <os/dir.h>

#include <os/device.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>
#include <os/user.h>
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"

namespace os {
Dir::Dir() = default;
Dir::~Dir() { close(); }
int Dir::open(Scope scope, const char* application) {
    if (descriptor_) return error_ = static_cast<int>(ErrorCode::Busy);
    if ((scope != Scope::User && scope != Scope::System) ||
        !validApplication(application) ||
        (application &&
         Runtime::stringLength(application) >= sizeof(application_)))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    scope_          = scope;
    application_[0] = 0;
    if (application)
        Runtime::copyString(application_, application, sizeof(application_));
    return error_ = 0;
}

bool Dir::userId(uint32_t& uid) {
    os::wire::TaskInfo info{};
    const int status = os::detail::TaskQuery().query(0, info);
    if (status) {
        error_ = status;
        return false;
    }
    uid = info.effectiveUid;
    return true;
}

bool Dir::validApplication(const char* application) {
    if (!application || !*application) return true;
    if (Runtime::stringsEqual(application, ".") ||
        Runtime::stringsEqual(application, ".."))
        return false;
    for (const char* cursor = application; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\' ||
            static_cast<unsigned char>(*cursor) < 32 || *cursor == 127)
            return false;
    return true;
}

char* Dir::append(const char* base, const char* application, char* buffer,
                  size_t capacity) {
    if (!buffer || !capacity || !base || base[0] != '/') {
        error_ = static_cast<int>(ErrorCode::InvalidArgument);
        return nullptr;
    }
    size_t length = Runtime::stringLength(base);
    while (length > 1 && base[length - 1] == '/') --length;
    const size_t suffix = application ? Runtime::stringLength(application) : 0;
    const size_t separator = suffix && length > 1 ? 1 : 0;
    if (length >= capacity || separator + suffix >= capacity - length) {
        error_ = static_cast<int>(ErrorCode::InvalidArgument);
        return nullptr;
    }
    // Permit the caller to append to the same buffer.
    for (size_t index = 0; index < length; ++index) buffer[index] = base[index];
    if (separator) buffer[length++] = '/';
    for (size_t index = 0; index < suffix; ++index)
        buffer[length++] = application[index];
    buffer[length] = 0;
    error_         = 0;
    return buffer;
}

char* Dir::getHome(char* buffer, size_t capacity) {
    const char* configured = Runtime::environmentValue("HOME");
    if (configured && configured[0] == '/')
        return append(configured, nullptr, buffer, capacity);
    uint32_t uid;
    if (!userId(uid)) return nullptr;
    if (!uid) return append("/root", nullptr, buffer, capacity);

    User::Profile profile{};
    const int status = User().lookup(uid, profile);
    if (status) {
        error_ = static_cast<int>(status == 1 ? ErrorCode::NotFound
                                              : ErrorCode::IoError);
        return nullptr;
    }
    return append(profile.getHome(), nullptr, buffer, capacity);
}

char* Dir::getPath(Dir::Kind kind, char* buffer, size_t capacity) {
    const char* application = application_;
    const auto scope        = scope_;
    if (scope == Dir::Scope::System) {
        const char* base = nullptr;
        switch (kind) {
            case Dir::Kind::Data:
                base = "/usr/share";
                break;
            case Dir::Kind::State:
                base = "/var/lib";
                break;
            case Dir::Kind::Config:
                base = "/etc/xdg";
                break;
            case Dir::Kind::Cache:
                base = "/var/cache";
                break;
            case Dir::Kind::Runtime:
                base = "/run";
                break;
            case Dir::Kind::Log:
                base = "/var/log";
                break;
        }
        return append(base, application, buffer, capacity);
    }
    if (scope != Dir::Scope::User) {
        error_ = static_cast<int>(ErrorCode::InvalidArgument);
        return nullptr;
    }
    const char* variable = nullptr;
    const char* fallback = nullptr;
    switch (kind) {
        case Dir::Kind::Data:
            variable = "XDG_DATA_HOME";
            fallback = ".local/share";
            break;
        case Dir::Kind::State:
        case Dir::Kind::Log:
            variable = "XDG_STATE_HOME";
            fallback = ".local/state";
            break;
        case Dir::Kind::Config:
            variable = "XDG_CONFIG_HOME";
            fallback = ".config";
            break;
        case Dir::Kind::Cache:
            variable = "XDG_CACHE_HOME";
            fallback = ".cache";
            break;
        case Dir::Kind::Runtime:
            variable = "XDG_RUNTIME_DIR";
            break;
        default:
            error_ = static_cast<int>(ErrorCode::InvalidArgument);
            return nullptr;
    }
    char base[512];
    const char* configured = Runtime::environmentValue(variable);
    if (configured && configured[0] == '/') {
        if (!append(configured, nullptr, base, sizeof(base))) return nullptr;
    } else if (kind == Dir::Kind::Runtime) {
        uint32_t uid;
        if (!userId(uid)) return nullptr;
        char digits[11];
        size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + uid % 10);
            uid /= 10;
        } while (uid);
        char decimal[11];
        for (size_t index = 0; index < count; ++index)
            decimal[index] = digits[count - index - 1];
        decimal[count] = 0;
        if (!append("/run/user", decimal, base, sizeof(base))) return nullptr;
    } else {
        if (!getHome(base, sizeof(base)) ||
            !append(base, fallback, base, sizeof(base)))
            return nullptr;
    }
    if (kind == Dir::Kind::Runtime) {
        uint32_t uid;
        File directory;
        if (!userId(uid)) return nullptr;
        if (directory.refresh(base)) {
            error_ = static_cast<int>(ErrorCode::NotFound);
            return nullptr;
        }
        if (directory.getType() != File::Type::Directory ||
            directory.getUid() != uid ||
            (directory.getMode() & 07777) != 0700) {
            error_ = static_cast<int>(ErrorCode::PermissionDenied);
            return nullptr;
        }
    }
    if (!append(base, application, buffer, capacity)) return nullptr;
    if (kind == Dir::Kind::Log) return append(buffer, "log", buffer, capacity);
    return buffer;
}

char* Dir::search(bool configuration, uint32_t index, char* buffer,
                  size_t capacity) {
    if (!index && scope_ == Scope::User)
        return getPath(configuration ? Kind::Config : Kind::Data, buffer,
                       capacity);
    if (scope_ == Scope::User) --index;
    const char* entries = Runtime::environmentValue(
        configuration ? "XDG_CONFIG_DIRS" : "XDG_DATA_DIRS");
    if (!entries || !*entries)
        entries = configuration ? "/etc/xdg" : "/usr/local/share:/usr/share";
    while (*entries) {
        const char* end = entries;
        while (*end && *end != ':') ++end;
        if (entries[0] == '/' && index == 0) {
            const size_t length = static_cast<size_t>(end - entries);
            char base[512];
            if (length >= sizeof(base)) {
                error_ = static_cast<int>(ErrorCode::InvalidArgument);
                return nullptr;
            }
            Runtime::copyBytes(base, entries, length);
            base[length] = 0;
            return append(base, application_, buffer, capacity);
        }
        if (entries[0] == '/' && index) --index;
        entries = *end ? end + 1 : end;
    }
    error_ = static_cast<int>(ErrorCode::NotFound);
    return nullptr;
}

char* Dir::getDataSearch(uint32_t index, char* buffer, size_t capacity) {
    return search(false, index, buffer, capacity);
}
char* Dir::getConfigSearch(uint32_t index, char* buffer, size_t capacity) {
    return search(true, index, buffer, capacity);
}

char* Dir::getData(char* buffer, size_t capacity) {
    return getPath(Dir::Kind::Data, buffer, capacity);
}

char* Dir::getState(char* buffer, size_t capacity) {
    return getPath(Dir::Kind::State, buffer, capacity);
}

char* Dir::getConfig(char* buffer, size_t capacity) {
    return getPath(Dir::Kind::Config, buffer, capacity);
}

char* Dir::getCache(char* buffer, size_t capacity) {
    return getPath(Dir::Kind::Cache, buffer, capacity);
}

char* Dir::getRuntime(char* buffer, size_t capacity) {
    return getPath(Dir::Kind::Runtime, buffer, capacity);
}

char* Dir::getLog(char* buffer, size_t capacity) {
    return getPath(Dir::Kind::Log, buffer, capacity);
}
}  // namespace os

namespace os {
Dir::Dir(Dir&& other) noexcept { *this = static_cast<Dir&&>(other); }
Dir& Dir::operator=(Dir&& other) noexcept {
    if (this == &other) return *this;
    if (service_ || descriptor_) {
        error_ = static_cast<int>(ErrorCode::Busy);
        return *this;
    }
    service_    = other.service_;
    descriptor_ = other.descriptor_;
    offset_     = other.offset_;
    scope_      = other.scope_;
    error_      = other.error_;
    Runtime::copyBytes(path_, other.path_, sizeof(path_));
    Runtime::copyBytes(application_, other.application_, sizeof(application_));
    ended_      = other.ended_;
    cacheIndex_ = other.cacheIndex_;
    cacheCount_ = other.cacheCount_;
    for (size_t i = 0; i < 14; ++i) cache_[i] = other.cache_[i];
    other.service_ = other.descriptor_ = other.offset_ = 0;
    other.path_[0]                                     = 0;
    other.cacheIndex_ = other.cacheCount_ = 0;
    other.ended_                          = false;
    other.error_                          = 0;
    return *this;
}
uint64_t Dir::service() {
    if (!service_) service_ = detail::RegistryClient().lookup(fsServiceName, 1000);
    return service_;
}
int Dir::open(const char* path) {
    if (descriptor_) return error_ = static_cast<int>(ErrorCode::Busy);
    char resolved[384];
    if (!Runtime::absolutePath(path, resolved, sizeof(resolved)))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    if (!service()) return error_ = static_cast<int>(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::DirOpen);
    request.length = 48;
    Runtime::copyString(reinterpret_cast<char*>(request.data), resolved, 384);
    const int status = callService(service_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 1) return error_ = static_cast<int>(ErrorCode::IoError);
    if (reply.data[0])
        return error_ = (static_cast<int64_t>(reply.data[0]) > 0
                             ? static_cast<int>(ErrorCode::IoError)
                             : static_cast<int>(reply.data[0]));
    if (reply.length < 2 || !reply.data[1])
        return error_ = static_cast<int>(ErrorCode::IoError);
    descriptor_ = reply.data[1];
    offset_     = 0;
    cacheIndex_ = cacheCount_ = 0;
    ended_                    = false;
    Runtime::copyString(path_, resolved, sizeof(path_));
    return error_ = 0;
}
int Dir::close() {
    if (descriptor_) {
        IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(FsRequest::DirClose);
        request.length   = 1;
        request.data[0]  = descriptor_;
        const int status = callService(service_, request, reply);
        if (status && status != static_cast<int>(ErrorCode::PeerDied) &&
            status != static_cast<int>(ErrorCode::EndpointClosed) &&
            status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
        if (!status &&
            (reply.length < 1 ||
             (reply.data[0] && static_cast<int64_t>(reply.data[0]) !=
                                   static_cast<int>(ErrorCode::InvalidHandle))))
            return error_ = reply.length
                                ? (static_cast<int64_t>(reply.data[0]) > 0
                                       ? static_cast<int>(ErrorCode::IoError)
                                       : static_cast<int>(reply.data[0]))
                                : static_cast<int>(ErrorCode::IoError);
        descriptor_ = offset_ = 0;
        path_[0]              = 0;
        cacheIndex_ = cacheCount_ = 0;
        ended_                    = false;
    }
    if (service_) {
        const int status = Ipc::Handle(service_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
        service_ = 0;
    }
    return error_ = 0;
}
int Dir::rewind() {
    if (!descriptor_)
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    offset_     = 0;
    cacheIndex_ = cacheCount_ = 0;
    ended_                    = false;
    return error_             = 0;
}
int Dir::create(const char* path, uint32_t mode) {
    if (descriptor_) return error_ = static_cast<int>(ErrorCode::Busy);
    char resolved[384];
    if ((mode & ~07777U) ||
        !Runtime::absolutePath(path, resolved, sizeof(resolved)))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    if (!service()) return error_ = static_cast<int>(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(FsRequest::DirCreate);
    request.length   = 49;
    request.data[48] = mode;
    Runtime::copyString(reinterpret_cast<char*>(request.data), resolved, 384);
    const int status = callService(service_, request, reply);
    return error_    = status         ? status
                       : reply.length ? (static_cast<int64_t>(reply.data[0]) > 0
                                             ? static_cast<int>(ErrorCode::IoError)
                                             : static_cast<int>(reply.data[0]))
                                      : static_cast<int>(ErrorCode::IoError);
}
int Dir::remove(const char* path) {
    char resolved[384];
    if (!Runtime::absolutePath(path, resolved, sizeof(resolved)))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    if (!service()) return error_ = static_cast<int>(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::DirRemove);
    request.length = 48;
    Runtime::copyString(reinterpret_cast<char*>(request.data), resolved, 384);
    const int status = callService(service_, request, reply);
    return error_    = status         ? status
                       : reply.length ? (static_cast<int64_t>(reply.data[0]) > 0
                                             ? static_cast<int>(ErrorCode::IoError)
                                             : static_cast<int>(reply.data[0]))
                                      : static_cast<int>(ErrorCode::IoError);
}
int Dir::rename(const char* existing, const char* target) {
    char from[384], to[384];
    if (!Runtime::absolutePath(existing, from, sizeof(from)) ||
        !Runtime::absolutePath(target, to, sizeof(to)))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    if (!service()) return error_ = static_cast<int>(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(FsRequest::DirRename);
    request.length = 96;
    Runtime::copyString(reinterpret_cast<char*>(request.data), from, 384);
    Runtime::copyString(reinterpret_cast<char*>(request.data) + 384, to, 384);
    const int status = callService(service_, request, reply);
    if (status) return error_ = status;
    if (!reply.length) return error_ = static_cast<int>(ErrorCode::IoError);
    if (reply.data[0])
        return error_ = (static_cast<int64_t>(reply.data[0]) > 0
                             ? static_cast<int>(ErrorCode::IoError)
                             : static_cast<int>(reply.data[0]));
    if (descriptor_ && Runtime::stringsEqual(path_, from))
        Runtime::copyString(path_, to, sizeof(path_));
    return error_ = 0;
}
int Dir::readBatch(Entry* entries, size_t capacity, size_t& count) {
    count = 0;
    if (!descriptor_)
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (!capacity) return error_ = 0;
    if (!entries) return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    if (cacheIndex_ == cacheCount_) {
        if (ended_) return error_ = 0;
        IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(FsRequest::DirRead);
        request.length   = 3;
        request.data[0]  = descriptor_;
        request.data[1]  = offset_;
        request.data[2]  = 14;
        const int status = callService(service_, request, reply);
        if (status) return error_ = status;
        if (reply.length < 1)
            return error_ = static_cast<int>(ErrorCode::IoError);
        if (reply.data[0])
            return error_ = (static_cast<int64_t>(reply.data[0]) > 0
                                 ? static_cast<int>(ErrorCode::IoError)
                                 : static_cast<int>(reply.data[0]));
        if (reply.length < 2 || reply.data[1] > 14 ||
            reply.length <
                2 + (reply.data[1] * sizeof(wire::DirectoryEntry) + 7) / 8)
            return error_ = static_cast<int>(ErrorCode::IoError);
        wire::DirectoryEntry records[14]{};
        Runtime::copyBytes(records, &reply.data[2],
                           reply.data[1] * sizeof(*records));
        uint64_t previous = offset_;
        for (size_t i = 0; i < reply.data[1]; ++i) {
            bool terminated = false;
            for (char c : records[i].name)
                if (!c) {
                    terminated = true;
                    break;
                }
            if (!terminated || records[i].type > 2 ||
                records[i].nextOffset <= previous)
                return error_ = static_cast<int>(ErrorCode::IoError);
            previous = records[i].nextOffset;
        }
        cacheIndex_ = 0;
        cacheCount_ = reply.data[1];
        ended_      = cacheCount_ == 0;
        for (size_t i = 0; i < cacheCount_; ++i) {
            cache_[i].type_       = static_cast<File::Type>(records[i].type);
            cache_[i].inode_      = records[i].inode;
            cache_[i].nextOffset_ = records[i].nextOffset;
            Runtime::copyBytes(cache_[i].name_, records[i].name, 256);
        }
    }
    while (count < capacity && cacheIndex_ < cacheCount_) {
        entries[count] = cache_[cacheIndex_++];
        offset_        = entries[count++].nextOffset_;
    }
    return error_ = 0;
}
int Dir::readNext(Entry& entry) {
    size_t count     = 0;
    const int status = readBatch(&entry, 1, count);
    return status ? status : static_cast<int>(count);
}
const char* Dir::getPath() const { return path_; }
bool Dir::getOpened() const { return descriptor_ != 0; }
uint64_t Dir::getOffset() const { return offset_; }
int Dir::getError() const { return error_; }
const char* Dir::Entry::getName() const { return name_; }
File::Type Dir::Entry::getType() const { return type_; }
uint64_t Dir::Entry::getInode() const { return inode_; }
uint64_t Dir::Entry::getNextOffset() const { return nextOffset_; }
}  // namespace os
