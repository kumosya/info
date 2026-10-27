#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/server.h"
#include "service/fs/sysfs/filesystem.h"

namespace vfs::server {
namespace {
class MountPolicy {
   public:
    static bool typeMatches(const MountSpec& spec, const DeviceRecord& device);
    static bool bounded(const char* text, size_t length);
    static void failure(const char* text, const char* source);
};

bool MountPolicy::typeMatches(const MountSpec& spec,
                              const DeviceRecord& device) {
    return os::Runtime::stringsEqual(spec.type, "auto") ||
           os::Runtime::stringsEqual(spec.type,
                                     FormatProbe().formatName(device.format));
}
bool MountPolicy::bounded(const char* text, size_t length) {
    for (size_t i = 0; i < length; ++i)
        if (!text[i]) return true;
    return false;
}
void MountPolicy::failure(const char* text, const char* source) {
    os::Sys().log("[FS] ");
    os::Sys().log(text);
    os::Sys().log(": ");
    os::Sys().log(source);
    os::Sys().log("\n");
}
}  // namespace

bool FsServer::init(uint64_t storageEndpoint, uint64_t deviceEndpoint) {
    deviceEndpoint_ = deviceEndpoint;
    os::IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::StorageRequest::GetCount);
    if (!storageEndpoint || !deviceEndpoint ||
        os::callService(storageEndpoint, request, reply) != 0 ||
        reply.data[0] != 0 || !reply.data[1] ||
        reply.data[1] > os::maximumStorageDevices)
        return false;
    const uint32_t count = static_cast<uint32_t>(reply.data[1]);
    for (uint32_t index = 0; index < count; ++index) {
        if (!devices_[index].init(storageEndpoint, index) ||
            !catalog_.add(devices_[index].identity(), devices_[index]))
            return false;
        const DeviceRecord& record = *catalog_.get(index);
        os::Sys().log("[FS] detected /dev/");
        os::Sys().log(record.identity.name);
        os::Sys().log(" type=");
        os::Sys().log(FormatProbe().formatName(record.format));
        os::Sys().log(" label=");
        os::Sys().log(record.label);
        os::Sys().log(" UUID=");
        os::Sys().log(record.uuid);
        os::Sys().log(" PARTUUID=");
        os::Sys().log(record.identity.partitionUuid);
        os::Sys().log("\n");
    }

