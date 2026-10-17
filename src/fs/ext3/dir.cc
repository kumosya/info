#include "../ext3.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext3 {

using namespace detail;

bool Filesystem::lookupInDirectory(const Inode& directory, const char* name,
                               size_t nameLength, uint32_t& inode) {
    uint8_t data[maximumBlockSize]{};
    const uint32_t blocks = (directory.size + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint32_t block = getDataBlock(directory, logical);
        if (block == 0 || !readBlock(block, data)) return false;
        uint32_t offset = 0;
        while (offset + sizeof(RawDirectoryEntry) <= blockSize_) {
            const auto* entry =
                reinterpret_cast<const RawDirectoryEntry*>(data + offset);
            if (entry->recordLength < sizeof(RawDirectoryEntry) ||
                offset + entry->recordLength > blockSize_)
                return false;
            const char* entryName = reinterpret_cast<const char*>(entry + 1);
            if (entry->inode != 0 && entry->nameLength == nameLength &&
                namesEqual(entryName, name, nameLength)) {
                inode = entry->inode;
                return true;
            }
            offset += entry->recordLength;
        }
    }
    return false;
}

bool Filesystem::resolve(const char* path, uint32_t& number, Inode* inode) {
    if (path == nullptr || path[0] != '/') return false;
    number = rootInode;
    Inode current{};
    if (!readInode(number, current)) return false;
    size_t position = 1;
    while (path[position] != '\0') {
        while (path[position] == '/') ++position;
        if (path[position] == '\0') break;
        const size_t start = position;
        while (path[position] != '\0' && path[position] != '/') ++position;
        if ((current.mode & 0xf000U) != modeDirectory ||
            !lookupInDirectory(current, path + start, position - start,
                               number) ||
            !readInode(number, current))
            return false;
    }
    if (inode != nullptr) *inode = current;
    return true;
}

bool Filesystem::resolveParent(const char* path, uint32_t& parentNumber,
                           Inode& parent, char* name, size_t capacity) {
    if (path == nullptr || path[0] != '/') return false;
    size_t length = 0;
    while (path[length] != '\0') ++length;
    while (length > 1 && path[length - 1] == '/') --length;
    size_t separator = length;
    while (separator > 0 && path[separator - 1] != '/') --separator;
    const size_t nameLength = length - separator;
    if (nameLength == 0 || nameLength >= capacity) return false;
    for (size_t index = 0; index < nameLength; ++index)
        name[index] = path[separator + index];
    name[nameLength] = '\0';
    char parentPath[256]{};
    if (separator <= 1) {
        parentPath[0] = '/';
    } else {
        for (size_t index = 0; index < separator - 1; ++index)
            parentPath[index] = path[index];
    }
    return resolve(parentPath, parentNumber, &parent) &&
           (parent.mode & 0xf000U) == modeDirectory;
}

bool Filesystem::addDirectoryEntry(uint32_t directoryNumber, Inode& directory,
                               uint32_t child, const char* name, uint8_t type) {
    size_t nameLength = 0;
    while (name[nameLength] != '\0') ++nameLength;
    if (nameLength == 0 || nameLength > 255) return false;
    const uint16_t needed = alignedEntrySize(nameLength);
    uint8_t data[maximumBlockSize]{};
    uint32_t blocks = (directory.size + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint32_t block = getDataBlock(directory, logical);
        if (block == 0 || !readBlock(block, data)) return false;
        uint32_t offset = 0;
        while (offset + sizeof(RawDirectoryEntry) <= blockSize_) {
            auto* entry = reinterpret_cast<RawDirectoryEntry*>(data + offset);
            if (entry->recordLength < sizeof(RawDirectoryEntry) ||
                offset + entry->recordLength > blockSize_)
                return false;
            const uint16_t used = alignedEntrySize(entry->nameLength);
            if (entry->inode == 0 && entry->recordLength >= needed) {
                entry->inode      = child;
                entry->nameLength = static_cast<uint8_t>(nameLength);
                entry->fileType   = type;
                char* target      = reinterpret_cast<char*>(entry + 1);
                for (size_t i = 0; i < nameLength; ++i) target[i] = name[i];
                return writeBlock(block, data) &&
                       writeInode(directoryNumber, directory);
            }
            if (entry->inode != 0 && entry->recordLength >= used + needed) {
                const uint16_t original = entry->recordLength;
                entry->recordLength     = used;
                auto* added =
                    reinterpret_cast<RawDirectoryEntry*>(data + offset + used);
                added->inode        = child;
                added->recordLength = original - used;
                added->nameLength   = static_cast<uint8_t>(nameLength);
                added->fileType     = type;
                char* target        = reinterpret_cast<char*>(added + 1);
                for (size_t i = 0; i < nameLength; ++i) target[i] = name[i];
                return writeBlock(block, data) &&
                       writeInode(directoryNumber, directory);
            }
            offset += entry->recordLength;
        }
    }
    const uint32_t block = ensureDataBlock(directory, blocks);
    if (block == 0) return false;
    os::clearMemory(data, blockSize_);
    auto* entry         = reinterpret_cast<RawDirectoryEntry*>(data);
    entry->inode        = child;
    entry->recordLength = static_cast<uint16_t>(blockSize_);
    entry->nameLength   = static_cast<uint8_t>(nameLength);
    entry->fileType     = type;
    char* target        = reinterpret_cast<char*>(entry + 1);
    for (size_t i = 0; i < nameLength; ++i) target[i] = name[i];
    directory.size += blockSize_;
    return writeBlock(block, data) && writeInode(directoryNumber, directory);
}

