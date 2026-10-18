#include "../fat32.h"
#include "name.h"

namespace {

constexpr uint32_t sectorSize = 512;
constexpr uint32_t endOfChain = 0x0ffffff8U;

class FatDirectoryCodec {
 public:
    static uint16_t read16(const uint8_t* bytes);
    static uint32_t read32(const uint8_t* bytes);
    static void write16(uint8_t* bytes, uint16_t value);
    static void write32(uint8_t* bytes, uint32_t value);
    static char upperAscii(char value);
    static bool nameEquals(const char* requested, const char* shortName);
    static void copyShortName(const uint8_t* raw, char* name, size_t capacity);
};

uint16_t FatDirectoryCodec::read16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t FatDirectoryCodec::read32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void FatDirectoryCodec::write16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

void FatDirectoryCodec::write32(uint8_t* bytes, uint32_t value) {
    for (uint32_t index = 0; index < 4; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

char FatDirectoryCodec::upperAscii(char value) {
    return value >= 'a' && value <= 'z'
               ? static_cast<char>(value - 'a' + 'A')
               : value;
}

bool FatDirectoryCodec::nameEquals(const char* requested, const char* shortName) {
    uint32_t requestedIndex = 0;
    uint32_t nameIndex = 0;
    while (requested[requestedIndex] != '\0' &&
           requested[requestedIndex] != '/' && nameIndex < 11) {
        const char value = requested[requestedIndex++];
        if (value == '.') {
            while (nameIndex < 8) {
                if (shortName[nameIndex] != ' ') return false;
                ++nameIndex;
            }
            continue;
        }
        if (FatDirectoryCodec::upperAscii(value) != shortName[nameIndex++]) return false;
    }
    while (nameIndex < 11 && shortName[nameIndex] == ' ') ++nameIndex;
    return requested[requestedIndex] == '\0' && nameIndex == 11;
}

void FatDirectoryCodec::copyShortName(const uint8_t* raw, char* name, size_t capacity) {
    size_t output = 0;
    for (uint32_t index = 0; index < 8 && raw[index] != ' '; ++index) {
        if (output + 1 >= capacity) break;
        name[output++] = static_cast<char>(raw[index]);
    }
    if (raw[8] != ' ' && output + 1 < capacity) name[output++] = '.';
    for (uint32_t index = 8; index < 11 && raw[index] != ' '; ++index) {
        if (output + 1 >= capacity) break;
        name[output++] = static_cast<char>(raw[index]);
    }
    name[output] = '\0';
}

}  // namespace

bool Fat32FileSystem::mount(vfs::SectorDevice& device) {
    uint8_t boot[sectorSize]{};
    device_ = nullptr;
    if (!device.readSector(0, boot) || boot[510] != 0x55 ||
        boot[511] != 0xaa || FatDirectoryCodec::read16(boot + 11) != sectorSize ||
        boot[13] == 0 || boot[16] == 0 || FatDirectoryCodec::read16(boot + 14) == 0 ||
        FatDirectoryCodec::read32(boot + 36) == 0) {
        return false;
    }
    reservedSectors_ = FatDirectoryCodec::read16(boot + 14);
    fatCount_ = boot[16];
    sectorsPerCluster_ = boot[13];
    fatSectors_ = FatDirectoryCodec::read32(boot + 36);
    fsInfoSector_ = FatDirectoryCodec::read16(boot + 48);
    backupBootSector_ = FatDirectoryCodec::read16(boot + 50);
    totalSectors_ = FatDirectoryCodec::read16(boot + 19);
    if (totalSectors_ == 0) totalSectors_ = FatDirectoryCodec::read32(boot + 32);
    rootCluster_ = FatDirectoryCodec::read32(boot + 44) & 0x0fffffffU;
    const uint64_t dataStart =
        static_cast<uint64_t>(reservedSectors_) +
        static_cast<uint64_t>(fatCount_) * fatSectors_;
    if (totalSectors_ == 0 || totalSectors_ > device.sectorCount() ||
        dataStart >= totalSectors_ || rootCluster_ < 2 ||
        sectorsPerCluster_ > 128 ||
        (sectorsPerCluster_ & (sectorsPerCluster_ - 1)) != 0) {
        return false;
    }
    firstDataSector_ = static_cast<uint32_t>(dataStart);
    clusterCount_ =
        (totalSectors_ - firstDataSector_) / sectorsPerCluster_;
    if (clusterCount_ < 65525 ||
        static_cast<uint64_t>(clusterCount_) + 2 >
            static_cast<uint64_t>(fatSectors_) * sectorSize / 4 ||
        rootCluster_ >= clusterCount_ + 2) {
        return false;
    }
    device_ = &device;
    readOnly_ = device.readOnly();
    return true;
}

bool Fat32FileSystem::readOnly() const {
    return device_ == nullptr || readOnly_;
}

bool Fat32FileSystem::sync() {
    return device_ != nullptr && device_->flush();
}

bool Fat32FileSystem::lookup(const char* path, vfs::FileStatus& status) {
    if (device_ == nullptr || path == nullptr || path[0] != '/') return false;
    Node node{};
    if (!resolve(path, node)) return false;
    status.inode = node.firstCluster;
    status.mode = node.directory ? vfs::modeDirectory | 0755 : vfs::modeRegular | 0644;
    status.links = node.directory ? 2 : 1;
    if (readOnly()) status.mode &= ~0222U;
    if (node.entrySector != 0) {
        uint8_t entry[sectorSize]{};
        if (!readSector(node.entrySector, entry) || node.entryOffset > sectorSize - 32)
            return false;
        if (entry[node.entryOffset + 11] & 1U) status.mode &= ~0222U;
    }
    status.size = node.size;
    status.privateData =
        (static_cast<uint64_t>(node.entrySector) * sectorSize) +
        node.entryOffset;
    return true;
}

bool Fat32FileSystem::resolve(const char* path, Node& status) {
    status = {rootCluster_, 0, true, 0, 0, 0, 0};
    while (*path == '/') ++path;
    while (*path != '\0') {
        char component[256]{};
        uint32_t length = 0;
        while (*path != '\0' && *path != '/') {
            if (length + 1 >= sizeof(component)) return false;
            component[length++] = *path++;
        }
        while (*path == '/') ++path;
        if (length == 0) continue;
        component[length] = '\0';
        if (component[0] == '.' &&
            (component[1] == '\0' ||
             (component[1] == '.' && component[2] == '\0'))) {
            continue;
        }
        if (!status.directory) return false;
        Node child{};
        if (!findEntry(status.firstCluster, component, child)) return false;
        status = child;
    }
    return true;
}

bool Fat32FileSystem::findEntry(uint32_t directoryCluster, const char* name,
                                Node& status) {
    for (uint32_t index = 0; index < clusterCount_ * sectorsPerCluster_ * 16;
         ++index) {
        Entry entry{};
        Node candidate{};
        if (!readEntry(directoryCluster, index, entry, &candidate)) {
            return false;
        }
        if (entry.end) return false;
        if (entry.valid && (fat32::detail::NameCodec::equal(name, entry.name) || FatDirectoryCodec::nameEquals(name, entry.shortName))) {
            status = candidate;
            return true;
        }
    }
    return false;
}

bool Fat32FileSystem::readEntry(uint32_t directoryCluster, uint32_t index,
                                Entry& entry, Node* status) {
    uint8_t sector[sectorSize]{};
    const uint32_t entriesPerCluster = sectorsPerCluster_ * 16;
    uint32_t cluster = directoryCluster;
    uint32_t clusterOffset = index / entriesPerCluster;
    while (clusterOffset-- != 0) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= endOfChain) return false;
        cluster = next;
    }
    const uint32_t entryInCluster = index % entriesPerCluster;
    const uint32_t sectorIndex = entryInCluster / 16;
    const uint32_t entryOffset = (entryInCluster % 16) * 32;
    const uint32_t sectorNumber =
        firstDataSector_ + (cluster - 2) * sectorsPerCluster_ + sectorIndex;
    if (!readSector(sectorNumber, sector)) return false;
    const uint8_t* raw = sector + entryOffset;
    if (raw[0] == 0) {
        entry.end = true;
        return true;
    }
    if (raw[0] == 0xe5 || raw[11] == 0x0f || (raw[11] & 0x08) != 0) {
        return true;
    }
    for (uint32_t index = 0; index < sizeof(entry.shortName); ++index) {
        entry.shortName[index] = static_cast<char>(raw[index]);
    }
    FatDirectoryCodec::copyShortName(raw, entry.name, sizeof(entry.name));
    // Long entries precede the short entry in reverse ordinal order.
    // Reject broken chains/checksums and retain the interoperable 8.3 alias.
    const uint8_t sum = fat32::detail::NameCodec::checksum(entry.shortName);
    uint16_t units[260]{};
    uint8_t longEntries = 0;
    for (uint32_t ordinal = 1; ordinal <= 20 && ordinal <= index; ++ordinal) {
        uint32_t longSector = 0;
        uint16_t longOffset = 0;
        uint8_t bytes[sectorSize]{};
        if (!directoryLocation(directoryCluster, index - ordinal, longSector, longOffset) ||
            !readSector(longSector, bytes)) return false;
        const uint8_t* part = bytes + longOffset;
        if (part[0] != ordinal && part[0] != (ordinal | 0x40)) break;
        if (part[11] != 0x0f || part[12] || part[13] != sum || FatDirectoryCodec::read16(part + 26)) break;
        for (uint32_t unit = 0; unit < 13; ++unit)
            units[(ordinal - 1) * 13 + unit] = FatDirectoryCodec::read16(part + fat32::detail::unitOffsets[unit]);
        if (part[0] & 0x40) {
            char decoded[256]{};
            if (fat32::detail::NameCodec::toUtf8(units, ordinal * 13, decoded)) {
                for (size_t i = 0; i < sizeof(entry.name); ++i) entry.name[i] = decoded[i];
                longEntries = static_cast<uint8_t>(ordinal);
            }
            break;
        }
    }
    entry.directory = (raw[11] & 0x10) != 0;
    entry.valid = true;
    if (status != nullptr) {
        status->firstCluster =
            (static_cast<uint32_t>(FatDirectoryCodec::read16(raw + 20)) << 16) |
            FatDirectoryCodec::read16(raw + 26);
        status->size = FatDirectoryCodec::read32(raw + 28);
        status->directory = entry.directory;
        status->entrySector = sectorNumber;
        status->entryOffset = static_cast<uint16_t>(entryOffset);
        status->entryIndex = index;
        status->longEntries = longEntries;
    }
    return true;
}

