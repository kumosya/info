#include <new>

#include "service/common/queries.h"
#include "service/common/runtime.h"
#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/fat/filesystem.h"
#include "service/fs/iso/filesystem.h"
#include "service/fs/vfs.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

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
        void* memory = os::Runtime::allocateMemory(sizeof(Type));
        return memory == nullptr ? nullptr : new (memory) Type{};
    }
};

FileSystem* FileSystemFactory::createFileSystem(const char* type) {
    if (DriverTypes::matches(type, "ext3"))
        return DriverTypes::create<fs::ext3::Filesystem>();
    if (DriverTypes::matches(type, "ext4"))
        return DriverTypes::create<fs::ext4::Filesystem>();
    if (DriverTypes::matches(type, "fat32"))
        return DriverTypes::create<Fat32FileSystem>();
    if (DriverTypes::matches(type, "iso9660"))
        return DriverTypes::create<Iso9660FileSystem>();
    return nullptr;
}

void FileSystemFactory::destroyFileSystem(FileSystem* filesystem,
                                          const char* type) {
    if (filesystem == nullptr) return;
    if (DriverTypes::matches(type, "ext3"))
        static_cast<fs::ext3::Filesystem*>(filesystem)->~Filesystem();
    else if (DriverTypes::matches(type, "ext4"))
        static_cast<fs::ext4::Filesystem*>(filesystem)->~Filesystem();
    else if (DriverTypes::matches(type, "fat32"))
        static_cast<Fat32FileSystem*>(filesystem)->~Fat32FileSystem();
    else if (DriverTypes::matches(type, "iso9660"))
        static_cast<Iso9660FileSystem*>(filesystem)->~Iso9660FileSystem();
    os::Runtime::freeMemory(filesystem);
}

}  // namespace vfs
