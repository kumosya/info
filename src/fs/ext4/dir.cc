#include "service/common/runtime.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/ext4/internal.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>

namespace fs::ext4 {

using namespace detail;

bool Filesystem::lookupInDirectory(const uint8_t* directory, const char* name,
                                   size_t nameLength, uint32_t& inodeNumber) {
    if ((DiskEncoding::read16(directory) & 0xf000U) != modeDirectory) return false;
    uint64_t directorySize = DiskEncoding::read32(directory + 4);
    const uint32_t blockCount =
        static_cast<uint32_t>((directorySize + blockSize_ - 1) / blockSize_);
    for (uint32_t logical = 0; logical < blockCount; ++logical) {
        const uint64_t physical = getDataBlock(directory, logical);
        if (physical == 0 || !readBlock(physical, blockBuffer_)) return false;
        uint32_t offset = 0;
        while (offset + 8 <= blockSize_) {
            const uint8_t* entry          = blockBuffer_ + offset;
            const uint32_t inode          = DiskEncoding::read32(entry);
            const uint16_t recordLength   = DiskEncoding::read16(entry + 4);
            const uint8_t entryNameLength = entry[6];
            if (recordLength < 8 || offset + recordLength > blockSize_ ||
                entryNameLength > recordLength - 8) {
                return false;
            }
            if (inode != 0 && entryNameLength == nameLength &&
                DiskEncoding::namesEqual(reinterpret_cast<const char*>(entry + 8), name,
                           nameLength)) {
                inodeNumber = inode;
                return true;
            }
            offset += recordLength;
        }
    }
    return false;
}

bool Filesystem::resolve(const char* path, uint32_t& number, uint8_t* inode) {
    if (path == nullptr || path[0] != '/' || inode == nullptr) return false;
    number           = rootInode;
    uint8_t* current = inodeBuffer_;
    if (!readInode(number, current)) return false;
    size_t position = 1;
    while (path[position] != '\0') {
        while (path[position] == '/') ++position;
        if (path[position] == '\0') break;
        const size_t start = position;
        while (path[position] != '\0' && path[position] != '/') ++position;
        if ((DiskEncoding::read16(current) & 0xf000U) != modeDirectory ||
            !lookupInDirectory(current, path + start, position - start,
                               number) ||
            !readInode(number, current)) {
            return false;
        }
    }
    os::Runtime::copyBytes(inode, current, inodeSize_);
    return true;
}

bool Filesystem::resolveParent(const char* path, uint32_t& parentNumber,
                               uint8_t* parent, char* name, size_t capacity) {
    if (path == nullptr || path[0] != '/' || parent == nullptr ||
        name == nullptr || capacity < 2) {
        return false;
    }
    size_t length = 0;
    while (path[length] != '\0') ++length;
    while (length > 1 && path[length - 1] == '/') --length;
    size_t start = length;
    while (start > 0 && path[start - 1] != '/') --start;
    const size_t nameLength = length - start;
    if (nameLength == 0 || nameLength >= capacity ||
        (nameLength == 1 && path[start] == '.') ||
        (nameLength == 2 && path[start] == '.' && path[start + 1] == '.')) {
        return false;
    }
    os::Runtime::copyBytes(name, path + start, nameLength);
    name[nameLength] = '\0';
    char parentPath[256]{};
    if (start <= 1) {
        parentPath[0] = '/';
        parentPath[1] = '\0';
    } else {
        const size_t parentLength = start - 1;
        if (parentLength >= sizeof(parentPath)) return false;
        os::Runtime::copyBytes(parentPath, path, parentLength);
        parentPath[parentLength] = '\0';
    }
    return resolve(parentPath, parentNumber, parent) &&
           (DiskEncoding::read16(parent) & 0xf000U) == modeDirectory;
}

bool Filesystem::addDirectoryEntry(uint32_t directoryNumber, uint8_t* directory,
                                   uint32_t child, const char* name,
                                   uint8_t type) {
    const size_t nameLength = [&name]() {
        size_t length = 0;
        while (name[length] != '\0') ++length;
        return length;
    }();
    if (nameLength == 0 || nameLength > 255) return false;
    const uint16_t required =
        DiskEncoding::alignedDirectoryEntry(static_cast<uint32_t>(8 + nameLength));
    uint64_t directorySize = DiskEncoding::read32(directory + 4);
    uint32_t blockCount =
        static_cast<uint32_t>((directorySize + blockSize_ - 1) / blockSize_);
    for (uint32_t logical = 0; logical < blockCount; ++logical) {
        const uint64_t physical = getDataBlock(directory, logical);
        if (physical == 0 || !readBlock(physical, blockBuffer_)) return false;
        uint32_t offset = 0;
        while (offset + 8 <= blockSize_) {
            uint8_t* entry              = blockBuffer_ + offset;
            const uint16_t recordLength = DiskEncoding::read16(entry + 4);
            if (recordLength < 8 || offset + recordLength > blockSize_) {
                return false;
            }
            const uint32_t used =
                DiskEncoding::read32(entry) == 0 ? 0
                                   : DiskEncoding::alignedDirectoryEntry(
                                         8 + static_cast<uint32_t>(entry[6]));
            if ((DiskEncoding::read32(entry) == 0 && recordLength >= required) ||
                (DiskEncoding::read32(entry) != 0 && recordLength - used >= required)) {
                if (DiskEncoding::read32(entry) != 0) {
                    DiskEncoding::write16(entry + 4, static_cast<uint16_t>(used));
                    offset += used;
                    entry = blockBuffer_ + offset;
                    DiskEncoding::write16(entry + 4,
                            static_cast<uint16_t>(recordLength - used));
                } else {
                    os::Runtime::clearMemory(entry, recordLength);
                    DiskEncoding::write16(entry + 4, recordLength);
                }
                DiskEncoding::write32(entry, child);
                entry[6] = static_cast<uint8_t>(nameLength);
                entry[7] = type;
                os::Runtime::copyBytes(entry + 8, name, nameLength);
                return writeBlock(physical, blockBuffer_);
            }
            offset += recordLength;
        }
    }

    const uint32_t logical  = blockCount;
    const uint64_t physical = ensureDataBlock(directory, logical);
    if (physical == 0) return false;
    os::Runtime::clearMemory(blockBuffer_, blockSize_);
    DiskEncoding::write32(blockBuffer_, child);
    DiskEncoding::write16(blockBuffer_ + 4, static_cast<uint16_t>(blockSize_));
    blockBuffer_[6] = static_cast<uint8_t>(nameLength);
    blockBuffer_[7] = type;
    os::Runtime::copyBytes(blockBuffer_ + 8, name, nameLength);
    if (!writeBlock(physical, blockBuffer_)) return false;
    directorySize += blockSize_;
    DiskEncoding::write32(directory + 4, static_cast<uint32_t>(directorySize));
    return writeInode(directoryNumber, directory);
}

bool Filesystem::removeDirectoryEntry(const uint8_t* directory, const char* name) {
    size_t length = 0;
    while (name[length]) ++length;
    const uint32_t blocks = (DiskEncoding::read32(directory + 4) + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint64_t physical = getDataBlock(directory, logical);
        if (!physical || !readBlock(physical, blockBuffer_)) return false;
        uint8_t* previous = nullptr;
        for (uint32_t offset = 0; offset + 8 <= blockSize_;) {
            uint8_t* entry = blockBuffer_ + offset;
            const uint16_t record = DiskEncoding::read16(entry + 4);
            if (record < 8 || (record & 3) || record > blockSize_ - offset || entry[6] > record - 8)
                return false;
            if (DiskEncoding::read32(entry) && entry[6] == length &&
                DiskEncoding::namesEqual(reinterpret_cast<const char*>(entry + 8), name, length)) {
                if (previous) DiskEncoding::write16(previous + 4, DiskEncoding::read16(previous + 4) + record);
                else DiskEncoding::write32(entry, 0);
                return writeBlock(physical, blockBuffer_);
            }
            previous = entry;
            offset += record;
        }
    }
    return false;
}

bool Filesystem::directoryEmpty(const uint8_t* directory) {
    const uint32_t blocks = (DiskEncoding::read32(directory + 4) + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint64_t physical = getDataBlock(directory, logical);
        if (!physical || !readBlock(physical, blockBuffer_)) return false;
        for (uint32_t offset = 0; offset + 8 <= blockSize_;) {
            const uint8_t* entry = blockBuffer_ + offset;
            const uint16_t record = DiskEncoding::read16(entry + 4);
            if (record < 8 || (record & 3) || record > blockSize_ - offset || entry[6] > record - 8)
                return false;
            const bool dot = entry[6] == 1 && entry[8] == '.';
            const bool parent = entry[6] == 2 && entry[8] == '.' && entry[9] == '.';
            if (DiskEncoding::read32(entry) && !dot && !parent) return false;
            offset += record;
        }
    }
    return true;
}

int Filesystem::readDirectory(const char* path, uint32_t index,
                              DirectoryEntry& entry) {
    const int result = readDirectoryBatch(path, index, &entry, 1);
    return result < 0 ? -1 : (result == 0 ? 1 : 0);
}

int Filesystem::readDirectoryBatch(const char* path, uint32_t index,
                                   DirectoryEntry* entries, uint32_t capacity) {
    if (entries == nullptr || capacity == 0) return -1;
    uint32_t number = 0;
    if (!resolve(path, number, inodeBuffer_) ||
        (DiskEncoding::read16(inodeBuffer_) & 0xf000U) != modeDirectory) {
        return -1;
    }
    const uint64_t directorySize = DiskEncoding::read32(inodeBuffer_ + 4);
    const uint32_t blockCount =
        static_cast<uint32_t>((directorySize + blockSize_ - 1) / blockSize_);
    uint32_t visible = 0;
    uint32_t count   = 0;
    for (uint32_t logical = 0; logical < blockCount; ++logical) {
        const uint64_t physical = getDataBlock(inodeBuffer_, logical);
        if (physical == 0 || !readBlock(physical, blockBuffer_)) return -1;
        uint32_t offset = 0;
        while (offset + 8 <= blockSize_) {
            const uint8_t* raw          = blockBuffer_ + offset;
            const uint32_t inode        = DiskEncoding::read32(raw);
            const uint16_t recordLength = DiskEncoding::read16(raw + 4);
            const uint8_t nameLength    = raw[6];
            if (recordLength < 8 || offset + recordLength > blockSize_ ||
                nameLength > recordLength - 8) {
                return -1;
            }
            const char* name = reinterpret_cast<const char*>(raw + 8);
            const bool dot   = nameLength == 1 && name[0] == '.';
            const bool dotdot =
                nameLength == 2 && name[0] == '.' && name[1] == '.';
            if (inode != 0 && !dot && !dotdot && visible++ >= index) {
                DirectoryEntry& output  = entries[count++];
                output                  = {};
                output.inode            = inode;
                output.type             = raw[7];
                const size_t copyLength = nameLength < sizeof(output.name) - 1
                                              ? nameLength
                                              : sizeof(output.name) - 1;
                os::Runtime::copyBytes(output.name, name, copyLength);
                if (count == capacity) return static_cast<int>(count);
            }
            offset += recordLength;
        }
    }
    return static_cast<int>(count);
}

}  // namespace fs::ext4