int Fat32FileSystem::readDirectory(const char* path, uint32_t index,
                                   vfs::DirectoryEntry& entry) {
    Node directory{};
    if (!resolve(path, directory) || !directory.directory) return -1;
    uint32_t visibleIndex = 0;
    for (uint32_t rawIndex = 0;
         rawIndex < clusterCount_ * sectorsPerCluster_ * 16; ++rawIndex) {
        Entry candidate{};
        if (!readEntry(directory.firstCluster, rawIndex, candidate, nullptr)) {
            return 1;
        }
        if (candidate.end) return 1;
        if (!candidate.valid || fat32::detail::NameCodec::equal(candidate.name, ".") ||
            fat32::detail::NameCodec::equal(candidate.name, "..")) {
            continue;
        }
        if (visibleIndex++ == index) {
            entry.type = candidate.directory ? 2 : 1;
            size_t nameIndex = 0;
            while (nameIndex + 1 < sizeof(entry.name) &&
                   candidate.name[nameIndex] != '\0') {
                entry.name[nameIndex] = candidate.name[nameIndex];
                ++nameIndex;
            }
            entry.name[nameIndex] = '\0';
            return 0;
        }
    }
    return 1;
}

int64_t Fat32FileSystem::read(const vfs::FileStatus& status, uint64_t offset,
                              void* buffer, size_t size) {
    if (device_ == nullptr || buffer == nullptr ||
        (status.mode & 0xf000U) == vfs::modeDirectory) {
        return -1;
    }
    if (offset >= status.size || size == 0) return 0;
    const uint64_t remaining = status.size - offset;
    if (size > remaining) size = static_cast<size_t>(remaining);
    const uint32_t clusterBytes = sectorsPerCluster_ * sectorSize;
    uint32_t cluster = status.inode;
    if (status.privateData != 0) {
        const uint32_t sector = static_cast<uint32_t>(
            status.privateData / sectorSize);
        const uint16_t entryOffset = static_cast<uint16_t>(
            status.privateData % sectorSize);
        uint8_t entrySector[sectorSize]{};
        if (entryOffset > sectorSize - 32 ||
            !readSector(sector, entrySector)) {
            return -1;
        }
        const uint8_t* raw = entrySector + entryOffset;
        cluster = (static_cast<uint32_t>(FatDirectoryCodec::read16(raw + 20)) << 16) |
                  FatDirectoryCodec::read16(raw + 26);
    }
    if (cluster < 2) return 0;
    uint64_t clusterIndex = offset / clusterBytes;
    while (clusterIndex-- != 0) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= endOfChain) return -1;
        cluster = next;
    }
    uint64_t withinCluster = offset % clusterBytes;
    size_t copied = 0;
    uint8_t sector[sectorSize]{};
    while (copied < size) {
        const uint32_t sectorIndex =
            static_cast<uint32_t>(withinCluster / sectorSize);
        const uint32_t sectorOffset =
            static_cast<uint32_t>(withinCluster % sectorSize);
        const uint32_t sectorNumber =
            firstDataSector_ + (cluster - 2) * sectorsPerCluster_ +
            sectorIndex;
        if (!readSector(sectorNumber, sector)) return -1;
        size_t chunk = sectorSize - sectorOffset;
        if (chunk > size - copied) chunk = size - copied;
        for (size_t byte = 0; byte < chunk; ++byte) {
            static_cast<uint8_t*>(buffer)[copied + byte] =
                sector[sectorOffset + byte];
        }
        copied += chunk;
        withinCluster += chunk;
        if (withinCluster == clusterBytes && copied < size) {
            uint32_t next = 0;
            if (!nextCluster(cluster, next) || next >= endOfChain) return -1;
            cluster = next;
            withinCluster = 0;
        }
    }
    return static_cast<int64_t>(copied);
}

