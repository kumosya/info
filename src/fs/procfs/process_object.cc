#include "service/common/runtime.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/fs/server.h"
#include "service/fs/procfs/process_object.h"
namespace vfs::server {
uint64_t FsServer::openProcessObject(uint64_t caller, uint64_t object, int flags, const char* path) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) return 0;
    OpenFileDescription* source = nullptr;
    {
        std::lock_guard<std::mutex> guard(descriptorLock_);
        auto* descriptor = getDescriptorRecord(caller, object);
        if (!descriptor || descriptor->mappingClaimed || (descriptor->flags & O_ACCMODE) == O_WRONLY ||
            descriptor->description->backend != DescriptorBackend::Filesystem ||
            (descriptor->description->mode & 0170000) != 0100000) return 0;
        source = descriptor->description; ++source->referenceCount;
    }
    const uint64_t result = [&]() -> uint64_t {
        std::lock_guard<std::mutex> description(descriptionMutex(source));
        std::lock_guard<std::mutex> filesystem(fileSystemLock(source->mountIndex));
        std::lock_guard<std::mutex> descriptors(descriptorLock_);
        uint32_t fd = descriptorCapacity, slot = descriptorCapacity;
        for (uint32_t i = 0; i < descriptorCapacity; ++i) {
            if (fd == descriptorCapacity && !descriptors_[i].description) fd = i;
            if (slot == descriptorCapacity && openDescriptions_[i].backend == DescriptorBackend::None) slot = i;
        }
        if (fd == descriptorCapacity || slot == descriptorCapacity || !source->filesystem->retainOpenFile(source->inode)) return 0;
        auto& copy = openDescriptions_[slot]; copy = *source;
        copy.offset = 0; copy.referenceCount = 1;
        os::Runtime::copyString(copy.path, path, sizeof(copy.path));
        descriptors_[fd] = {caller, &copy, flags}; return fd + 1;
    }();
    releaseDescription(source); return result;
}
int FsServer::processObjectDirectory(const char* path, uint64_t index, uint64_t caller, vfs::DirectoryEntry& entry) {
    uint64_t object = 0;
    const auto kind = ProcessObjectPath::parse(path, caller, object);
    if (kind == ProcessObjectPath::Other) return -2;
    if (kind != ProcessObjectPath::Directory) return -1;
    std::lock_guard<std::mutex> guard(descriptorLock_);
    uint64_t position = 0;
    for (uint32_t i = 0; i < descriptorCapacity; ++i) {
        const auto& record = descriptors_[i];
        if (!record.description || record.ownerPid != caller || record.mappingClaimed ||
            record.description->backend != DescriptorBackend::Filesystem ||
            (record.description->mode & 0170000) != 0100000) continue;
        if (position++ != index) continue;
        entry = {}; entry.type = 1; entry.inode = record.description->inode;
        char digits[20]; unsigned count = 0, value = i + 1;
        do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
        for (unsigned j = 0; j < count; ++j) entry.name[j] = digits[count - 1 - j];
        return 0;
    }
    return 1;
}
}
