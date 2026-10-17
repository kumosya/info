#include <new>

#include "ext3.h"
#include "ext4.h"
#include "fat32.h"
#include "iso.h"
#include "os/abi.h"
#include "vfs.h"

namespace vfs {
namespace {

bool matches(const char* value, const char* expected) {
    if (value == nullptr || expected == nullptr) return false;
    while (*value != '\0' && *value == *expected) {
        ++value;
        ++expected;
    }
    return *value == '\0' && *expected == '\0';
}

}  // namespace

template <typename Type>
FileSystem* create() {
    void* memory = os::allocateMemory(sizeof(Type));
    return memory == nullptr ? nullptr : new (memory) Type{};
}

FileSystem* createFileSystem(const char* type) {
    if (matches(type, "ext3")) return create<fs::ext3::Filesystem>();
    if (matches(type, "ext4")) return create<fs::ext4::Filesystem>();
    if (matches(type, "fat32")) return create<Fat32FileSystem>();
    if (matches(type, "iso9660")) return create<Iso9660FileSystem>();
    return nullptr;
}

void destroyFileSystem(FileSystem* filesystem, const char* type) {
    if (filesystem == nullptr) return;
    if (matches(type, "ext3"))
        static_cast<fs::ext3::Filesystem*>(filesystem)->~Filesystem();
    else if (matches(type, "ext4"))
        static_cast<fs::ext4::Filesystem*>(filesystem)->~Filesystem();
    else if (matches(type, "fat32"))
        static_cast<Fat32FileSystem*>(filesystem)->~Fat32FileSystem();
    else if (matches(type, "iso9660"))
        static_cast<Iso9660FileSystem*>(filesystem)->~Iso9660FileSystem();
    os::freeMemory(filesystem);
}

}  // namespace vfs