int64_t Fat32FileSystem::write(const vfs::FileStatus& status,
                               uint64_t offset, const void* buffer,
                               size_t size) {
    if (readOnly() || status.privateData == 0 || buffer == nullptr ||
        (status.mode & 0xf000U) == vfs::modeDirectory ||
        offset > UINT32_MAX || size > UINT32_MAX - offset) {
        return -1;
    }
    if (size == 0) return 0;

    const uint32_t entrySectorNumber = static_cast<uint32_t>(
        status.privateData / sectorSize);
    const uint16_t entryOffset = static_cast<uint16_t>(
        status.privateData % sectorSize);
    uint8_t entrySector[sectorSize]{};
    if (entryOffset > sectorSize - 32 ||
        !readSector(entrySectorNumber, entrySector)) {
        return -1;
    }
    uint8_t* raw = entrySector + entryOffset;
    uint32_t firstCluster =
        (static_cast<uint32_t>(FatDirectoryCodec::read16(raw + 20)) << 16) | FatDirectoryCodec::read16(raw + 26);
    const uint32_t previousSize = FatDirectoryCodec::read32(raw + 28);
    const uint32_t clusterBytes = sectorsPerCluster_ * sectorSize;
    size_t written = 0;
    uint8_t sector[sectorSize]{};
    while (written < size) {
        const uint64_t position = offset + written;
        const uint32_t logicalCluster =
            static_cast<uint32_t>(position / clusterBytes);
        if (firstCluster < 2) {
            if (!allocateCluster(firstCluster)) return -1;
        }
        uint32_t cluster = firstCluster;
        for (uint32_t logical = 0; logical < logicalCluster; ++logical) {
            uint32_t next = 0;
            if (!nextCluster(cluster, next)) return -1;
            if (next >= endOfChain) {
                if (!allocateCluster(next) || !setFatEntry(cluster, next)) {
                    return -1;
                }
            }
            cluster = next;
        }

        const uint32_t withinCluster =
            static_cast<uint32_t>(position % clusterBytes);
        const uint32_t sectorIndex = withinCluster / sectorSize;
        const uint32_t sectorOffset = withinCluster % sectorSize;
        const size_t chunk =
            size - written < sectorSize - sectorOffset
                ? size - written
                : sectorSize - sectorOffset;
        const uint32_t sectorNumber =
            firstDataSector_ + (cluster - 2) * sectorsPerCluster_ +
            sectorIndex;
        if (!readSector(sectorNumber, sector)) return -1;
        for (size_t index = 0; index < chunk; ++index) {
            sector[sectorOffset + index] =
                static_cast<const uint8_t*>(buffer)[written + index];
        }
        if (!writeSector(sectorNumber, sector)) return -1;
        written += chunk;
    }

    if (firstCluster < 2 ||
        !updateFileEntry(status.privateData, firstCluster,
                         previousSize > offset + written
                             ? previousSize
                             : static_cast<uint32_t>(offset + written))) {
        return -1;
    }
    return static_cast<int64_t>(written);
}

