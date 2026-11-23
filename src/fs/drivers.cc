#include <os/detail/client.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include <new>

#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/fat/filesystem.h"
#include "service/fs/iso/filesystem.h"
#include "service/fs/vfs.h"

namespace fs {

    namespace driver_types {

        static bool matches(const char* value, const char* expected) {
            if (value == nullptr || expected == nullptr) {
                return false;
            }

            while (*value != '\0' && *value == *expected) {
                ++value;
                ++expected;
            }

            return *value == '\0' && *expected == '\0';
        }

        template <typename Type>
        static FileSystem* create() {
            void* memory = os::Runtime::current().allocateMemory(sizeof(Type));
            return memory == nullptr ? nullptr : new (memory) Type{};
        }
    }  // namespace driver_types

    FileSystem* FileSystemFactory::createFileSystem() {
        const char* type = type_;
        if (driver_types::matches(type, "ext3")) {
            return driver_types::create<fs::ext3::Filesystem>();
        }

        if (driver_types::matches(type, "ext4")) {
            return driver_types::create<fs::ext4::Filesystem>();
        }

        if (driver_types::matches(type, "fat32")) {
            return driver_types::create<fs::fat::Filesystem>();
        }

        if (driver_types::matches(type, "iso9660")) {
            return driver_types::create<fs::iso::Filesystem>();
        }

        return nullptr;
    }

    void FileSystemFactory::destroyFileSystem(FileSystem* filesystem) {
        const char* type = type_;
        if (filesystem == nullptr) {
            return;
        }

        if (driver_types::matches(type, "ext3")) {
            static_cast<fs::ext3::Filesystem*>(filesystem)->~Filesystem();
        } else if (driver_types::matches(type, "ext4")) {
            static_cast<fs::ext4::Filesystem*>(filesystem)->~Filesystem();
        } else if (driver_types::matches(type, "fat32")) {
            static_cast<fs::fat::Filesystem*>(filesystem)->~Filesystem();
        } else if (driver_types::matches(type, "iso9660")) {
            static_cast<fs::iso::Filesystem*>(filesystem)->~Filesystem();
        }

        os::Runtime::current().freeMemory(filesystem);
    }

}  // namespace fs
