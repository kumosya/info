#include "../ext3.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext3 {

using namespace detail;

bool Filesystem::lookup(const char* path, FileStatus& status) {
    Inode inode{};
    uint32_t number = 0;
    if (!resolve(path, number, &inode)) return false;
    status = {number, inode.mode, inode.size, inode.uid, inode.gid,
              inode.linksCount, inode.accessTime, inode.modificationTime,
              inode.changeTime};
    return true;
}

int64_t Filesystem::read(const FileStatus& status, uint64_t offset,
                         void* buffer, size_t size) {
    return read(status.inode, offset, buffer, size);
}

int64_t Filesystem::write(const FileStatus& status, uint64_t offset,
                          const void* buffer, size_t size) {
    return write(status.inode, offset, buffer, size);
}

int64_t Filesystem::read(uint32_t number, uint64_t offset, void* buffer,
                     size_t size) {
    Inode inode{};
    if (buffer == nullptr || !readInode(number, inode)) return -1;
    if (offset >= inode.size) return 0;
    if (size > inode.size - offset) size = inode.size - offset;
    uint8_t blockData[maximumBlockSize]{};
    size_t copied = 0;
    while (copied < size) {
        const uint64_t position = offset + copied;
        const uint32_t logical  = static_cast<uint32_t>(position / blockSize_);
        const uint32_t within   = static_cast<uint32_t>(position % blockSize_);
        const uint32_t block    = getDataBlock(inode, logical);
        if (block == 0) {
            os::clearMemory(blockData, blockSize_);
        } else if (!readBlock(block, blockData)) {
            return -1;
        }
        size_t count = blockSize_ - within;
        if (count > size - copied) count = size - copied;
        os::copyBytes(static_cast<uint8_t*>(buffer) + copied,
                      blockData + within, count);
        copied += count;
        prefetchAhead(number, inode, logical);
    }
    const uint32_t now = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    if (!device_->readOnly() && now > inode.accessTime + 3600) {
        inode.accessTime = now;
        writeInode(number, inode);
    }
    return static_cast<int64_t>(copied);
}

int64_t Filesystem::write(uint32_t number, uint64_t offset, const void* buffer,
                      size_t size) {
    Inode inode{};
    if (buffer == nullptr || device_->readOnly() || !readInode(number, inode) ||
        (inode.mode & 0xf000U) != modeRegular || offset + size > UINT32_MAX)
        return -1;
    const uint64_t pointers = blockSize_ / sizeof(uint32_t);
    const uint64_t maximumBlocks =
        directBlockCount + pointers + pointers * pointers +
        pointers * pointers * pointers;
    if (size != 0 && (offset + size - 1) / blockSize_ >= maximumBlocks)
        return -1;
    uint8_t blockData[maximumBlockSize]{};
    size_t copied = 0;
    while (copied < size) {
        const uint64_t position = offset + copied;
        const uint32_t logical  = static_cast<uint32_t>(position / blockSize_);
        const uint32_t within   = static_cast<uint32_t>(position % blockSize_);
        const uint32_t block    = ensureDataBlock(inode, logical);
        if (block == 0 || !readBlock(block, blockData)) return -1;
        size_t count = blockSize_ - within;
        if (count > size - copied) count = size - copied;
        os::copyBytes(blockData + within,
                      static_cast<const uint8_t*>(buffer) + copied, count);
        if (!writeBlock(block, blockData)) return -1;
        copied += count;
    }
    if (offset + size > inode.size)
        inode.size = static_cast<uint32_t>(offset + size);
    const uint32_t now = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    inode.modificationTime = now;
    inode.changeTime = now;
    return writeInode(number, inode) && sync() ? static_cast<int64_t>(copied)
                                               : -1;
}

bool Filesystem::truncate(uint32_t number) {
    Inode inode{};
    if (!readInode(number, inode) || !clearInodeBlocks(inode)) return false;
    const uint32_t now = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    inode.modificationTime = now;
    inode.changeTime = now;
    return writeInode(number, inode) && sync();
}