bool Fat32FileSystem::truncate(uint32_t) { return false; }

bool Fat32FileSystem::truncate(const vfs::FileStatus& status) {
    if (readOnly() || status.privateData == 0 ||
        (status.mode & 0xf000U) == vfs::modeDirectory) {
        return false;
    }
    uint32_t cluster = status.inode;
    for (uint32_t visited = 0; cluster >= 2 && visited < clusterCount_;
         ++visited) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || !setFatEntry(cluster, 0)) {
            return false;
        }
        if (next >= endOfChain) {
            cluster = 0;
            break;
        }
        cluster = next;
    }
    return cluster == 0 &&
           updateFileEntry(status.privateData, 0, 0) && sync();
}

bool Fat32FileSystem::create(const char* path, bool directory) {
    if (readOnly() || path == nullptr || path[0] != '/') return false;
    Node parent{};
    char name[256]{};
    char shortName[11]{};
    uint16_t units[255]{};
    size_t unitCount = 0;
    if (!resolveParent(path, parent, name, sizeof(name)) ||
        !fat32::detail::NameCodec::toUtf16(name, units, unitCount)) {
        return false;
    }
    Node existing{};
    if (findEntry(parent.firstCluster, name, existing)) return false;

    const bool shortEncoded = encodeShortName(name, shortName);
    char shortText[13]{};
    if (shortEncoded) FatDirectoryCodec::copyShortName(reinterpret_cast<const uint8_t*>(shortName), shortText, sizeof(shortText));
    bool exactShort = shortEncoded;
    for (size_t i = 0; exactShort; ++i) {
        if (shortText[i] != name[i]) { exactShort = false; break; }
        if (!name[i]) break;
    }
    if (!shortEncoded) {
        bool available = false;
        for (uint32_t alias = 1; alias < 1000000; ++alias) {
            for (auto& c : shortName) c = ' ';
            char digits[6];
            unsigned count = 0;
            for (uint32_t n = alias; n; n /= 10) digits[count++] = '0' + n % 10;
            unsigned base = 0;
            size_t extension = 0;
            for (size_t i = 0; name[i]; ++i) if (name[i] == '.') extension = i + 1;
            for (size_t i = 0; name[i] && (extension == 0 || i + 1 < extension) && base < 7 - count; ++i) {
                const char c = FatDirectoryCodec::upperAscii(name[i]);
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') shortName[base++] = c;
            }
            if (!base) shortName[base++] = '_';
            shortName[base++] = '~';
            while (count) shortName[base++] = digits[--count];
            if (extension) for (unsigned i = 0; i < 3 && name[extension + i]; ++i) {
                const char c = FatDirectoryCodec::upperAscii(name[extension + i]);
                shortName[8 + i] = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_';
            }
            FatDirectoryCodec::copyShortName(reinterpret_cast<const uint8_t*>(shortName), shortText, sizeof(shortText));
            if (!findEntry(parent.firstCluster, shortText, existing)) { available = true; break; }
        }
        if (!available) return false;
    }
    const uint32_t longCount = exactShort ? 0 : (unitCount + 12) / 13;
    uint32_t firstEntry = 0;
    if (!findFreeEntries(parent.firstCluster, longCount + 1, firstEntry)) return false;

    uint32_t entrySectorNumber = 0;
    uint16_t entryOffset = 0;
    if (!directoryLocation(parent.firstCluster, firstEntry + longCount, entrySectorNumber, entryOffset)) {
        return false;
    }
    uint32_t firstCluster = 0;
    if (directory && !allocateCluster(firstCluster)) return false;
    for (uint32_t slot = 0; slot < longCount; ++slot) {
        uint32_t number = 0;
        uint16_t offset = 0;
        uint8_t bytes[sectorSize]{};
        if (!directoryLocation(parent.firstCluster, firstEntry + slot, number, offset) ||
            !readSector(number, bytes)) { releaseChain(firstCluster); return false; }
        uint8_t* part = bytes + offset;
        for (unsigned i = 0; i < 32; ++i) part[i] = 0;
        const uint32_t ordinal = longCount - slot;
        part[0] = ordinal | (slot == 0 ? 0x40 : 0);
        part[11] = 0x0f;
        part[13] = fat32::detail::NameCodec::checksum(shortName);
        for (uint32_t i = 0; i < 13; ++i) {
            const size_t unit = (ordinal - 1) * 13 + i;
            FatDirectoryCodec::write16(part + fat32::detail::unitOffsets[i],
                    unit < unitCount ? units[unit] : unit == unitCount ? 0 : 0xffff);
        }
        if (!writeSector(number, bytes)) { releaseChain(firstCluster); return false; }
    }
    uint8_t sector[sectorSize]{};
    if (!readSector(entrySectorNumber, sector)) return false;
    uint8_t* raw = sector + entryOffset;
    for (unsigned i = 0; i < 32; ++i) raw[i] = 0;
    for (uint32_t index = 0; index < sizeof(shortName); ++index) {
        raw[index] = static_cast<uint8_t>(shortName[index]);
    }
    raw[11] = directory ? 0x10 : 0x20;
    FatDirectoryCodec::write16(raw + 20, static_cast<uint16_t>(firstCluster >> 16));
    FatDirectoryCodec::write16(raw + 26, static_cast<uint16_t>(firstCluster));
    FatDirectoryCodec::write32(raw + 28, 0);
    if (!writeSector(entrySectorNumber, sector)) return false;

    if (directory) {
        uint8_t dotSector[sectorSize]{};
        dotSector[0] = '.';
        for (uint32_t index = 1; index < 11; ++index) dotSector[index] = ' ';
        dotSector[11] = 0x10;
        FatDirectoryCodec::write16(dotSector + 20,
                static_cast<uint16_t>(firstCluster >> 16));
        FatDirectoryCodec::write16(dotSector + 26, static_cast<uint16_t>(firstCluster));
        uint8_t* dotdot = dotSector + 32;
        dotdot[0] = '.';
        dotdot[1] = '.';
        for (uint32_t index = 2; index < 11; ++index) dotdot[index] = ' ';
        dotdot[11] = 0x10;
        const uint32_t parentCluster =
            parent.firstCluster == rootCluster_ ? 0 : parent.firstCluster;
        FatDirectoryCodec::write16(dotdot + 20,
                static_cast<uint16_t>(parentCluster >> 16));
        FatDirectoryCodec::write16(dotdot + 26, static_cast<uint16_t>(parentCluster));
        const uint32_t sectorNumber =
            firstDataSector_ + (firstCluster - 2) * sectorsPerCluster_;
        if (!writeSector(sectorNumber, dotSector)) return false;
    }
    return true;
}