    // /etc/fstab lives on the root volume: a candidate must select itself.
    // Never choose the first readable partition when multiple roots match.
    uint32_t selected = os::maximumStorageDevices;
    for (uint32_t index = 0; index < count; ++index) {
        const DeviceRecord& record = *catalog_.get(index);
        if (record.format == Format::Unknown || record.format == Format::ExFat)
            continue;
        const char* type           = FormatProbe().formatName(record.format);
        vfs::FileSystem* candidate = FileSystemFactory::createFileSystem(type);
        if (!candidate) return false;
        vfs::FileStatus status{};
        char text[sizeof(fstabText_)]{};
        bool valid = candidate->mount(devices_[index]) &&
                     candidate->lookup("/etc/fstab", status) &&
                     (status.mode & 0xf000U) == vfs::modeRegular &&
                     status.size && status.size < sizeof(text) &&
                     candidate->read(status, 0, text, status.size) ==
                         static_cast<int64_t>(status.size);
        bool root = false;
        if (valid) {
            for (const char* cursor = text; *cursor;) {
                MountSpec spec{};
                bool present = false;
                if (!Fstab::next(cursor, spec, present)) {
                    valid = false;
                    break;
                }
                if (!present || !os::Runtime::stringsEqual(spec.target, "/"))
                    continue;
                if (root) {
                    valid = false;
                    break;
                }
                root               = true;
                const int resolved = catalog_.resolve(spec.source);
                if (resolved == DeviceCatalog::ambiguous)
                    MountPolicy::failure("ambiguous root identifier",
                                         spec.source);
                if (resolved != static_cast<int>(index) ||
                    !MountPolicy::typeMatches(spec, record) ||
                    !spec.automatic || !spec.required)
                    valid = false;
            }
        }
        if (!valid || !root) {
            FileSystemFactory::destroyFileSystem(candidate, type);
            continue;
        }
        if (selected != os::maximumStorageDevices) {
            MountPolicy::failure("multiple root filesystems match their fstab",
                                 "/");
            FileSystemFactory::destroyFileSystem(candidate, type);
            return false;
        }
        selected        = index;
        fileSystems_[0] = candidate;
        os::Runtime::copyString(fstabText_, text, sizeof(fstabText_));
    }
    if (selected == os::maximumStorageDevices) {
        MountPolicy::failure("no uniquely identified root filesystem",
                             "/etc/fstab");
        return false;
    }
    mountTable_[0].deviceIndex = selected;
    if (!loadMountTable()) return false;
    if (!SystemFiles::init(*fileSystems_[0])) return false;
    mounted_[0] = true;
    os::Sys().log("[FS] root resolved to /dev/");
    os::Sys().log(catalog_.get(selected)->identity.name);
    os::Sys().log("\n");
    for (uint32_t index = 1; index < mountCapacity; ++index) {
        MountEntry& entry = mountTable_[index];
        if (!entry.configured || !entry.spec.automatic) continue;
        const PathBackend parent  = pathBackend(entry.spec.target);
        vfs::FileSystem* parentFs = fileSystem(parent);
        vfs::FileStatus target{};
        const bool exists =
            parentFs &&
            parentFs->lookup(relativePath(entry.spec.target, parent), target) &&
            (target.mode & 0xf000U) == vfs::modeDirectory;
        if (!exists || !attachMount(index)) {
            MountPolicy::failure("mount failed", entry.spec.source);
            if (entry.spec.required) return false;
        }
    }
    return true;
}

bool FsServer::loadMountTable() {
    uint32_t next = 1;
    for (const char* cursor = fstabText_; *cursor;) {
        MountSpec spec{};
        bool present = false;
        if (!Fstab::next(cursor, spec, present)) return false;
        if (!present) continue;
        if (PathText::startsWithPath(spec.target, "/dev")) return false;
        const uint32_t index =
            os::Runtime::stringsEqual(spec.target, "/") ? 0 : next++;
        if (index >= mountCapacity) return false;
        for (const MountEntry& prior : mountTable_)
            if (prior.configured &&
                os::Runtime::stringsEqual(prior.spec.target, spec.target))
                return false;
        MountEntry& entry = mountTable_[index];
        entry.spec        = spec;
        entry.configured  = true;
        if (index == 0) {
            const DeviceRecord& root = *catalog_.get(entry.deviceIndex);
            if (catalog_.resolve(spec.source) !=
                    static_cast<int>(entry.deviceIndex) ||
                !MountPolicy::typeMatches(spec, root))
                return false;
        }
    }
    return mountTable_[0].configured;
}

bool FsServer::attachMount(uint32_t index) {
    MountEntry& entry = mountTable_[index];
    const int device  = catalog_.resolve(entry.spec.source);
    if (device < 0) {
        MountPolicy::failure(device == DeviceCatalog::ambiguous
                                 ? "ambiguous device identifier"
                                 : "device not found",
                             entry.spec.source);
        return false;
    }
    const DeviceRecord& record = *catalog_.get(static_cast<uint32_t>(device));
    if (!MountPolicy::typeMatches(entry.spec, record)) return false;
    for (uint32_t prior = 0; prior < mountCapacity; ++prior)
        if (mounted_[prior] &&
            mountTable_[prior].deviceIndex == static_cast<uint32_t>(device))
            return false;
    // A configured mount keeps its instance and identity across
    // unmount/remount.
    if (entry.deviceIndex < catalog_.count() &&
        entry.deviceIndex != static_cast<uint32_t>(device))
        return false;
    entry.deviceIndex = static_cast<uint32_t>(device);
    const char* type  = FormatProbe().formatName(record.format);
    if (!fileSystems_[index])
        fileSystems_[index] = FileSystemFactory::createFileSystem(type);
    if (!fileSystems_[index] || !fileSystems_[index]->mount(devices_[device]))
        return false;
    if (fileSystems_[index]->readOnly()) entry.spec.readOnly = true;
    mounted_[index] = true;
    os::Sys().log("[FS] mounted /dev/");
    os::Sys().log(record.identity.name);
    os::Sys().log(" on ");
    os::Sys().log(entry.spec.target);
    os::Sys().log("\n");
    return true;
}

