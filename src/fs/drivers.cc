#include <new>

#include "ext3.h"
#include "ext4.h"
#include "fat32.h"
#include "iso.h"
#include "os/abi.h"
#include "vfs.h"

namespace vfs {
class DriverTypes {
 public:

static bool matches(const char* value, const char* expected) {
    if (value == nullptr || expected == nullptr) return false;
    while (*value != '\0' && *value == *expected) {
        ++value;
        ++expected;
    }
    return *value == '\0' && *expected == '\0';
}



template <typename Type>
static FileSystem* create() {
    void* memory = os::Memory::allocateMemory(sizeof(Type));
    return memory == nullptr ? nullptr : new (memory) Type{};
}

};

FileSystem* FileSystemFactory::createFileSystem(const char* type) {
    if (DriverTypes::matches(type, "ext3")) return DriverTypes::create<fs::ext3::Filesystem>();
    if (DriverTypes::matches(type, "ext4")) return DriverTypes::create<fs::ext4::Filesystem>();
    if (DriverTypes::matches(type, "fat32")) return DriverTypes::create<Fat32FileSystem>();
    if (DriverTypes::matches(type, "iso9660")) return DriverTypes::create<Iso9660FileSystem>();
    return nullptr;
}

void FileSystemFactory::destroyFileSystem(FileSystem* filesystem, const char* type) {
    if (filesystem == nullptr) return;
    if (DriverTypes::matches(type, "ext3"))
        static_cast<fs::ext3::Filesystem*>(filesystem)->~Filesystem();
    else if (DriverTypes::matches(type, "ext4"))
        static_cast<fs::ext4::Filesystem*>(filesystem)->~Filesystem();
    else if (DriverTypes::matches(type, "fat32"))
        static_cast<Fat32FileSystem*>(filesystem)->~Fat32FileSystem();
    else if (DriverTypes::matches(type, "iso9660"))
        static_cast<Iso9660FileSystem*>(filesystem)->~Iso9660FileSystem();
    os::Memory::freeMemory(filesystem);
}

}  // namespace vfs