bool Fat32FileSystem::releaseChain(uint32_t cluster) {
    if (!cluster) return true;
    for (uint32_t visited = 0; visited < clusterCount_; ++visited) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || !setFatEntry(cluster, 0)) return false;
        if (next >= endOfChain) return true;
        cluster = next;
    }
    return false;
}

bool Fat32FileSystem::remove(const char* path) {
    Node parent{}, node{};
    char name[256]{};
    if (readOnly() || !resolveParent(path, parent, name, sizeof(name)) ||
        !findEntry(parent.firstCluster, name, node) || node.firstCluster == rootCluster_)
        return false;
    if (node.directory) {
        for (uint32_t index = 0; index < clusterCount_ * sectorsPerCluster_ * 16; ++index) {
            Entry entry{};
            if (!readEntry(node.firstCluster, index, entry, nullptr)) return false;
            if (entry.end) break;
            if (entry.valid && !fat32::detail::NameCodec::equal(entry.name, ".") &&
                !fat32::detail::NameCodec::equal(entry.name, "..")) return false;
        }
    }
    // Remove the visible short record first; then erase its validated LFN chain.
    for (uint32_t i = 0; i <= node.longEntries; ++i) {
        uint32_t sector = 0;
        uint16_t offset = 0;
        uint8_t data[sectorSize]{};
        if (!directoryLocation(parent.firstCluster, node.entryIndex - i, sector, offset) ||
            !readSector(sector, data)) return false;
        data[offset] = 0xe5;
        if (!writeSector(sector, data)) return false;
    }
    return releaseChain(node.firstCluster) && sync();
}

