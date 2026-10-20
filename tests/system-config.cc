#include "../src/fs/sysfs.h"
#include "os/abi.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

namespace os {
void Runtime::copyBytes(void* to, const void* from, size_t size) { std::memcpy(to, from, size); }
void Runtime::copyString(char* to, const char* from, size_t capacity) {
    if (capacity) { std::strncpy(to, from, capacity - 1); to[capacity - 1] = 0; }
}
bool Runtime::stringsEqual(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
bool System::getSystemInfo(SystemInfo&) { return false; }
void System::log(const char*) {}
}

class ConfigurationFs final : public vfs::FileSystem {
 public:
    std::map<std::string, std::string> files{{"/lib/version.1", "0.1.1-test\r\n"}, {"/etc/hostname", "configured-host\n"}};
    std::string selected;
    bool mount(vfs::SectorDevice&) override { return false; }
    bool lookup(const char* path, vfs::FileStatus& status) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        selected = path; status = {}; status.mode = vfs::modeRegular; status.size = it->second.size(); return true;
    }
    int64_t read(const vfs::FileStatus&, uint64_t offset, void* buffer, size_t size) override {
        const auto& value = files.at(selected);
        if (offset >= value.size()) return 0;
        if (size > value.size() - offset) size = value.size() - offset;
        std::memcpy(buffer, value.data() + offset, size); return size;
    }
    int readDirectory(const char*, uint32_t, vfs::DirectoryEntry&) override { return -1; }
    bool readOnly() const override { return true; }
};

int main() {
    ConfigurationFs root;
    assert(vfs::SystemFiles::initialize(root));
    vfs::FileStatus status{};
    // Configuration nodes remain readable when live system statistics fail.
    assert(vfs::SystemFiles::lookup("/sys/kernel/osrelease", status));
    assert(status.size == 11);
    char buffer[80]{};
    assert(vfs::SystemFiles::read(status.inode, 0, buffer, sizeof(buffer)) == 11);
    assert(std::string(buffer, 11) == "0.1.1-test\n");
    assert(vfs::SystemFiles::read(status.inode, 2, buffer, 3) == 3);
    assert(std::string(buffer, 3) == "1.1");
    assert(vfs::SystemFiles::read(status.inode, 11, buffer, sizeof(buffer)) == 0);
    assert(vfs::SystemFiles::read(status.inode, 0, buffer, 0) == 0);
    assert(!vfs::SystemFiles::lookup("/sys/uptime", status));

    for (const auto& invalid : {std::string(), std::string("bad\nsecond\n"), std::string("bad host\n"), std::string(64, 'x'), std::string("bad\0host", 8)}) {
        root.files["/etc/hostname"] = invalid;
        assert(!vfs::SystemFiles::initialize(root));
    }
    root.files.erase("/etc/hostname");
    assert(!vfs::SystemFiles::initialize(root));
    root.files["/etc/hostname"] = std::string(63, 'h');
    root.files["/lib/version.1"] = std::string(63, 'v') + "\r\n";
    assert(vfs::SystemFiles::initialize(root));
    assert(vfs::SystemFiles::lookup("/sys/kernel/hostname", status) && status.size == 64);
    root.files["/lib/version.1"] = "bad/version\n";
    assert(!vfs::SystemFiles::initialize(root));
    std::cout << "System configuration: source files, CRLF, bounded reads, invalid/missing data and statistics independence passed\n";
}
