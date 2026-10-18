#include "../ext4.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext4 {

using namespace detail;

bool Filesystem::lookup(const char* path, FileStatus& status) {
    uint32_t number = 0;
    if (!resolve(path, number, inodeBuffer_)) return false;
    const uint16_t mode = DiskEncoding::read16(inodeBuffer_);
    uint64_t size       = DiskEncoding::read32(inodeBuffer_ + 4);
    if ((mode & 0xf000U) == modeRegular) {
        size |= static_cast<uint64_t>(DiskEncoding::read32(inodeBuffer_ + 108)) << 32;
    }
    status = {number,
              mode,
              size,
              DiskEncoding::read16(inodeBuffer_ + 2),
              DiskEncoding::read16(inodeBuffer_ + 24),
              DiskEncoding::read16(inodeBuffer_ + 26),
              DiskEncoding::read32(inodeBuffer_ + 8),
              DiskEncoding::read32(inodeBuffer_ + 16),
              DiskEncoding::read32(inodeBuffer_ + 12)};
    return true;
}

int64_t Filesystem::read(const FileStatus& status, uint64_t offset,
                         void* buffer, size_t size) {
    return read(status.inode, offset, buffer, size);
}

int64_t Filesystem::read(uint32_t number, uint64_t offset, void* buffer,
                         size_t size) {
    if (buffer == nullptr || !readInode(number, inodeBuffer_)) return -1;
    const uint16_t mode = DiskEncoding::read16(inodeBuffer_);
    if ((mode & 0xf000U) != modeRegular && (mode & 0xf000U) != modeDirectory) {
        return -1;
    }
    uint64_t fileSize = DiskEncoding::read32(inodeBuffer_ + 4);
    if ((mode & 0xf000U) == modeRegular) {
        fileSize |= static_cast<uint64_t>(DiskEncoding::read32(inodeBuffer_ + 108)) << 32;
    }
    if (offset >= fileSize) return 0;
    if (size > fileSize - offset) size = static_cast<size_t>(fileSize - offset);

    size_t copied = 0;
    while (copied < size) {
        const uint64_t position  = offset + copied;
        const uint64_t logical64 = position / blockSize_;
        if (logical64 > UINT32_MAX) return -1;
        const uint32_t logical  = static_cast<uint32_t>(logical64);
        const uint32_t within   = static_cast<uint32_t>(position % blockSize_);
        const uint64_t physical = getDataBlock(inodeBuffer_, logical);
        if (physical == 0) {
            os::Runtime::clearMemory(blockBuffer_, blockSize_);
        } else if (!readBlock(physical, blockBuffer_)) {
            return -1;
        }
        size_t count = blockSize_ - within;
        if (count > size - copied) count = size - copied;
        os::Runtime::copyBytes(static_cast<uint8_t*>(buffer) + copied,
                      blockBuffer_ + within, count);
        copied += count;
    }
    return static_cast<int64_t>(copied);
}

int64_t Filesystem::write(const FileStatus& status, uint64_t offset,
                          const void* buffer, size_t size) {
    if (readOnly() || buffer == nullptr ||
        (status.mode & 0xf000U) != modeRegular || offset > UINT32_MAX ||
        size > UINT32_MAX - offset) {
        return -1;
    }
    uint8_t* inode = inodeBuffer_;
    if (!readInode(status.inode, inode) ||
        (DiskEncoding::read16(inode) & 0xf000U) != modeRegular) {
        return -1;
    }
    size_t copied = 0;
    while (copied < size) {
        const uint64_t position = offset + copied;
        const uint32_t logical  = static_cast<uint32_t>(position / blockSize_);
        const uint32_t within   = static_cast<uint32_t>(position % blockSize_);
        uint64_t physical       = getDataBlock(inode, logical);
        bool newBlock           = physical == 0;
        if (newBlock) physical = ensureDataBlock(inode, logical);
        if (physical == 0)
            return copied == 0 ? -1 : static_cast<int64_t>(copied);
        if (!newBlock && (within != 0 || size - copied < blockSize_) &&
            !readBlock(physical, blockBuffer_)) {
            return copied == 0 ? -1 : static_cast<int64_t>(copied);
        }
        if (newBlock) os::Runtime::clearMemory(blockBuffer_, blockSize_);
        size_t count = blockSize_ - within;
        if (count > size - copied) count = size - copied;
        os::Runtime::copyBytes(blockBuffer_ + within,
                      static_cast<const uint8_t*>(buffer) + copied, count);
        if (!writeBlock(physical, blockBuffer_)) {
            return copied == 0 ? -1 : static_cast<int64_t>(copied);
        }
        copied += count;
    }
    const uint64_t newSize = offset + copied;
    const uint64_t oldSize = static_cast<uint64_t>(DiskEncoding::read32(inode + 4)) |
                             (static_cast<uint64_t>(DiskEncoding::read32(inode + 108)) << 32);
    if (newSize > oldSize) {
        DiskEncoding::write32(inode + 4, static_cast<uint32_t>(newSize));
        DiskEncoding::write32(inode + 108, static_cast<uint32_t>(newSize >> 32));
    }
    if (!writeInode(status.inode, inode)) return -1;
    return static_cast<int64_t>(copied);
}