bool Fat32FileSystem::rename(const char* existing, const char* newPath) {
    Node from{}, to{}, source{}, target{};
    char oldName[256]{}, newName[256]{};
    if (readOnly() || !resolveParent(existing, from, oldName, sizeof(oldName)) ||
        !resolveParent(newPath, to, newName, sizeof(newName)) ||
        !findEntry(from.firstCluster, oldName, source)) return false;
    if (source.directory) {
        uint32_t ancestor = to.firstCluster;
        for (uint32_t visited = 0; visited < clusterCount_; ++visited) {
            if (ancestor == source.firstCluster) return false;
            if (!ancestor || ancestor == rootCluster_) break;
            Node parent{};
            if (!findEntry(ancestor, "..", parent) || parent.firstCluster == ancestor) return false;
            ancestor = parent.firstCluster;
            if (visited + 1 == clusterCount_) return false;
        }
    }
    if (findEntry(to.firstCluster, newName, target)) {
        if (source.entrySector == target.entrySector && source.entryOffset == target.entryOffset) return true;
        if (source.directory != target.directory || !remove(newPath)) return false;
    }
    uint8_t original[sectorSize]{};
    if (!readSector(source.entrySector, original)) return false;
    uint8_t metadata[21];
    for (unsigned i = 0; i < sizeof(metadata); ++i) metadata[i] = original[source.entryOffset + 11 + i];
    if (!create(newPath, source.directory) || !findEntry(to.firstCluster, newName, target)) return false;
    uint8_t destination[sectorSize]{};
    if (!readSector(target.entrySector, destination)) return false;
    for (unsigned i = 0; i < sizeof(metadata); ++i) destination[target.entryOffset + 11 + i] = metadata[i];
    if (!writeSector(target.entrySector, destination)) return false;
    if (target.firstCluster && !releaseChain(target.firstCluster)) return false;
    if (source.directory && from.firstCluster != to.firstCluster) {
        const uint32_t sector = firstDataSector_ + (source.firstCluster - 2) * sectorsPerCluster_;
        uint8_t dot[sectorSize]{};
        if (!readSector(sector, dot) || dot[32] != '.' || dot[33] != '.') return false;
        const uint32_t parent = to.firstCluster == rootCluster_ ? 0 : to.firstCluster;
        FatDirectoryCodec::write16(dot + 32 + 20, static_cast<uint16_t>(parent >> 16));
        FatDirectoryCodec::write16(dot + 32 + 26, static_cast<uint16_t>(parent));
        if (!writeSector(sector, dot)) return false;
    }
    for (uint32_t i = 0; i <= source.longEntries; ++i) {
        uint32_t sector = 0; uint16_t offset = 0; uint8_t data[sectorSize]{};
        if (!directoryLocation(from.firstCluster, source.entryIndex - i, sector, offset) ||
            !readSector(sector, data)) return false;
        data[offset] = 0xe5;
        if (!writeSector(sector, data)) return false;
    }
    return sync();
}

bool Fat32FileSystem::changeMode(const char* path, uint16_t mode) {
    if (readOnly()) return false;
    vfs::FileStatus status{};
    if (!lookup(path, status) || status.privateData == 0) return false;
    const uint32_t sectorNumber =
        static_cast<uint32_t>(status.privateData / sectorSize);
    const uint16_t offset =
        static_cast<uint16_t>(status.privateData % sectorSize);
    if (offset > sectorSize - 32) return false;
    uint8_t sector[sectorSize]{};
    if (!readSector(sectorNumber, sector)) return false;
    uint8_t* entry = sector + offset;
    if (entry[0] == 0 || entry[0] == 0xe5 || (entry[11] & 0x08) != 0) {
        return false;
    }
    if ((mode & 0222U) == 0) {
        entry[11] |= 0x01;
    } else {
        entry[11] &= static_cast<uint8_t>(~0x01U);
    }
    return writeSector(sectorNumber, sector) && sync();
}

bool Fat32FileSystem::nextCluster(uint32_t cluster, uint32_t& next) {
    if (cluster < 2 || cluster >= clusterCount_ + 2) return false;
    const uint64_t fatOffset = static_cast<uint64_t>(cluster) * 4;
    const uint64_t sectorNumber =
        static_cast<uint64_t>(reservedSectors_) + fatOffset / sectorSize;
    uint8_t sector[sectorSize]{};
    if (fatOffset / sectorSize >= fatSectors_ ||
        sectorNumber > UINT32_MAX ||
        !readSector(static_cast<uint32_t>(sectorNumber), sector)) {
        return false;
    }
    next = FatDirectoryCodec::read32(sector + static_cast<uint32_t>(fatOffset % sectorSize)) &
           0x0fffffffU;
    return next >= endOfChain ||
           (next >= 2 && next < clusterCount_ + 2);
}

bool Fat32FileSystem::readSector(uint32_t sector, uint8_t* buffer) {
    return device_ != nullptr && sector < totalSectors_ &&
           device_->readSector(sector, buffer);
}

bool Fat32FileSystem::writeSector(uint32_t sector, const uint8_t* buffer) {
    return device_ != nullptr && !readOnly_ && sector < totalSectors_ &&
           device_->writeSector(sector, buffer);
}