const MountEntry* FsServer::mountEntry(PathBackend backend) const {
    const uint32_t index = static_cast<uint32_t>(backend);
    return index < mountCapacity && mountTable_[index].configured
               ? &mountTable_[index]
               : nullptr;
}

bool FsServer::mount(const os::IpcMessage& request) {
    const char* source = reinterpret_cast<const char*>(request.data);
    const char* target = source + 64;
    const char* type   = source + 128;
    if (!MountPolicy::bounded(source, 64) ||
        !MountPolicy::bounded(target, 64) || !MountPolicy::bounded(type, 16) ||
        target[0] != '/' || PathText::startsWithPath(target, "/dev") ||
        os::Runtime::stringsEqual(target, "/"))
        return false;
    char targetBuffer[128]{};
    os::Runtime::copyString(targetBuffer, target, sizeof(targetBuffer));
    if (!Fstab::canonicalTarget(targetBuffer) ||
        PathText::startsWithPath(targetBuffer, "/dev") ||
        os::Runtime::stringsEqual(targetBuffer, "/"))
        return false;
    target = targetBuffer;
    std::lock_guard<std::mutex> operation(mountOperationLock_);
    uint32_t slot = mountCapacity;
    for (uint32_t index = 1; index < mountCapacity; ++index) {
        if (mountTable_[index].configured &&
            os::Runtime::stringsEqual(mountTable_[index].spec.target, target)) {
            slot = index;
            break;
        }
        if (!mountTable_[index].configured && slot == mountCapacity)
            slot = index;
    }
    if (slot == mountCapacity) return false;
    const int device = catalog_.resolve(source);
    if (device < 0) return false;
    MountSpec spec{};
    os::Runtime::copyString(spec.source, source, sizeof(spec.source));
    os::Runtime::copyString(spec.target, target, sizeof(spec.target));
    os::Runtime::copyString(spec.type, type, sizeof(spec.type));
    os::Runtime::copyString(spec.options, "defaults", sizeof(spec.options));
    if (!MountPolicy::typeMatches(spec, *catalog_.get(device))) return false;
    MountEntry& entry = mountTable_[slot];
    const bool fresh  = !entry.configured;
    if (entry.configured) {
        if (catalog_.resolve(entry.spec.source) != device) return false;
        if (mounted_[slot]) return true;
    } else {
        const PathBackend parent    = pathBackend(target);
        vfs::FileSystem* filesystem = fileSystem(parent);
        vfs::FileStatus status{};
        if (!filesystem) return false;
        std::lock_guard<std::mutex> guard(fileSystemLock(parent));
        if (!filesystem->lookup(relativePath(target, parent), status) ||
            (status.mode & 0xf000U) != vfs::modeDirectory)
            return false;
    }
    std::lock_guard<std::mutex> volume(fileSystemLocks_[slot]);
    std::lock_guard<std::mutex> state(mountStateLock_);
    if (fresh) {
        entry.spec       = spec;
        entry.configured = true;
    }
    const bool attached = attachMount(slot);
    if (!attached && fresh) {
        const char* actualType =
            FormatProbe().formatName(catalog_.get(device)->format);
        FileSystemFactory::destroyFileSystem(fileSystems_[slot], actualType);
        fileSystems_[slot] = nullptr;
        entry              = {};
    }
    return attached;
}