bool Filesystem::truncate(uint32_t number) {
    if (readOnly()) return false;
    uint8_t* inode = inodeBuffer_;
    if (!readInode(number, inode) || (DiskEncoding::read16(inode) & 0xf000U) != modeRegular ||
        !clearInodeBlocks(inode) || !writeInode(number, inode)) {
        return false;
    }
    return sync();
}

bool Filesystem::create(const char* path, bool directory) {
    if (readOnly()) return false;
    uint32_t existingNumber = 0;
    if (resolve(path, existingNumber, inodeBuffer_)) return false;

    uint32_t parentNumber = 0;
    uint8_t* parent       = inodeScratch_[0];
    char name[256]{};
    if (!resolveParent(path, parentNumber, parent, name, sizeof(name))) {
        return false;
    }
    const uint32_t group = (parentNumber - 1) / inodesPerGroup_;
    const uint32_t child = allocateInode(directory, group);
    if (child == 0) return false;
    uint8_t* inode = inodeScratch_[1];
    os::Runtime::clearMemory(inode, sizeof(inodeScratch_[1]));
    DiskEncoding::write16(inode, directory ? (modeDirectory | 0755) : (modeRegular | 0644));
    DiskEncoding::write16(inode + 2, 0);
    DiskEncoding::write16(inode + 24, 0);
    DiskEncoding::write16(inode + 26, 0);
    DiskEncoding::write16(inode + 26, directory ? 2 : 1);
    DiskEncoding::write16(inode + 40, 0xf30a);
    DiskEncoding::write16(inode + 44, 4);
    DiskEncoding::write16(inode + 46, 0);
    DiskEncoding::write32(inode + 32, 0x00080000U);

    if (directory) {
        const uint64_t dataBlock = ensureDataBlock(inode, 0);
        if (dataBlock == 0) {
            freeInode(child, true);
            return false;
        }
        os::Runtime::clearMemory(blockBuffer_, blockSize_);
        DiskEncoding::write32(blockBuffer_, child);
        DiskEncoding::write16(blockBuffer_ + 4, 12);
        blockBuffer_[6] = 1;
        blockBuffer_[7] = 2;
        blockBuffer_[8] = '.';
        DiskEncoding::write32(blockBuffer_ + 12, parentNumber);
        DiskEncoding::write16(blockBuffer_ + 16, static_cast<uint16_t>(blockSize_ - 12));
        blockBuffer_[18] = 2;
        blockBuffer_[19] = 2;
        blockBuffer_[20] = '.';
        blockBuffer_[21] = '.';
        DiskEncoding::write32(inode + 4, blockSize_);
        if (!writeBlock(dataBlock, blockBuffer_)) {
            freeBlock(static_cast<uint32_t>(dataBlock));
            freeInode(child, true);
            return false;
        }
    }
    if (!writeInode(child, inode)) {
        if (directory) clearInodeBlocks(inode);
        freeInode(child, directory);
        return false;
    }
    if (!addDirectoryEntry(parentNumber, parent, child, name,
                           directory ? 2 : 1)) {
        if (directory) clearInodeBlocks(inode);
        freeInode(child, directory);
        return false;
    }
    if (directory) {
        DiskEncoding::write16(parent + 26, static_cast<uint16_t>(DiskEncoding::read16(parent + 26) + 1));
        if (!writeInode(parentNumber, parent)) return false;
    }
    return sync();
}

bool Filesystem::remove(const char* path) {
    if (readOnly()) return false;
    uint32_t number = 0;
    uint8_t* inode = inodeScratch_[1];
    if (!resolve(path, number, inode) || number == rootInode) return false;
    const bool directory = (DiskEncoding::read16(inode) & 0xf000U) == modeDirectory;
    if (directory && !directoryEmpty(inode)) return false;
    // Only remove layouts that clearInodeBlocks can release completely.
    if (DiskEncoding::read16(inode + 40) != 0xf30a || DiskEncoding::read16(inode + 46) != 0 || DiskEncoding::read16(inode + 42) > 4)
        return false;
    uint32_t parentNumber = 0;
    uint8_t* parent = inodeScratch_[0];
    char name[256]{};
    if (!resolveParent(path, parentNumber, parent, name, sizeof(name)) ||
        !removeDirectoryEntry(parent, name)) return false;
    if (directory) {
        const uint16_t links = DiskEncoding::read16(parent + 26);
        if (links) DiskEncoding::write16(parent + 26, links - 1);
        if (!writeInode(parentNumber, parent)) return false;
    } else if (DiskEncoding::read16(inode + 26) > 1) {
        DiskEncoding::write16(inode + 26, DiskEncoding::read16(inode + 26) - 1);
        return writeInode(number, inode) && sync();
    }
    if (!clearInodeBlocks(inode)) return false;
    os::Runtime::clearMemory(inode, inodeSize_);
    DiskEncoding::write32(inode + 20, static_cast<uint32_t>(os::Clock::getWallClockTime() / 1000000000ULL));
    if (!writeInode(number, inode)) return false;
    freeInode(number, directory);
    return sync();
}

