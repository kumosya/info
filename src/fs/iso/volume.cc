#include "service/fs/iso/filesystem.h"

namespace fs::iso {

    constexpr uint32_t isoBlockSize = 2048;

    namespace iso_names {

        static uint32_t readLe32(const uint8_t* value);
        static char lower(char value);
        static bool sameName(const uint8_t* raw, uint32_t rawLength,
                             const char* name, size_t nameLength);
    }  // namespace iso_names

    uint32_t iso_names::readLe32(const uint8_t* value) {
        return static_cast<uint32_t>(value[0]) |
               (static_cast<uint32_t>(value[1]) << 8) |
               (static_cast<uint32_t>(value[2]) << 16) |
               (static_cast<uint32_t>(value[3]) << 24);
    }

    char iso_names::lower(char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32)
                                            : value;
    }

    bool iso_names::sameName(const uint8_t* raw, uint32_t rawLength,
                             const char* name, size_t nameLength) {
        uint32_t length = rawLength;
        for (uint32_t index = 0; index < rawLength; ++index) {
            if (raw[index] == ';') {
                length = index;
                break;
            }
        }

        if (length > 0 && raw[length - 1] == '.') {
            --length;
        }

        if (length != nameLength) {
            return false;
        }

        for (uint32_t index = 0; index < length; ++index) {
            if (iso_names::lower(static_cast<char>(raw[index])) !=
                iso_names::lower(name[index])) {
                return false;
            }
        }

        return true;
    }

    bool Filesystem::mount(fs::SectorDevice& device) {
        device_ = &device;
        uint8_t block[isoBlockSize]{};
        if (!readBlock(16, block) || block[0] != 1 || block[6] != 1 ||
            block[1] != 'C' || block[2] != 'D' || block[3] != '0' ||
            block[4] != '0' || block[5] != '1') {
            return false;
        }

        const uint8_t* root = block + 156;
        if (root[0] < 34 || (root[25] & 2U) == 0) {
            return false;
        }

        root_ = {iso_names::readLe32(root + 2), iso_names::readLe32(root + 10),
                 true};
        return root_.extent != 0 && root_.size != 0;
    }

    bool Filesystem::readBlock(uint32_t block, void* buffer) {
        return device_ != nullptr &&
               device_->readSectors(static_cast<uint64_t>(block) * 4, 4,
                                    buffer);
    }

    Filesystem::RecordResult Filesystem::readRecord(const Node& directory,
                                                    uint32_t offset,
                                                    uint8_t* block,
                                                    const uint8_t*& record,
                                                    uint32_t& nextOffset) {
        while (offset < directory.size) {
            const uint32_t blockIndex = offset / isoBlockSize;
            const uint32_t blockOffset = offset % isoBlockSize;
            if (!readBlock(directory.extent + blockIndex, block)) {
                return RecordResult::Error;
            }

            const uint8_t length = block[blockOffset];
            if (length == 0) {
                offset = (blockIndex + 1) * isoBlockSize;
                continue;
            }

            if (length < 34 || blockOffset + length > isoBlockSize ||
                length > directory.size - offset ||
                block[blockOffset + 32] > length - 33) {
                return RecordResult::Error;
            }

            record = block + blockOffset;
            nextOffset = offset + length;
            return RecordResult::Entry;
        }

        return RecordResult::End;
    }

    bool Filesystem::findInDirectory(const Node& directory, const char* name,
                                     size_t nameLength, Node& status) {
        uint8_t block[isoBlockSize]{};
        uint32_t offset = 0;
        while (offset < directory.size) {
            const uint8_t* record = nullptr;
            uint32_t nextOffset = 0;
            if (readRecord(directory, offset, block, record, nextOffset) !=
                RecordResult::Entry) {
                return false;
            }

            offset = nextOffset;
            const uint8_t length = record[32];
            if (length == 1 && (record[33] == 0 || record[33] == 1)) {
                continue;
            }

            if (iso_names::sameName(record + 33, length, name, nameLength)) {
                status = {iso_names::readLe32(record + 2),
                          iso_names::readLe32(record + 10),
                          (record[25] & 2U) != 0};
                return true;
            }
        }

        return false;
    }

    bool Filesystem::lookup(const char* path, fs::FileStatus& status) {
        Node node{};
        if (!resolve(path, node)) {
            return false;
        }

        status.inode = node.extent;
        status.mode =
            node.directory ? fs::modeDirectory | 0555 : fs::modeRegular | 0444;
        status.size = node.size;
        status.links = node.directory ? 2 : 1;
        return true;
    }

    bool Filesystem::resolve(const char* path, Node& status) {
        if (device_ == nullptr || path == nullptr || path[0] != '/') {
            return false;
        }

        status = root_;
        uint32_t position = 1;
        while (path[position] != '\0') {
            while (path[position] == '/') {
                ++position;
            }

            if (path[position] == '\0') {
                break;
            }

            const uint32_t start = position;
            while (path[position] != '\0' && path[position] != '/') {
                ++position;
            }

            Node child{};
            if (!status.directory ||
                !findInDirectory(status, path + start, position - start,
                                 child)) {
                return false;
            }

            status = child;
        }

        return true;
    }

    int64_t Filesystem::read(const fs::FileStatus& status, uint64_t offset,
                             void* buffer, size_t size) {
        if ((status.mode & 0xf000U) == fs::modeDirectory || buffer == nullptr ||
            offset >= status.size) {
            return offset == status.size ? 0 : -1;
        }

        if (size > status.size - offset) {
            size = status.size - offset;
        }

        uint8_t block[isoBlockSize]{};
        size_t copied = 0;
        while (copied < size) {
            const uint64_t current = offset + copied;
            if (!readBlock(status.inode + current / isoBlockSize, block)) {
                return -1;
            }

            const uint32_t within =
                static_cast<uint32_t>(current % isoBlockSize);
            size_t chunk = isoBlockSize - within;
            if (chunk > size - copied) {
                chunk = size - copied;
            }

            for (size_t index = 0; index < chunk; ++index) {
                static_cast<uint8_t*>(buffer)[copied + index] =
                    block[within + index];
            }

            copied += chunk;
        }

        return static_cast<int64_t>(copied);
    }

    int Filesystem::readDirectory(const char* path, uint32_t index,
                                  fs::DirectoryEntry& entry) {
        Node directory{};
        if (!resolve(path, directory) || !directory.directory) {
            return -1;
        }

        uint8_t block[isoBlockSize]{};
        uint32_t offset = 0;
        uint32_t current = 0;
        while (offset < directory.size) {
            const uint8_t* record = nullptr;
            uint32_t nextOffset = 0;
            const RecordResult result =
                readRecord(directory, offset, block, record, nextOffset);
            if (result == RecordResult::End) {
                return 1;
            }

            if (result == RecordResult::Error) {
                return -1;
            }

            offset = nextOffset;
            const uint8_t length = record[32];
            if (length == 1 && (record[33] == 0 || record[33] == 1)) {
                continue;
            }

            if (current++ != index) {
                continue;
            }

            entry.type = (record[25] & 2U) != 0 ? 2 : 1;
            uint32_t outputLength = 0;
            while (outputLength < length &&
                   outputLength + 1 < sizeof(entry.name) &&
                   record[33 + outputLength] != ';') {
                entry.name[outputLength] =
                    iso_names::lower(record[33 + outputLength]);
                ++outputLength;
            }

            if (outputLength != 0 && entry.name[outputLength - 1] == '.') {
                --outputLength;
            }

            entry.name[outputLength] = '\0';
            return 0;
        }

        return 1;
    }

}  // namespace fs::iso