bool Fat32FileSystem::directoryLocation(uint32_t directoryCluster,
                                        uint32_t index, uint32_t& sector,
                                        uint16_t& offset) {
    const uint32_t entriesPerCluster = sectorsPerCluster_ * 16;
    uint32_t cluster = directoryCluster;
    uint32_t clusterOffset = index / entriesPerCluster;
    while (clusterOffset-- != 0) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= endOfChain) return false;
        cluster = next;
    }
    const uint32_t entryInCluster = index % entriesPerCluster;
    sector = firstDataSector_ + (cluster - 2) * sectorsPerCluster_ +
             entryInCluster / 16;
    offset = static_cast<uint16_t>((entryInCluster % 16) * 32);
    return true;
}

bool Fat32FileSystem::findFreeEntry(uint32_t directoryCluster,
                                    uint32_t& sector, uint16_t& offset) {
    const uint32_t entriesPerCluster = sectorsPerCluster_ * 16;
    uint32_t cluster = directoryCluster;
    for (uint32_t visited = 0; visited < clusterCount_; ++visited) {
        for (uint32_t index = 0; index < entriesPerCluster; ++index) {
            uint8_t data[sectorSize]{};
            uint32_t currentSector = 0;
            uint16_t currentOffset = 0;
            if (!directoryLocation(directoryCluster,
                                   visited * entriesPerCluster + index,
                                   currentSector, currentOffset)) {
                break;
            }
            if (!readSector(currentSector, data)) return false;
            const uint8_t first = data[currentOffset];
            if (first == 0 || first == 0xe5) {
                sector = currentSector;
                offset = currentOffset;
                return true;
            }
        }
        uint32_t next = 0;
        if (!nextCluster(cluster, next)) return false;
        if (next >= endOfChain) {
            if (!allocateCluster(next) || !setFatEntry(cluster, next)) {
                return false;
            }
            sector = firstDataSector_ + (next - 2) * sectorsPerCluster_;
            offset = 0;
            return true;
        }
        cluster = next;
    }
    return false;
}

bool Fat32FileSystem::findFreeEntries(uint32_t directoryCluster, uint32_t count, uint32_t& first) {
    const uint32_t perCluster = sectorsPerCluster_ * 16;
    uint32_t cluster = directoryCluster, run = 0;
    for (uint32_t visited = 0; visited < clusterCount_; ++visited) {
        for (uint32_t slot = 0; slot < perCluster; ++slot) {
            const uint32_t sector = firstDataSector_ + (cluster - 2) * sectorsPerCluster_ + slot / 16;
            uint8_t data[sectorSize]{};
            if (!readSector(sector, data)) return false;
            const uint8_t byte = data[(slot % 16) * 32];
            if (byte == 0 || byte == 0xe5) {
                if (!run) first = visited * perCluster + slot;
                if (++run == count) return true;
            } else run = 0;
        }
        uint32_t next = 0;
        if (!nextCluster(cluster, next)) return false;
        if (next >= endOfChain && (!allocateCluster(next) || !setFatEntry(cluster, next))) return false;
        cluster = next;
    }
    return false;
}

bool Fat32FileSystem::allocateCluster(uint32_t& cluster) {
    if (readOnly()) return false;
    uint8_t sector[sectorSize]{};
    for (uint32_t fatSector = 0; fatSector < fatSectors_; ++fatSector) {
        if (!readSector(reservedSectors_ + fatSector, sector)) return false;
        for (uint32_t slot = 0; slot < sectorSize / 4; ++slot) {
            const uint32_t candidate =
                fatSector * (sectorSize / 4) + slot;
            if (candidate < 2 || candidate >= clusterCount_ + 2 ||
                FatDirectoryCodec::read32(sector + slot * 4) != 0) {
                continue;
            }
            if (!setFatEntry(candidate, endOfChain)) return false;
            uint8_t zero[sectorSize]{};
            const uint32_t firstSector =
                firstDataSector_ + (candidate - 2) * sectorsPerCluster_;
            for (uint32_t index = 0; index < sectorsPerCluster_; ++index) {
                if (!writeSector(firstSector + index, zero)) return false;
            }
            cluster = candidate;
            return true;
        }
    }
    return false;
}

bool Fat32FileSystem::setFatEntry(uint32_t cluster, uint32_t value) {
    if (readOnly() || cluster < 2 || cluster >= clusterCount_ + 2) {
        return false;
    }
    const uint32_t fatOffset = cluster * 4;
    const uint32_t fatSectorOffset = fatOffset / sectorSize;
    const uint32_t byteOffset = fatOffset % sectorSize;
    if (fatSectorOffset >= fatSectors_) return false;
    uint8_t sector[sectorSize]{};
    bool wasFree = false;
    for (uint32_t copy = 0; copy < fatCount_; ++copy) {
        const uint32_t sectorNumber =
            reservedSectors_ + copy * fatSectors_ + fatSectorOffset;
        if (!readSector(sectorNumber, sector)) return false;
        const uint32_t previous = FatDirectoryCodec::read32(sector + byteOffset);
        if (copy == 0) wasFree = (previous & 0x0fffffffU) == 0;
        FatDirectoryCodec::write32(sector + byteOffset,
                (previous & 0xf0000000U) | (value & 0x0fffffffU));
        if (!writeSector(sectorNumber, sector)) return false;
    }
    if (wasFree && value != 0) return adjustFreeClusterCount(-1);
    if (!wasFree && value == 0) return adjustFreeClusterCount(1);
    return true;
}