bool Filesystem::changeOwner(const char* path, uint16_t uid, uint16_t gid) {
    uint32_t number = 0;
    if (readOnly() || !resolve(path, number, inodeBuffer_)) return false;
    if (uid != 0xffff) DiskEncoding::write16(inodeBuffer_ + 2, uid);
    if (gid != 0xffff) DiskEncoding::write16(inodeBuffer_ + 24, gid);
    return writeInode(number, inodeBuffer_) && sync();
}

bool Filesystem::changeTimes(const char* path, uint32_t accessTime, uint32_t modificationTime) {
    uint32_t number = 0;
    if (readOnly() || !resolve(path, number, inodeBuffer_)) return false;
    DiskEncoding::write32(inodeBuffer_ + 8, accessTime);
    DiskEncoding::write32(inodeBuffer_ + 16, modificationTime);
    DiskEncoding::write32(inodeBuffer_ + 12, static_cast<uint32_t>(os::Clock::getWallClockTime() / 1000000000ULL));
    return writeInode(number, inodeBuffer_) && sync();
}

bool Filesystem::changeMode(const char* path, uint16_t mode) {
    if (readOnly()) return false;
    uint32_t number = 0;
    if (!resolve(path, number, inodeBuffer_)) return false;
    DiskEncoding::write16(inodeBuffer_, (DiskEncoding::read16(inodeBuffer_) & 0xf000U) | (mode & 0x0fffU));
    return writeInode(number, inodeBuffer_) && sync();
}

bool Filesystem::rename(const char* existing, const char* newPath) {
    if (readOnly()) return false;
    uint8_t source[4096]{}, from[4096]{}, to[4096]{};
    uint32_t number = 0, fromNumber = 0, toNumber = 0;
    char oldName[256]{}, newName[256]{};
    if (!resolve(existing, number, source) || number == rootInode ||
        !resolveParent(existing, fromNumber, from, oldName, sizeof(oldName)) ||
        !resolveParent(newPath, toNumber, to, newName, sizeof(newName))) return false;
    const bool directory = (DiskEncoding::read16(source) & 0xf000) == modeDirectory;
    if (directory) {
        uint32_t ancestor = toNumber;
        for (uint32_t visited = 0; visited < inodesCount_; ++visited) {
            if (ancestor == number) return false;
            if (ancestor == rootInode) break;
            uint32_t parent = 0;
            if (!readInode(ancestor, inodeBuffer_) || !lookupInDirectory(inodeBuffer_, "..", 2, parent) || parent == ancestor)
                return false;
            ancestor = parent;
            if (visited + 1 == inodesCount_) return false;
        }
    }
    FileStatus target{};
    if (lookup(newPath, target)) {
        if (target.inode == number) return true;
        if (((target.mode & 0xf000) == modeDirectory) != directory || !remove(newPath)) return false;
        if (!readInode(toNumber, to) || !readInode(fromNumber, from)) return false;
    }
    if (!addDirectoryEntry(toNumber, to, number, newName, directory ? 2 : 1)) return false;
    if (!removeDirectoryEntry(from, oldName)) {
        removeDirectoryEntry(to, newName);
        return false;
    }
    if (directory && fromNumber != toNumber) {
        const uint64_t block = getDataBlock(source, 0);
        if (!block || !readBlock(block, blockBuffer_)) return false;
        bool updated = false;
        for (uint32_t offset = 0; offset + 8 <= blockSize_;) {
            uint8_t* entry = blockBuffer_ + offset;
            const uint16_t length = DiskEncoding::read16(entry + 4);
            if (length < 8 || length > blockSize_ - offset) return false;
            if (entry[6] == 2 && entry[8] == '.' && entry[9] == '.') {
                DiskEncoding::write32(entry, toNumber); updated = true; break;
            }
            offset += length;
        }
        if (!updated || !writeBlock(block, blockBuffer_)) return false;
        if (DiskEncoding::read16(from + 26)) DiskEncoding::write16(from + 26, DiskEncoding::read16(from + 26) - 1);
        DiskEncoding::write16(to + 26, DiskEncoding::read16(to + 26) + 1);
        if (!writeInode(fromNumber, from) || !writeInode(toNumber, to)) return false;
    }
    return sync();
}

}  // namespace fs::ext4
