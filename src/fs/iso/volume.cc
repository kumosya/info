#include "../iso.h"

namespace fs::iso9660 {

namespace {

constexpr uint32_t isoBlockSize = 2048;

uint32_t readLe32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8) |
           (static_cast<uint32_t>(value[2]) << 16) |
           (static_cast<uint32_t>(value[3]) << 24);
}

char lower(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32)
                                        : value;
}

bool sameName(const uint8_t* raw, uint32_t rawLength, const char* name,
              size_t nameLength) {
    uint32_t length = rawLength;
    for (uint32_t index = 0; index < rawLength; ++index) {
        if (raw[index] == ';') {
            length = index;
            break;
        }
    }
    if (length != nameLength) return false;
    for (uint32_t index = 0; index < length; ++index) {
        if (lower(static_cast<char>(raw[index])) != lower(name[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool Volume::mount(SectorDevice& device) {
    device_ = &device;
    uint8_t block[isoBlockSize]{};
    if (!device.readOnly() || !readBlock(16, block) || block[0] != 1 ||
        block[6] != 1 || block[1] != 'C' || block[2] != 'D' ||
        block[3] != '0' || block[4] != '0' || block[5] != '1') {
        return false;
    }
    const uint8_t* root = block + 156;
    if (root[0] < 34 || (root[25] & 2U) == 0) return false;
    root_ = {readLe32(root + 2), readLe32(root + 10), true};
    return root_.extent != 0 && root_.size != 0;
}

bool Volume::readBlock(uint32_t block, void* buffer) {
    return device_ != nullptr &&
           device_->readSectors(static_cast<uint64_t>(block) * 4, 4, buffer);
}

bool Volume::readRecord(const FileStatus& directory, uint32_t offset,
                        uint8_t* block, const uint8_t*& record,
                        uint32_t& nextOffset) {
    while (offset < directory.size) {
        const uint32_t blockIndex = offset / isoBlockSize;
        const uint32_t blockOffset = offset % isoBlockSize;
        if (!readBlock(directory.extent + blockIndex, block)) return false;
        const uint8_t length = block[blockOffset];
        if (length == 0) {
            offset = (blockIndex + 1) * isoBlockSize;
            continue;
        }
        if (length < 34 || blockOffset + length > isoBlockSize ||
            offset + length > directory.size) {
            return false;
        }
        record = block + blockOffset;
        nextOffset = offset + length;
        return true;
    }
    return false;
}

bool Volume::findInDirectory(const FileStatus& directory, const char* name,
                             size_t nameLength, FileStatus& status) {
    uint8_t block[isoBlockSize]{};
    uint32_t offset = 0;
    while (offset < directory.size) {
        const uint8_t* record = nullptr;
        uint32_t nextOffset = 0;
        if (!readRecord(directory, offset, block, record, nextOffset)) {
            return false;
        }
        offset = nextOffset;
        const uint8_t length = record[32];
        if (length == 1 && (record[33] == 0 || record[33] == 1)) continue;
        if (sameName(record + 33, length, name, nameLength)) {
            status = {readLe32(record + 2), readLe32(record + 10),
                      (record[25] & 2U) != 0};
            return true;
        }
    }
    return false;
}

bool Volume::lookup(const char* path, FileStatus& status) {
    if (device_ == nullptr || path == nullptr || path[0] != '/') return false;
    status = root_;
    uint32_t position = 1;
    while (path[position] != '\0') {
        while (path[position] == '/') ++position;
        if (path[position] == '\0') break;
        const uint32_t start = position;
        while (path[position] != '\0' && path[position] != '/') ++position;
        if (!status.directory ||
            !findInDirectory(status, path + start, position - start, status)) {
            return false;
        }
    }
    return true;
}

int64_t Volume::read(const FileStatus& status, uint64_t offset, void* buffer,
                     size_t size) {
    if (status.directory || buffer == nullptr || offset >= status.size) {
        return offset == status.size ? 0 : -1;
    }
    if (size > status.size - offset) size = status.size - offset;
    uint8_t block[isoBlockSize]{};
    size_t copied = 0;
    while (copied < size) {
        const uint64_t current = offset + copied;
        if (!readBlock(status.extent + current / isoBlockSize, block)) return -1;
        const uint32_t within = static_cast<uint32_t>(current % isoBlockSize);
        size_t chunk = isoBlockSize - within;
        if (chunk > size - copied) chunk = size - copied;
        for (size_t index = 0; index < chunk; ++index) {
            static_cast<uint8_t*>(buffer)[copied + index] = block[within + index];
        }
        copied += chunk;
    }
    return static_cast<int64_t>(copied);
}

int Volume::readDirectory(const char* path, uint32_t index,
                          DirectoryEntry& entry) {
    FileStatus directory{};
    if (!lookup(path, directory) || !directory.directory) return -1;
    uint8_t block[isoBlockSize]{};
    uint32_t offset = 0;
    uint32_t current = 0;
    while (offset < directory.size) {
        const uint8_t* record = nullptr;
        uint32_t nextOffset = 0;
        if (!readRecord(directory, offset, block, record, nextOffset)) return -1;
        offset = nextOffset;
        const uint8_t length = record[32];
        if (length == 1 && (record[33] == 0 || record[33] == 1)) continue;
        if (current++ != index) continue;
        entry.directory = (record[25] & 2U) != 0;
        uint32_t outputLength = 0;
        while (outputLength < length && outputLength + 1 < sizeof(entry.name) &&
               record[33 + outputLength] != ';') {
            entry.name[outputLength] = lower(record[33 + outputLength]);
            ++outputLength;
        }
        entry.name[outputLength] = '\0';
        return 0;
    }
    return 1;
}

}  // namespace fs::iso9660