bool Filesystem::removeDirectoryEntry(uint32_t directoryNumber, Inode& directory,
                                  const char* name) {
    size_t nameLength = 0;
    while (name[nameLength] != '\0') ++nameLength;
    uint8_t data[maximumBlockSize]{};
    const uint32_t blocks = (directory.size + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint32_t block = getDataBlock(directory, logical);
        if (block == 0 || !readBlock(block, data)) return false;
        uint32_t offset             = 0;
        RawDirectoryEntry* previous = nullptr;
        while (offset + sizeof(RawDirectoryEntry) <= blockSize_) {
            auto* entry = reinterpret_cast<RawDirectoryEntry*>(data + offset);
            if (entry->recordLength < sizeof(RawDirectoryEntry) ||
                offset + entry->recordLength > blockSize_)
                return false;
            if (entry->inode != 0 && entry->nameLength == nameLength &&
                namesEqual(reinterpret_cast<char*>(entry + 1), name,
                           nameLength)) {
                if (previous != nullptr)
                    previous->recordLength += entry->recordLength;
                else
                    entry->inode = 0;
                return writeBlock(block, data) &&
                       writeInode(directoryNumber, directory);
            }
            if (entry->inode != 0) previous = entry;
            offset += entry->recordLength;
        }
    }
    return false;
}

bool Filesystem::directoryEmpty(const Inode& directory) {
    uint8_t data[maximumBlockSize]{};
    const uint32_t blocks = (directory.size + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint32_t block = getDataBlock(directory, logical);
        if (block == 0 || !readBlock(block, data)) return false;
        uint32_t offset = 0;
        while (offset + sizeof(RawDirectoryEntry) <= blockSize_) {
            const auto* raw =
                reinterpret_cast<const RawDirectoryEntry*>(data + offset);
            if (raw->recordLength < sizeof(RawDirectoryEntry) ||
                offset + raw->recordLength > blockSize_)
                return false;
            if (raw->inode != 0) {
                const char* name = reinterpret_cast<const char*>(raw + 1);
                const bool dot   = raw->nameLength == 1 && name[0] == '.';
                const bool dotdot =
                    raw->nameLength == 2 && name[0] == '.' && name[1] == '.';
                if (!dot && !dotdot) return false;
            }
            offset += raw->recordLength;
        }
    }
    return true;
}

int Filesystem::readDirectory(const char* path, uint32_t index,
                          DirectoryEntry& result) {
    const int count = readDirectoryBatch(path, index, &result, 1);
    return count < 0 ? -1 : (count == 0 ? 1 : 0);
}

int Filesystem::readDirectoryBatch(const char* path, uint32_t index,
                               DirectoryEntry* results, uint32_t capacity) {
    if (results == nullptr || capacity == 0) return -1;
    uint32_t number = 0;
    Inode directory{};
    if (!resolve(path, number, &directory) ||
        (directory.mode & 0xf000U) != modeDirectory)
        return -1;
    uint8_t data[maximumBlockSize]{};
    uint32_t visible      = 0;
    uint32_t count        = 0;
    const uint32_t blocks = (directory.size + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint32_t block = getDataBlock(directory, logical);
        if (block == 0 || !readBlock(block, data)) return -1;
        uint32_t offset = 0;
        while (offset + sizeof(RawDirectoryEntry) <= blockSize_) {
            const auto* raw =
                reinterpret_cast<const RawDirectoryEntry*>(data + offset);
            if (raw->recordLength < sizeof(RawDirectoryEntry) ||
                offset + raw->recordLength > blockSize_)
                return -1;
            const char* name = reinterpret_cast<const char*>(raw + 1);
            const bool dot   = raw->nameLength == 1 && name[0] == '.';
            const bool dotdot =
                raw->nameLength == 2 && name[0] == '.' && name[1] == '.';
            if (raw->inode != 0 && !dot && !dotdot && visible++ >= index) {
                DirectoryEntry& result = results[count++];
                result                 = {};
                result.inode           = raw->inode;
                result.type            = raw->fileType;
                size_t nameLength = raw->nameLength;
                if (nameLength >= sizeof(result.name))
                    nameLength = sizeof(result.name) - 1;
                for (size_t i = 0; i < nameLength; ++i)
                    result.name[i] = name[i];
                if (count == capacity) return static_cast<int>(count);
            }
            offset += raw->recordLength;
        }
    }
    return static_cast<int>(count);
}

}  // namespace fs::ext3
