#include "sysfs.h"
#include "os/abi.h"

namespace {
struct Node { const char* path; uint32_t parent; bool directory; };
constexpr Node nodes[] = {
    {"/sys", 0, true}, {"/sys/kernel", 0, true}, {"/sys/cpu", 0, true},
    {"/sys/memory", 0, true}, {"/sys/kernel/ostype", 1, false},
    {"/sys/kernel/osrelease", 1, false}, {"/sys/kernel/hostname", 1, false},
    {"/sys/uptime", 0, false}, {"/sys/cpu/online", 2, false},
    {"/sys/memory/total", 3, false}, {"/sys/memory/free", 3, false},
    {"/sys/meminfo", 0, false}
};
class Text {
   public:
    void append(const char* value) { while (*value && used_ + 1 < sizeof(bytes_)) bytes_[used_++] = *value++; }
    void number(uint64_t value) {
        char digits[20]; unsigned count = 0;
        do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
        while (count && used_ + 1 < sizeof(bytes_)) bytes_[used_++] = digits[--count];
    }
    const char* data() const { return bytes_; }
    size_t size() const { return used_; }
   private:
    char bytes_[512]{};
    size_t used_ = 0;
};
bool contents(uint32_t index, Text& text) {
    if (index >= sizeof(nodes) / sizeof(nodes[0]) || nodes[index].directory) return false;
    os::SystemInfo info{};
    if (!os::getSystemInfo(info)) return false;
    switch (index) {
        case 4: text.append("osinfo"); break;
        case 5: text.append("0.2"); break;
        case 6: text.append("osinfo"); break;
        case 7: text.number(info.uptimeNanoseconds / 1000000000ULL); break;
        case 8: text.number(info.onlineCpus); break;
        case 9: text.number(info.totalMemoryBytes); break;
        case 10: text.number(info.freeMemoryBytes); break;
        case 11:
            text.append("MemTotal: "); text.number(info.totalMemoryBytes / 1024); text.append(" kB\nMemFree: ");
            text.number(info.freeMemoryBytes / 1024); text.append(" kB"); break;
        default: return false;
    }
    text.append("\n");
    return true;
}
}

namespace vfs {
bool SystemFiles::lookup(const char* path, FileStatus& status) {
    for (uint32_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
        if (!os::stringsEqual(path, nodes[i].path)) continue;
        Text text;
        if (!nodes[i].directory && !contents(i, text)) return false;
        status = {};
        status.inode = i + 1;
        status.mode = nodes[i].directory ? modeDirectory | 0555 : modeRegular | 0444;
        status.links = nodes[i].directory ? 2 : 1;
        status.size = text.size();
        return true;
    }
    return false;
}
int SystemFiles::directory(const char* path, uint32_t index, DirectoryEntry& entry) {
    FileStatus status{};
    if (!lookup(path, status) || (status.mode & 0xf000) != modeDirectory) return -1;
    for (uint32_t i = 1; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
        if (nodes[i].parent + 1 != status.inode) continue;
        if (index--) continue;
        entry = {};
        entry.inode = i + 1;
        entry.type = nodes[i].directory ? 2 : 1;
        const char* name = nodes[i].path;
        for (const char* p = name; *p; ++p) if (*p == '/') name = p + 1;
        os::copyString(entry.name, name, sizeof(entry.name));
        return 0;
    }
    return 1;
}
int64_t SystemFiles::read(uint32_t inode, uint64_t offset, void* buffer, size_t size) {
    Text text;
    if (!inode || !contents(inode - 1, text)) return -1;
    if (offset >= text.size()) return 0;
    if (size > text.size() - offset) size = text.size() - offset;
    os::copyBytes(buffer, text.data() + offset, size);
    return static_cast<int64_t>(size);
}
}