bool Filesystem::create(const char* path, bool directory) {
    FileStatus existing{};
    if (lookup(path, existing)) return false;
    uint32_t parentNumber = 0;
    Inode parent{};
    char name[256]{};
    if (!resolveParent(path, parentNumber, parent, name, sizeof(name)))
        return false;
    const uint32_t preferredGroup =
        (parentNumber - 1) / superblock_.inodesPerGroup;
    const uint32_t number = allocateInode(directory, preferredGroup);
    if (number == 0) return false;
    Inode inode{};
    inode.mode = static_cast<uint16_t>(
        (directory ? modeDirectory : modeRegular) | (directory ? 0755 : 0644));
    inode.linksCount = directory ? 2 : 1;
    const uint32_t now = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    inode.accessTime = now;
    inode.changeTime = now;
    inode.modificationTime = now;
    if (directory) {
        const uint32_t block = ensureDataBlock(inode, 0);
        uint8_t data[maximumBlockSize]{};
        if (block == 0) {
            freeInode(number, true);
            return false;
        }
        auto* dot         = reinterpret_cast<RawDirectoryEntry*>(data);
        dot->inode        = number;
        dot->recordLength = alignedEntrySize(1);
        dot->nameLength   = 1;
        dot->fileType     = 2;
        reinterpret_cast<char*>(dot + 1)[0] = '.';
        auto* dotdot =
            reinterpret_cast<RawDirectoryEntry*>(data + dot->recordLength);
        dotdot->inode                          = parentNumber;
        dotdot->recordLength                   = blockSize_ - dot->recordLength;
        dotdot->nameLength                     = 2;
        dotdot->fileType                       = 2;
        reinterpret_cast<char*>(dotdot + 1)[0] = '.';
        reinterpret_cast<char*>(dotdot + 1)[1] = '.';
        inode.size                             = blockSize_;
        if (!writeBlock(block, data)) {
            clearInodeBlocks(inode);
            freeInode(number, true);
            return false;
        }
        ++parent.linksCount;
    }
    if (!writeInode(number, inode) ||
        !addDirectoryEntry(parentNumber, parent, number, name,
                           directory ? 2 : 1)) {
        clearInodeBlocks(inode);
        freeInode(number, directory);
        return false;
    }
    return sync();
}

bool Filesystem::remove(const char* path) {
    uint32_t number = 0;
    Inode inode{};
    if (!resolve(path, number, &inode) || number == rootInode) return false;
    const bool directory = (inode.mode & 0xf000U) == modeDirectory;
    if (directory && !directoryEmpty(inode)) return false;
    uint32_t parentNumber = 0;
    Inode parent{};
    char name[256]{};
    if (!resolveParent(path, parentNumber, parent, name, sizeof(name)) ||
        !removeDirectoryEntry(parentNumber, parent, name))
        return false;
    if (directory && parent.linksCount != 0) {
        --parent.linksCount;
        writeInode(parentNumber, parent);
    }
    if (!directory && inode.linksCount > 1) {
        --inode.linksCount;
        inode.changeTime = static_cast<uint32_t>(
            os::getWallClockTime() / 1000000000ULL);
        return writeInode(number, inode) && sync();
    }
    clearInodeBlocks(inode);
    inode = {};
    inode.deletionTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    if (!writeInode(number, inode)) return false;
    freeInode(number, directory);
    return sync();
}