bool Fat32FileSystem::adjustFreeClusterCount(int32_t delta) {
    if (fsInfoSector_ == 0xffff || fsInfoSector_ >= reservedSectors_) {
        return true;
    }
    uint32_t sectors[2]{fsInfoSector_, UINT32_MAX};
    const uint32_t backupInfo =
        static_cast<uint32_t>(backupBootSector_) + fsInfoSector_;
    if (backupBootSector_ != 0xffff && backupInfo < reservedSectors_ &&
        backupInfo != fsInfoSector_) {
        sectors[1] = backupInfo;
    }
    for (uint32_t sectorIndex = 0; sectorIndex < 2; ++sectorIndex) {
        if (sectors[sectorIndex] == UINT32_MAX) continue;
        uint8_t data[sectorSize]{};
        if (!readSector(sectors[sectorIndex], data)) return false;
        if (FatDirectoryCodec::read32(data) != 0x41615252U ||
            FatDirectoryCodec::read32(data + 484) != 0x61417272U ||
            FatDirectoryCodec::read32(data + 508) != 0xaa550000U) {
            continue;
        }
        const uint32_t freeCount = FatDirectoryCodec::read32(data + 488);
        if (freeCount == UINT32_MAX) continue;
        if (delta < 0) {
            if (freeCount < static_cast<uint32_t>(-delta)) return false;
            FatDirectoryCodec::write32(data + 488, freeCount - static_cast<uint32_t>(-delta));
        } else {
            const uint32_t increase = static_cast<uint32_t>(delta);
            if (freeCount > clusterCount_ - increase) return false;
            FatDirectoryCodec::write32(data + 488, freeCount + increase);
        }
        if (!writeSector(sectors[sectorIndex], data)) return false;
    }
    return true;
}

bool Fat32FileSystem::clusterAt(uint32_t first, uint32_t index,
                                uint32_t& cluster) {
    if (first < 2 || first >= clusterCount_ + 2) return false;
    cluster = first;
    while (index-- != 0) {
        uint32_t next = 0;
        if (!nextCluster(cluster, next) || next >= endOfChain) return false;
        cluster = next;
    }
    return true;
}

bool Fat32FileSystem::updateFileEntry(uint64_t cookie, uint32_t firstCluster,
                                      uint32_t size) {
    if (readOnly() || cookie == 0) return false;
    const uint32_t sectorNumber =
        static_cast<uint32_t>(cookie / sectorSize);
    const uint16_t offset = static_cast<uint16_t>(cookie % sectorSize);
    uint8_t sector[sectorSize]{};
    if (offset > sectorSize - 32 || !readSector(sectorNumber, sector)) {
        return false;
    }
    uint8_t* raw = sector + offset;
    if (raw[0] == 0 || raw[0] == 0xe5) return false;
    FatDirectoryCodec::write16(raw + 20, static_cast<uint16_t>(firstCluster >> 16));
    FatDirectoryCodec::write16(raw + 26, static_cast<uint16_t>(firstCluster));
    FatDirectoryCodec::write32(raw + 28, size);
    return writeSector(sectorNumber, sector);
}

bool Fat32FileSystem::resolveParent(const char* path, Node& parent,
                                    char* name, size_t capacity) {
    if (path == nullptr || path[0] != '/' || name == nullptr || capacity < 2) {
        return false;
    }
    size_t length = 0;
    while (path[length] != '\0') ++length;
    while (length > 1 && path[length - 1] == '/') --length;
    size_t separator = length;
    while (separator != 0 && path[separator - 1] != '/') --separator;
    if (separator == length || length - separator + 1 > capacity) return false;
    size_t nameLength = length - separator;
    for (size_t index = 0; index < nameLength; ++index) {
        name[index] = path[separator + index];
    }
    name[nameLength] = '\0';
    if (fat32::detail::NameCodec::equal(name, ".") || fat32::detail::NameCodec::equal(name, "..")) return false;
    if (separator == 1) return resolve("/", parent);
    char parentPath[256]{};
    if (separator >= sizeof(parentPath)) return false;
    for (size_t index = 0; index < separator - 1; ++index) {
        parentPath[index] = path[index];
    }
    parentPath[separator - 1] = '\0';
    return resolve(parentPath, parent);
}

bool Fat32FileSystem::encodeShortName(const char* name, char output[11]) {
    if (name == nullptr || output == nullptr || *name == '\0') return false;
    for (uint32_t index = 0; index < 11; ++index) output[index] = ' ';
    uint32_t baseLength = 0;
    uint32_t extensionLength = 0;
    bool extension = false;
    for (uint32_t index = 0; name[index] != '\0'; ++index) {
        const char value = name[index];
        if (value == '.') {
            if (extension || baseLength == 0) return false;
            extension = true;
            continue;
        }
        const char upper = FatDirectoryCodec::upperAscii(value);
        const bool valid = (upper >= 'A' && upper <= 'Z') ||
                           (upper >= '0' && upper <= '9') || upper == '_' ||
                           upper == '-';
        if (!valid) return false;
        if (extension) {
            if (extensionLength == 3) return false;
            output[8 + extensionLength++] = upper;
        } else {
            if (baseLength == 8) return false;
            output[baseLength++] = upper;
        }
    }
    return baseLength != 0;
}