bool FsServer::unmount(const os::IpcMessage& request) {
    const char* target = reinterpret_cast<const char*>(request.data);
    if (!MountPolicy::bounded(target, 64)) return false;
    char normalized[128]{};
    os::Runtime::copyString(normalized, target, sizeof(normalized));
    if (!Fstab::canonicalTarget(normalized)) return false;
    target = normalized;
    std::lock_guard<std::mutex> operation(mountOperationLock_);
    uint32_t slot = mountCapacity;
    for (uint32_t index = 1; index < mountCapacity; ++index)
        if (mountTable_[index].configured && mounted_[index] &&
            os::Runtime::stringsEqual(mountTable_[index].spec.target, target)) {
            slot = index;
            break;
        }
    if (slot == mountCapacity) return false;
    std::lock_guard<std::mutex> volume(fileSystemLocks_[slot]);
    std::lock_guard<std::mutex> state(mountStateLock_);
    for (uint32_t index = 1; index < mountCapacity; ++index)
        if (index != slot && mounted_[index] &&
            PathText::startsWithPath(mountTable_[index].spec.target, target))
            return false;
    {
        std::lock_guard<std::mutex> descriptors(descriptorLock_);
        for (const OpenFileDescription& entry : openDescriptions_)
            if (entry.filesystem &&
                entry.mountIndex == static_cast<PathBackend>(slot))
                return false;
    }
    if (!fileSystems_[slot]->sync()) return false;
    mounted_[slot] = false;
    return true;
}

void FsServer::listMount(uint32_t requested, os::IpcMessage& reply) const {
    std::lock_guard<std::mutex> state(mountStateLock_);
    uint32_t current = 0;
    for (uint32_t index = 0; index < mountCapacity; ++index) {
        if (!mounted_[index] || current++ != requested) continue;
        const MountEntry& entry    = mountTable_[index];
        const DeviceRecord& device = *catalog_.get(entry.deviceIndex);
        char path[64]              = "/dev/";
        os::Runtime::copyString(path + 5, device.identity.name,
                                sizeof(path) - 5);
        reply.data[0] = 0;
        os::Runtime::copyString(reinterpret_cast<char*>(&reply.data[1]), path,
                                64);
        os::Runtime::copyString(reinterpret_cast<char*>(&reply.data[9]),
                                entry.spec.target, 64);
        os::Runtime::copyString(reinterpret_cast<char*>(&reply.data[17]),
                                FormatProbe().formatName(device.format), 16);
        reply.data[19] = readOnly(static_cast<PathBackend>(index));
        reply.length   = 20;
        return;
    }
    reply.data[0] = 1;
}

PathBackend FsServer::pathBackend(const char* path) const {
    if (!path || path[0] != '/') return PathBackend::Invalid;
    if (PathText::startsWithPath(path, "/sys") ||
        PathText::startsWithPath(path, "/proc"))
        return PathBackend::Invalid;
    std::lock_guard<std::mutex> guard(mountStateLock_);
    PathBackend result = PathBackend::Root;
    size_t length      = 0;
    for (uint32_t index = 1; index < mountCapacity; ++index) {
        const MountEntry& entry = mountTable_[index];
        if (entry.configured &&
            PathText::startsWithPath(path, entry.spec.target)) {
            const size_t size = os::Runtime::stringLength(entry.spec.target);
            if (size > length) {
                // An unmounted mountpoint exposes its directory on the parent
                // FS.
                if (mounted_[index]) {
                    result = static_cast<PathBackend>(index);
                    length = size;
                }
            }
        }
    }
    return mounted_[0] ? result : PathBackend::Invalid;
}
const char* FsServer::relativePath(const char* path,
                                   PathBackend backend) const {
    if (backend == PathBackend::Root) return path;
    const MountEntry* entry = mountEntry(backend);
    if (!entry || !PathText::startsWithPath(path, entry->spec.target))
        return path;
    const char* relative = path + os::Runtime::stringLength(entry->spec.target);
    return *relative ? relative : "/";
}
}  // namespace vfs::server