bool Filesystem::rename(const char* existing, const char* newPath) {
    if (readOnly()) return false;
    Inode source{}, from{}, to{};
    uint32_t number = 0, fromNumber = 0, toNumber = 0;
    char oldName[256]{}, newName[256]{};
    if (!resolve(existing, number, &source) || number == rootInode ||
        !resolveParent(existing, fromNumber, from, oldName, sizeof(oldName)) ||
        !resolveParent(newPath, toNumber, to, newName, sizeof(newName))) return false;
    const bool directory = (source.mode & 0xf000) == modeDirectory;
    if (directory) {
        uint32_t ancestor = toNumber;
        for (uint32_t visited = 0; visited < superblock_.inodesCount; ++visited) {
            if (ancestor == number) return false;
            if (ancestor == rootInode) break;
            Inode inode{}; uint32_t parent = 0;
            if (!readInode(ancestor, inode) || !lookupInDirectory(inode, "..", 2, parent) || parent == ancestor)
                return false;
            ancestor = parent;
            if (visited + 1 == superblock_.inodesCount) return false;
        }
    }
    FileStatus target{};
    if (lookup(newPath, target)) {
        if (target.inode == number) return true;
        if (((target.mode & 0xf000) == modeDirectory) != directory || !remove(newPath)) return false;
        if (!readInode(toNumber, to) || !readInode(fromNumber, from)) return false;
    }
    if (!addDirectoryEntry(toNumber, to, number, newName, directory ? 2 : 1)) return false;
    if (!readInode(fromNumber, from)) return false;
    if (!removeDirectoryEntry(fromNumber, from, oldName)) {
        removeDirectoryEntry(toNumber, to, newName);
        return false;
    }
    if (directory && fromNumber != toNumber) {
        uint8_t data[4096]{};
        const uint32_t block = getDataBlock(source, 0);
        if (!block || !readBlock(block, data)) return false;
        bool updated = false;
        for (uint32_t offset = 0; offset + 8 <= blockSize_;) {
            auto* entry = reinterpret_cast<RawDirectoryEntry*>(data + offset);
            if (entry->recordLength < 8 || entry->recordLength > blockSize_ - offset) return false;
            const char* name = reinterpret_cast<const char*>(entry + 1);
            if (entry->nameLength == 2 && name[0] == '.' && name[1] == '.') {
                entry->inode = toNumber; updated = true; break;
            }
            offset += entry->recordLength;
        }
        if (!updated || !writeBlock(block, data)) return false;
        if (from.linksCount) --from.linksCount;
        ++to.linksCount;
        if (!writeInode(fromNumber, from) || !writeInode(toNumber, to)) return false;
    }
    return sync();
}

bool Filesystem::link(const char* existing, const char* newPath) {
    uint32_t number = 0;
    Inode inode{};
    FileStatus duplicate{};
    if (device_->readOnly() || !resolve(existing, number, &inode) ||
        (inode.mode & 0xf000U) == modeDirectory || lookup(newPath, duplicate) ||
        inode.linksCount == 0xffffU) {
        return false;
    }
    uint32_t parentNumber = 0;
    Inode parent{};
    char name[256]{};
    if (!resolveParent(newPath, parentNumber, parent, name, sizeof(name)) ||
        !addDirectoryEntry(parentNumber, parent, number, name, 1)) {
        return false;
    }
    ++inode.linksCount;
    inode.changeTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    if (!writeInode(number, inode)) {
        removeDirectoryEntry(parentNumber, parent, name);
        return false;
    }
    return sync();
}

bool Filesystem::changeMode(const char* path, uint16_t mode) {
    uint32_t number = 0;
    Inode inode{};
    if (device_->readOnly() || !resolve(path, number, &inode)) return false;
    inode.mode = static_cast<uint16_t>((inode.mode & 0xf000U) | (mode & 07777U));
    inode.changeTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    return writeInode(number, inode) && sync();
}

bool Filesystem::changeOwner(const char* path, uint16_t uid, uint16_t gid) {
    uint32_t number = 0;
    Inode inode{};
    if (device_->readOnly() || !resolve(path, number, &inode)) return false;
    inode.uid = uid;
    inode.gid = gid;
    inode.changeTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    return writeInode(number, inode) && sync();
}

bool Filesystem::changeTimes(const char* path, uint32_t accessTime,
                         uint32_t modificationTime) {
    uint32_t number = 0;
    Inode inode{};
    if (device_->readOnly() || !resolve(path, number, &inode)) return false;
    inode.accessTime = accessTime;
    inode.modificationTime = modificationTime;
    inode.changeTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    return writeInode(number, inode) && sync();
}

}  // namespace fs::ext3
