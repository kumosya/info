#include "devices.h"

#include "os/abi.h"

namespace vfs::server {
namespace {
class DeviceNames {
 public:
    static bool prefix(const char* value, const char* start);
    static void text(char* target, size_t capacity, const uint8_t* source, size_t length);
    static void uuid(char* target, const uint8_t* source);
    static bool uuidEqual(const char* left, const char* right);
    static const char* selector(const DeviceRecord& record, const char* directory);
};

bool DeviceNames::prefix(const char* value, const char* start) {
    while (*start != '\0')
        if (*value++ != *start++) return false;
    return true;
}
void DeviceNames::text(char* target, size_t capacity, const uint8_t* source, size_t length) {
    while (length && (source[length - 1] == ' ' || source[length - 1] == 0))
        --length;
    if (length >= capacity) length = capacity - 1;
    for (size_t index = 0; index < length; ++index)
        target[index] = static_cast<char>(source[index]);
    target[length] = '\0';
}
void DeviceNames::uuid(char* target, const uint8_t* source) {
    constexpr char hex[] = "0123456789abcdef";
    uint32_t position    = 0;
    for (uint32_t index = 0; index < 16; ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10)
            target[position++] = '-';
        target[position++] = hex[source[index] >> 4];
        target[position++] = hex[source[index] & 15];
    }
    target[position] = '\0';
}
bool DeviceNames::uuidEqual(const char* left, const char* right) {
    while (*left && *right) {
        const char a = *left >= 'A' && *left <= 'F' ? *left + 32 : *left;
        const char b = *right >= 'A' && *right <= 'F' ? *right + 32 : *right;
        if (a != b) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}
const char* DeviceNames::selector(const DeviceRecord& record, const char* directory) {
    if (os::Runtime::stringsEqual(directory, "/dev/disk/by-uuid")) return record.uuid;
    if (os::Runtime::stringsEqual(directory, "/dev/disk/by-label")) return record.label;
    if (os::Runtime::stringsEqual(directory, "/dev/disk/by-partuuid"))
        return record.identity.partitionUuid;
    if (os::Runtime::stringsEqual(directory, "/dev/disk/by-path"))
        return record.identity.hardwarePath;
    return nullptr;
}
}  // namespace

bool DeviceCatalog::add(const os::StorageDeviceIdentity& identity,
                        SectorDevice& device) {
    if (count_ == os::maximumStorageDevices || identity.id != count_ ||
        identity.name[0] == '\0')
        return false;
    for (uint32_t index = 0; index < count_; ++index) {
        const auto& prior = records_[index].identity;
        if (os::DeviceNumber::deviceNumber(prior.major, prior.minor) ==
                os::DeviceNumber::deviceNumber(identity.major, identity.minor) ||
            os::Runtime::stringsEqual(prior.name, identity.name) ||
            os::Runtime::stringsEqual(prior.hardwarePath, identity.hardwarePath))
            return false;
    }
    DeviceRecord record{};
    record.identity = identity;
    uint8_t boot[2048]{};
    if (device.sectorCount() >= 4 && device.readSectors(0, 4, boot)) {
        record.format = FormatProbe::detectFormat(boot, sizeof(boot), nullptr, 0);
        if (record.format == Format::Ext3 || record.format == Format::Ext4) {
            DeviceNames::uuid(record.uuid, boot + 1024 + 104);
            DeviceNames::text(record.label, sizeof(record.label), boot + 1024 + 120, 16);
        } else if (record.format == Format::Fat32) {
            constexpr char hex[] = "0123456789ABCDEF";
            uint32_t position    = 0;
            for (int byte = 3; byte >= 0; --byte) {
                if (byte == 1) record.uuid[position++] = '-';
                record.uuid[position++] = hex[boot[67 + byte] >> 4];
                record.uuid[position++] = hex[boot[67 + byte] & 15];
            }
            DeviceNames::text(record.label, sizeof(record.label), boot + 71, 11);
        }
    }
    if (record.format == Format::Unknown && device.sectorCount() >= 68 &&
        device.readSectors(64, 4, boot) && FormatProbe::probeIso9660(boot, sizeof(boot))) {
        record.format = Format::Iso9660;
        DeviceNames::text(record.label, sizeof(record.label), boot + 40, 32);
        // libblkid uses the ISO volume creation timestamp as its filesystem
        // UUID.
        const uint8_t* stamp = boot + 813;
        bool timestamp       = true;
        for (uint32_t i = 0; i < 16; ++i)
            timestamp = timestamp && stamp[i] >= '0' && stamp[i] <= '9';
        if (timestamp) {
            uint32_t position = 0;
            for (uint32_t i = 0; i < 16; ++i) {
                if (i == 4 || i == 6 || i == 8 || i == 10 || i == 12 || i == 14)
                    record.uuid[position++] = '-';
                record.uuid[position++] = stamp[i];
            }
        }
        os::Runtime::copyString(record.opticalName, "sr0", sizeof(record.opticalName));
        if (opticalCount_ >= 10) return false;
        record.opticalName[2] = static_cast<char>('0' + opticalCount_++);
    }
    records_[count_++] = record;
    return true;
}

const DeviceRecord* DeviceCatalog::get(uint32_t index) const {
    return index < count_ ? &records_[index] : nullptr;
}

int DeviceCatalog::resolve(const char* source) const {
    if (source == nullptr || *source == '\0') return missing;
    int result = missing;
    for (uint32_t index = 0; index < count_; ++index) {
        const DeviceRecord& record = records_[index];
        bool match                 = false;
        if (DeviceNames::prefix(source, "UUID="))
            match = record.uuid[0] && DeviceNames::uuidEqual(source + 5, record.uuid);
        else if (DeviceNames::prefix(source, "LABEL="))
            match =
                record.label[0] && os::Runtime::stringsEqual(source + 6, record.label);
        else if (DeviceNames::prefix(source, "PARTUUID="))
            match = record.identity.partitionUuid[0] &&
                    DeviceNames::uuidEqual(source + 9, record.identity.partitionUuid);
        else if (DeviceNames::prefix(source, "/dev/disk/by-uuid/"))
            match = record.uuid[0] && DeviceNames::uuidEqual(source + 18, record.uuid);
        else if (DeviceNames::prefix(source, "/dev/disk/by-label/"))
            match =
                record.label[0] && os::Runtime::stringsEqual(source + 19, record.label);
        else if (DeviceNames::prefix(source, "/dev/disk/by-partuuid/"))
            match = record.identity.partitionUuid[0] &&
                    DeviceNames::uuidEqual(source + 22, record.identity.partitionUuid);
        else if (DeviceNames::prefix(source, "/dev/disk/by-path/"))
            match = os::Runtime::stringsEqual(source + 18, record.identity.hardwarePath);
        else if (DeviceNames::prefix(source, "/dev/")) {
            const char* name = source + 5;
            match            = os::Runtime::stringsEqual(name, record.identity.name) ||
                    (record.opticalName[0] &&
                     os::Runtime::stringsEqual(name, record.opticalName)) ||
                    (os::Runtime::stringsEqual(name, "cdrom") &&
                     os::Runtime::stringsEqual(record.opticalName, "sr0"));
        }
        if (match) {
            if (result != missing) return ambiguous;
            result = static_cast<int>(index);
        }
    }
    return result;
}

int DeviceCatalog::list(const char* directory, uint32_t requested,
                        DirectoryEntry& entry) const {
    uint32_t current = 0;
    auto emit        = [&](const char* name, bool dir = false) {
        if (current++ != requested) return false;
        entry      = {};
        entry.type = dir ? 2 : 1;
        os::Runtime::copyString(entry.name, name, sizeof(entry.name));
        return true;
    };
    if (os::Runtime::stringsEqual(directory, "/dev")) {
        if (emit("disk", true)) return 0;
        for (uint32_t index = 0; index < count_; ++index) {
            const DeviceRecord& record = records_[index];
            if (emit(record.identity.name)) return 0;
            if (record.opticalName[0] && emit(record.opticalName)) return 0;
            if (os::Runtime::stringsEqual(record.opticalName, "sr0") && emit("cdrom"))
                return 0;
        }
        return 1;
    }
    if (os::Runtime::stringsEqual(directory, "/dev/disk")) {
        constexpr const char* names[] = {"by-uuid", "by-label", "by-partuuid",
                                         "by-path"};
        for (const char* name : names)
            if (emit(name, true)) return 0;
        return 1;
    }
    for (uint32_t index = 0; index < count_; ++index) {
        const char* name = DeviceNames::selector(records_[index], directory);
        if (name == nullptr) return -1;
        if (*name == '\0') continue;
        bool duplicate = false;
        for (uint32_t prior = 0; prior < count_; ++prior) {
            if (prior != index &&
                os::Runtime::stringsEqual(name, DeviceNames::selector(records_[prior], directory)))
                duplicate = true;
        }
        if (!duplicate && emit(name)) return 0;
    }
    return 1;
}
}  // namespace vfs::server
