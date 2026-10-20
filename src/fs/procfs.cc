#include "procfs.h"
#include "os/task.h"
#include "os/core_service.h"

namespace {
class Text {
    char* data_; size_t capacity_, used_ = 0;
 public:
    Text(char* data, size_t capacity) : data_(data), capacity_(capacity) {}
    bool full = false;
    void add(char c) { if (used_ < capacity_) data_[used_++] = c; else full = true; }
    void add(const char* text) { while (*text) add(*text++); }
    void name(const char* text) { while (*text) { unsigned char c = *text++; add(c >= 32 && c != 127 ? static_cast<char>(c) : '?'); } }
    void line(const char* key, uint64_t value) { add(key); number(value); add('\n'); }
    void number(uint64_t value) { char digits[20]; unsigned count = 0; do { digits[count++] = '0' + value % 10; value /= 10; } while (value); while (count) add(digits[--count]); }
    size_t size() const { return used_; }
};
class ProcessPath {
 public:
static bool equal(const char* a, const char* b) { return os::Runtime::stringsEqual(a, b); }
static bool parse(const char* path, uint64_t caller, uint64_t& pid, const char*& file) {
    if (!path || (!equal(path, "/proc") && !equal(path, "/proc/") &&
        !(path[0] == '/' && path[1] == 'p' && path[2] == 'r' && path[3] == 'o' && path[4] == 'c' && path[5] == '/'))) return false;
    pid = 0; file = "";
    if (equal(path, "/proc") || equal(path, "/proc/")) return true;
    const char* cursor = path + 6;
    if (cursor[0] == 's' && cursor[1] == 'e' && cursor[2] == 'l' && cursor[3] == 'f' && (!cursor[4] || cursor[4] == '/')) {
        pid = caller; cursor += 4;
    } else {
        if (*cursor < '1' || *cursor > '9') return false;
        while (*cursor >= '0' && *cursor <= '9') {
            const unsigned digit = *cursor++ - '0';
            if (pid > (UINT64_MAX - digit) / 10) return false;
            pid = pid * 10 + digit;
        }
    }
    if (!pid || (*cursor && *cursor != '/')) return false;
    file = *cursor ? cursor + 1 : cursor;
    return true;
}
inline static const char* names[] = {"stat", "status", "cmdline", "comm"};
static const char* basename(const char* path) { const char* name = path; for (; *path; ++path) if (*path == '/') name = path + 1; return name; }
static int next(uint64_t cursor, os::TaskInfo& info) {
    os::IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::TaskOperation::Enumerate);
    request.length = 4; request.data[0] = cursor; request.data[1] = 1;
    const auto result = os::CoreServiceClient::result(os::CoreServiceKind::Task, request, reply);
    if (!result) os::Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}
};
}
namespace vfs {
bool ProcessFiles::lookup(const char* path, uint64_t caller, FileStatus& status) {
    uint64_t pid; const char* file;
    if (!ProcessPath::parse(path, caller, pid, file)) return false;
    os::TaskInfo info{};
    if (pid && os::Task::query(pid, info)) return false;
    status = {}; status.links = 1; status.mode = modeDirectory | 0555;
    status.inode = pid ? static_cast<uint32_t>(pid * 8 + 8) : 1;
    if (!*file) { status.links = 2; return true; }
    for (unsigned i = 0; i < 4; ++i) if (ProcessPath::equal(file, ProcessPath::names[i])) {
        status.inode += i + 1; status.mode = modeRegular | (i == 2 ? 0400 : 0444);
        status.uid = info.effectiveUid; status.gid = info.effectiveGid;
        return true;
    }
    return false;
}
int ProcessFiles::directory(const char* path, uint64_t cursor, uint64_t caller, DirectoryEntry& entry) {
    uint64_t pid; const char* file;
    if (!ProcessPath::parse(path, caller, pid, file) || *file) return -1;
    entry = {};
    if (!pid) {
        entry.type = 2;
        if (!cursor) { os::Runtime::copyString(entry.name, "self", sizeof(entry.name)); entry.inode = static_cast<uint32_t>(caller * 8 + 8); return 0; }
        os::TaskInfo info{};
        if (ProcessPath::next(cursor, info)) return 1;
        Text text(entry.name, sizeof(entry.name) - 1); text.number(info.id);
        entry.inode = static_cast<uint32_t>(info.id * 8 + 8);
        return 0;
    }
    os::TaskInfo info{};
    if (os::Task::query(pid, info)) return -1;
    if (cursor >= 4) return 1;
    entry.type = 1; entry.inode = static_cast<uint32_t>(pid * 8 + 9 + cursor);
    os::Runtime::copyString(entry.name, ProcessPath::names[cursor], sizeof(entry.name));
    return 0;
}
uint64_t ProcessFiles::nextCursor(const char* path, const DirectoryEntry& entry, uint64_t previous) {
    if (!ProcessPath::equal(path, "/proc") && !ProcessPath::equal(path, "/proc/")) return previous + 1;
    if (ProcessPath::equal(entry.name, "self")) return 1;
    uint64_t pid = 0;
    for (const char* p = entry.name; *p; ++p) pid = pid * 10 + *p - '0';
    return pid + 1;
}
int64_t ProcessFiles::snapshot(const char* path, uint64_t caller, char* bytes, size_t capacity) {
    uint64_t pid; const char* file; os::TaskInfo info{}, reader{};
    if (!ProcessPath::parse(path, caller, pid, file) || !pid || os::Task::query(pid, info)) return -1;
    Text text(bytes, capacity);
    if (ProcessPath::equal(file, "cmdline")) {
        if (os::Task::query(caller, reader) || (reader.effectiveUid && reader.effectiveUid != info.effectiveUid)) return -1;
        if (info.commandLineSize > sizeof(info.commandLine) || info.commandLineSize > capacity) return -1;
        os::Runtime::copyBytes(bytes, info.commandLine, info.commandLineSize);
        return info.commandLineSize;
    }
    if (ProcessPath::equal(file, "comm")) { text.name(ProcessPath::basename(info.name)); text.add('\n'); }
    else if (ProcessPath::equal(file, "status")) {
        text.add("Name:\t"); text.name(ProcessPath::basename(info.name)); text.add('\n');
        text.line("Pid:\t", info.id); text.line("PPid:\t", info.parentId);
        text.line("Uid:\t", info.uid); text.line("Euid:\t", info.effectiveUid);
        text.line("Gid:\t", info.gid); text.line("Egid:\t", info.effectiveGid);
        text.line("State:\t", info.state); text.line("Pgid:\t", info.taskGroupId); text.line("Sid:\t", info.sessionId);
        text.line("TtyKind:\t", info.terminalKind); text.line("TtyIndex:\t", info.terminalIndex);
        text.line("ForegroundPgid:\t", info.terminalForegroundGroup);
        text.line("StartNs:\t", info.startedNanoseconds); text.line("CpuNs:\t", info.cpuNanoseconds);
        text.line("ServiceRole:\t", info.serviceRole);
        text.line("Service:\t", info.managedService);
    } else if (ProcessPath::equal(file, "stat")) {
        text.number(info.id); text.add(" ("); text.name(ProcessPath::basename(info.name)); text.add(") ");
        text.add(info.state == 2 ? 'Z' : info.state == 3 ? 'T' : 'R');
        const uint64_t values[] = {info.parentId, info.taskGroupId, info.sessionId,
            info.terminalKind ? (static_cast<uint64_t>(info.terminalKind) << 32) | info.terminalIndex : 0,
            info.terminalForegroundGroup, 0, 0, 0, 0, 0, info.cpuNanoseconds / 10000000, 0, 0, 0, 0, 0, 1, 0,
            info.startedNanoseconds / 10000000};
        for (auto value : values) { text.add(' '); text.number(value); }
        text.add('\n');
    } else return -1;
    return text.full ? -1 : static_cast<int64_t>(text.size());
}
}
