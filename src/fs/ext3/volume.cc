#include "../ext3.h"
#include "os/abi.h"

namespace fs::ext3 {

namespace {

constexpr uint32_t sectorSize          = 512;
constexpr uint32_t directBlockCount    = 12;
constexpr uint32_t singlyIndirectIndex = 12;
constexpr uint32_t doublyIndirectIndex = 13;
constexpr uint32_t triplyIndirectIndex = 14;
constexpr uint32_t maximumBlockSize    = 4096;

uint16_t alignedEntrySize(uint32_t nameLength) {
    return static_cast<uint16_t>((sizeof(RawDirectoryEntry) + nameLength + 3) &
                                 ~3U);
}

bool namesEqual(const char* left, const char* right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

}  // namespace

bool Volume::mount(SectorDevice& device) {
    device_ = &device;
    if (!readSuperblock() || superblock_.magic != superMagic ||
        (superblock_.compatibleFeatures & 0x4U) == 0 ||
        (superblock_.incompatibleFeatures & ~0x2U) != 0) {
        return false;
    }
    blockSize_ = 1024U << superblock_.logBlockSize;
    if (blockSize_ < 1024 || blockSize_ > maximumBlockSize ||
        blockSize_ % sectorSize != 0 || superblock_.blocksPerGroup == 0 ||
        superblock_.inodesPerGroup == 0 ||
        superblock_.inodeSize < sizeof(Inode) ||
        superblock_.inodeSize > blockSize_ ||
        (superblock_.inodeSize & (superblock_.inodeSize - 1)) != 0) {
        return false;
    }
    groupCount_ = (superblock_.blocksCount - superblock_.firstDataBlock +
                   superblock_.blocksPerGroup - 1) /
                  superblock_.blocksPerGroup;
    Inode root{};
    return groupCount_ != 0 && readInode(rootInode, root) &&
           (root.mode & 0xf000U) == modeDirectory;
}

bool Volume::readSuperblock() {
    uint8_t data[1024]{};
    if (device_ == nullptr || !device_->readSector(2, data) ||
        !device_->readSector(3, data + sectorSize)) {
        return false;
    }
    uint8_t* target = reinterpret_cast<uint8_t*>(&superblock_);
    for (uint32_t index = 0; index < sizeof(superblock_); ++index) {
        target[index] = data[index];
    }
    return true;
}

bool Volume::writeSuperblock() {
    if (device_ == nullptr || device_->readOnly()) return false;
    const uint8_t* source = reinterpret_cast<const uint8_t*>(&superblock_);
    return device_->writeSector(2, source) &&
           device_->writeSector(3, source + sectorSize);
}

bool Volume::readBlock(uint32_t block, void* buffer) {
    if (device_ == nullptr || buffer == nullptr ||
        block >= superblock_.blocksCount)
        return false;
    for (uint32_t slot = 0; slot < blockCacheSlotCount; ++slot) {
        if (!cacheValid_[slot] || cachedBlocks_[slot] != block) continue;
        for (uint32_t index = 0; index < blockSize_; ++index) {
            static_cast<uint8_t*>(buffer)[index] = blockCache_[slot][index];
        }
        cacheAges_[slot] = ++cacheClock_;
        return true;
    }
    uint32_t selected = 0;
    for (uint32_t slot = 0; slot < blockCacheSlotCount; ++slot) {
        if (!cacheValid_[slot]) {
            selected = slot;
            break;
        }
        if (cacheAges_[slot] < cacheAges_[selected]) selected = slot;
    }
    const uint32_t sectors = blockSize_ / sectorSize;
    if (!device_->readSectors(static_cast<uint64_t>(block) * sectors, sectors,
                              blockCache_[selected]))
        return false;
    for (uint32_t index = 0; index < blockSize_; ++index) {
        static_cast<uint8_t*>(buffer)[index] = blockCache_[selected][index];
    }
    cachedBlocks_[selected] = block;
    cacheAges_[selected]    = ++cacheClock_;
    cacheValid_[selected]   = true;
    return true;
}

bool Volume::writeBlock(uint32_t block, const void* buffer) {
    if (device_ == nullptr || device_->readOnly() || buffer == nullptr ||
        block >= superblock_.blocksCount)
        return false;
    const uint32_t sectors = blockSize_ / sectorSize;
    if (!device_->writeSectors(static_cast<uint64_t>(block) * sectors, sectors,
                               buffer))
        return false;
    for (uint32_t slot = 0; slot < blockCacheSlotCount; ++slot) {
        if (!cacheValid_[slot] || cachedBlocks_[slot] != block) continue;
        for (uint32_t index = 0; index < blockSize_; ++index) {
            blockCache_[slot][index] =
                static_cast<const uint8_t*>(buffer)[index];
        }
        cacheAges_[slot] = ++cacheClock_;
        break;
    }
    return true;
}

bool Volume::readGroup(uint32_t group, GroupDescriptor& descriptor) {
    if (group >= groupCount_) return false;
    const uint32_t tableBlock = superblock_.firstDataBlock + 1;
    const uint32_t offset     = group * sizeof(GroupDescriptor);
    uint8_t data[maximumBlockSize]{};
    if (!readBlock(tableBlock + offset / blockSize_, data)) return false;
    const uint8_t* source = data + offset % blockSize_;
    uint8_t* target       = reinterpret_cast<uint8_t*>(&descriptor);
    for (uint32_t index = 0; index < sizeof(descriptor); ++index)
        target[index] = source[index];
    return true;
}

bool Volume::writeGroup(uint32_t group, const GroupDescriptor& descriptor) {
    if (group >= groupCount_) return false;
    const uint32_t tableBlock = superblock_.firstDataBlock + 1;
    const uint32_t offset     = group * sizeof(GroupDescriptor);
    uint8_t data[maximumBlockSize]{};
    const uint32_t block = tableBlock + offset / blockSize_;
    if (!readBlock(block, data)) return false;
    const uint8_t* source = reinterpret_cast<const uint8_t*>(&descriptor);
    for (uint32_t index = 0; index < sizeof(descriptor); ++index)
        data[offset % blockSize_ + index] = source[index];
    return writeBlock(block, data);
}

bool Volume::readInode(uint32_t number, Inode& inode) {
    if (number == 0 || number > superblock_.inodesCount) return false;
    const uint32_t group = (number - 1) / superblock_.inodesPerGroup;
    const uint32_t local = (number - 1) % superblock_.inodesPerGroup;
    GroupDescriptor descriptor{};
    if (!readGroup(group, descriptor)) return false;
    const uint64_t offset =
        static_cast<uint64_t>(local) * superblock_.inodeSize;
    uint8_t data[maximumBlockSize]{};
    if (!readBlock(descriptor.inodeTable + offset / blockSize_, data))
        return false;
    const uint8_t* source = data + offset % blockSize_;
    uint8_t* target       = reinterpret_cast<uint8_t*>(&inode);
    for (uint32_t index = 0; index < sizeof(inode); ++index)
        target[index] = source[index];
    return true;
}

bool Volume::writeInode(uint32_t number, const Inode& inode) {
    if (number == 0 || number > superblock_.inodesCount) return false;
    const uint32_t group = (number - 1) / superblock_.inodesPerGroup;
    const uint32_t local = (number - 1) % superblock_.inodesPerGroup;
    GroupDescriptor descriptor{};
    if (!readGroup(group, descriptor)) return false;
    const uint64_t offset =
        static_cast<uint64_t>(local) * superblock_.inodeSize;
    const uint32_t block = descriptor.inodeTable + offset / blockSize_;
    uint8_t data[maximumBlockSize]{};
    if (!readBlock(block, data)) return false;
    const uint8_t* source = reinterpret_cast<const uint8_t*>(&inode);
    for (uint32_t index = 0; index < sizeof(inode); ++index)
        data[offset % blockSize_ + index] = source[index];
    return writeBlock(block, data);
}

bool Volume::readPointer(uint32_t block, uint32_t index, uint32_t& value) {
    if (index >= blockSize_ / sizeof(uint32_t)) return false;
    uint32_t data[maximumBlockSize / sizeof(uint32_t)]{};
    if (!readBlock(block, data)) return false;
    value = data[index];
    return true;
}

bool Volume::writePointer(uint32_t block, uint32_t index, uint32_t value) {
    if (index >= blockSize_ / sizeof(uint32_t)) return false;
    uint32_t data[maximumBlockSize / sizeof(uint32_t)]{};
    if (!readBlock(block, data)) return false;
    data[index] = value;
    return writeBlock(block, data);
}

uint32_t Volume::getDataBlock(const Inode& inode, uint32_t logical) {
    const uint32_t pointers = blockSize_ / sizeof(uint32_t);
    if (logical < directBlockCount) return inode.blocks[logical];
    logical -= directBlockCount;
    uint32_t value = 0;
    if (logical < pointers) {
        return inode.blocks[singlyIndirectIndex] != 0 &&
                       readPointer(inode.blocks[singlyIndirectIndex], logical,
                                   value)
                   ? value
                   : 0;
    }
    logical -= pointers;
    const uint64_t doubleCapacity =
        static_cast<uint64_t>(pointers) * pointers;
    if (logical < doubleCapacity) {
        if (inode.blocks[doublyIndirectIndex] == 0) return 0;
        uint32_t indirect = 0;
        return readPointer(inode.blocks[doublyIndirectIndex],
                           logical / pointers, indirect) &&
                       indirect != 0 &&
                       readPointer(indirect, logical % pointers, value)
                   ? value
                   : 0;
    }
    logical -= static_cast<uint32_t>(doubleCapacity);
    if (logical >= doubleCapacity * pointers ||
        inode.blocks[triplyIndirectIndex] == 0) {
        return 0;
    }
    uint32_t first = 0;
    uint32_t second = 0;
    return readPointer(inode.blocks[triplyIndirectIndex],
                       logical / doubleCapacity, first) &&
                   first != 0 &&
                   readPointer(first, (logical / pointers) % pointers,
                               second) &&
                   second != 0 &&
                   readPointer(second, logical % pointers, value)
               ? value
               : 0;
}

uint32_t Volume::ensureDataBlock(Inode& inode, uint32_t logical) {
    uint32_t existing = getDataBlock(inode, logical);
    if (existing != 0) return existing;
    const uint32_t pointers = blockSize_ / sizeof(uint32_t);
    uint32_t preferredGroup = 0;
    if (logical != 0) {
        const uint32_t previous = getDataBlock(inode, logical - 1);
        if (previous >= superblock_.firstDataBlock) {
            preferredGroup = (previous - superblock_.firstDataBlock) /
                             superblock_.blocksPerGroup;
        }
    }
    const uint32_t allocated = allocateBlock(preferredGroup);
    if (allocated == 0) return 0;
    if (logical < directBlockCount) {
        inode.blocks[logical] = allocated;
        inode.sectorsCount += blockSize_ / sectorSize;
        return allocated;
    }
    logical -= directBlockCount;
    if (logical < pointers) {
        if (inode.blocks[singlyIndirectIndex] == 0) {
            inode.blocks[singlyIndirectIndex] = allocateBlock(preferredGroup);
            if (inode.blocks[singlyIndirectIndex] == 0) {
                freeBlock(allocated);
                return 0;
            }
            inode.sectorsCount += blockSize_ / sectorSize;
        }
        if (!writePointer(inode.blocks[singlyIndirectIndex], logical,
                          allocated)) {
            freeBlock(allocated);
            return 0;
        }
        inode.sectorsCount += blockSize_ / sectorSize;
        return allocated;
    }
    logical -= pointers;
    const uint64_t doubleCapacity =
        static_cast<uint64_t>(pointers) * pointers;
    if (logical < doubleCapacity) {
        if (inode.blocks[doublyIndirectIndex] == 0) {
            inode.blocks[doublyIndirectIndex] = allocateBlock(preferredGroup);
            if (inode.blocks[doublyIndirectIndex] == 0) {
                freeBlock(allocated);
                return 0;
            }
            inode.sectorsCount += blockSize_ / sectorSize;
        }
        uint32_t indirect = 0;
        const uint32_t first = logical / pointers;
        if (!readPointer(inode.blocks[doublyIndirectIndex], first, indirect)) {
            freeBlock(allocated);
            return 0;
        }
        if (indirect == 0) {
            indirect = allocateBlock(preferredGroup);
            if (indirect == 0 ||
                !writePointer(inode.blocks[doublyIndirectIndex], first,
                              indirect)) {
                if (indirect != 0) freeBlock(indirect);
                freeBlock(allocated);
                return 0;
            }
            inode.sectorsCount += blockSize_ / sectorSize;
        }
        if (!writePointer(indirect, logical % pointers, allocated)) {
            freeBlock(allocated);
            return 0;
        }
        inode.sectorsCount += blockSize_ / sectorSize;
        return allocated;
    }
    logical -= static_cast<uint32_t>(doubleCapacity);
    if (logical >= doubleCapacity * pointers) {
        freeBlock(allocated);
        return 0;
    }
    if (inode.blocks[triplyIndirectIndex] == 0) {
        inode.blocks[triplyIndirectIndex] = allocateBlock(preferredGroup);
        if (inode.blocks[triplyIndirectIndex] == 0) {
            freeBlock(allocated);
            return 0;
        }
        inode.sectorsCount += blockSize_ / sectorSize;
    }
    uint32_t firstBlock = 0;
    const uint32_t firstIndex =
        static_cast<uint32_t>(logical / doubleCapacity);
    if (!readPointer(inode.blocks[triplyIndirectIndex], firstIndex,
                     firstBlock)) {
        freeBlock(allocated);
        return 0;
    }
    if (firstBlock == 0) {
        firstBlock = allocateBlock(preferredGroup);
        if (firstBlock == 0 ||
            !writePointer(inode.blocks[triplyIndirectIndex], firstIndex,
                          firstBlock)) {
            if (firstBlock != 0) freeBlock(firstBlock);
            freeBlock(allocated);
            return 0;
        }
        inode.sectorsCount += blockSize_ / sectorSize;
    }
    uint32_t secondBlock = 0;
    const uint32_t secondIndex = (logical / pointers) % pointers;
    if (!readPointer(firstBlock, secondIndex, secondBlock)) {
        freeBlock(allocated);
        return 0;
    }
    if (secondBlock == 0) {
        secondBlock = allocateBlock(preferredGroup);
        if (secondBlock == 0 ||
            !writePointer(firstBlock, secondIndex, secondBlock)) {
            if (secondBlock != 0) freeBlock(secondBlock);
            freeBlock(allocated);
            return 0;
        }
        inode.sectorsCount += blockSize_ / sectorSize;
    }
    if (!writePointer(secondBlock, logical % pointers, allocated)) {
        freeBlock(allocated);
        return 0;
    }
    inode.sectorsCount += blockSize_ / sectorSize;
    return allocated;
}

uint32_t Volume::allocateBlock(uint32_t preferredGroup) {
    uint8_t bitmap[maximumBlockSize]{};
    if (groupCount_ == 0) return 0;
    preferredGroup %= groupCount_;
    for (uint32_t scanned = 0; scanned < groupCount_; ++scanned) {
        const uint32_t group = (preferredGroup + scanned) % groupCount_;
        GroupDescriptor descriptor{};
        if (!readGroup(group, descriptor) || descriptor.freeBlocksCount == 0 ||
            !readBlock(descriptor.blockBitmap, bitmap))
            continue;
        uint32_t limit = superblock_.blocksPerGroup;
        const uint32_t groupStart =
            superblock_.firstDataBlock + group * superblock_.blocksPerGroup;
        if (groupStart + limit > superblock_.blocksCount)
            limit = superblock_.blocksCount - groupStart;
        for (uint32_t local = 0; local < limit; ++local) {
            if ((bitmap[local / 8] & (1U << (local % 8))) != 0) continue;
            bitmap[local / 8] |= static_cast<uint8_t>(1U << (local % 8));
            if (!writeBlock(descriptor.blockBitmap, bitmap)) return 0;
            --descriptor.freeBlocksCount;
            --superblock_.freeBlocksCount;
            if (!writeGroup(group, descriptor) || !writeSuperblock()) return 0;
            uint8_t zero[maximumBlockSize]{};
            const uint32_t block = groupStart + local;
            return writeBlock(block, zero) ? block : 0;
        }
    }
    return 0;
}

void Volume::freeBlock(uint32_t block) {
    if (block < superblock_.firstDataBlock || block >= superblock_.blocksCount)
        return;
    const uint32_t relative = block - superblock_.firstDataBlock;
    const uint32_t group    = relative / superblock_.blocksPerGroup;
    const uint32_t local    = relative % superblock_.blocksPerGroup;
    GroupDescriptor descriptor{};
    uint8_t bitmap[maximumBlockSize]{};
    if (!readGroup(group, descriptor) ||
        !readBlock(descriptor.blockBitmap, bitmap) ||
        (bitmap[local / 8] & (1U << (local % 8))) == 0)
        return;
    bitmap[local / 8] &= static_cast<uint8_t>(~(1U << (local % 8)));
    if (writeBlock(descriptor.blockBitmap, bitmap)) {
        ++descriptor.freeBlocksCount;
        ++superblock_.freeBlocksCount;
        writeGroup(group, descriptor);
        writeSuperblock();
    }
}

uint32_t Volume::allocateInode(bool directory, uint32_t preferredGroup) {
    uint8_t bitmap[maximumBlockSize]{};
    if (groupCount_ == 0) return 0;
    preferredGroup %= groupCount_;
    for (uint32_t scanned = 0; scanned < groupCount_; ++scanned) {
        const uint32_t group = (preferredGroup + scanned) % groupCount_;
        GroupDescriptor descriptor{};
        if (!readGroup(group, descriptor) || descriptor.freeInodesCount == 0 ||
            !readBlock(descriptor.inodeBitmap, bitmap))
            continue;
        uint32_t limit = superblock_.inodesPerGroup;
        if ((group + 1) * limit > superblock_.inodesCount)
            limit = superblock_.inodesCount - group * limit;
        for (uint32_t local = 0; local < limit; ++local) {
            const uint32_t number =
                group * superblock_.inodesPerGroup + local + 1;
            if (number < superblock_.firstInode ||
                (bitmap[local / 8] & (1U << (local % 8))) != 0)
                continue;
            bitmap[local / 8] |= static_cast<uint8_t>(1U << (local % 8));
            if (!writeBlock(descriptor.inodeBitmap, bitmap)) return 0;
            --descriptor.freeInodesCount;
            if (directory) ++descriptor.usedDirectoriesCount;
            --superblock_.freeInodesCount;
            return writeGroup(group, descriptor) && writeSuperblock() ? number
                                                                      : 0;
        }
    }
    return 0;
}

void Volume::freeInode(uint32_t inode, bool directory) {
    if (inode < superblock_.firstInode || inode > superblock_.inodesCount)
        return;
    const uint32_t group = (inode - 1) / superblock_.inodesPerGroup;
    const uint32_t local = (inode - 1) % superblock_.inodesPerGroup;
    GroupDescriptor descriptor{};
    uint8_t bitmap[maximumBlockSize]{};
    if (!readGroup(group, descriptor) ||
        !readBlock(descriptor.inodeBitmap, bitmap) ||
        (bitmap[local / 8] & (1U << (local % 8))) == 0)
        return;
    bitmap[local / 8] &= static_cast<uint8_t>(~(1U << (local % 8)));
    if (writeBlock(descriptor.inodeBitmap, bitmap)) {
        ++descriptor.freeInodesCount;
        if (directory && descriptor.usedDirectoriesCount != 0)
            --descriptor.usedDirectoriesCount;
        ++superblock_.freeInodesCount;
        writeGroup(group, descriptor);
        writeSuperblock();
    }
}

bool Volume::lookupInDirectory(const Inode& directory, const char* name,
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

bool Volume::resolve(const char* path, uint32_t& number, Inode* inode) {
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

bool Volume::resolveParent(const char* path, uint32_t& parentNumber,
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

bool Volume::lookup(const char* path, FileStatus& status) {
    Inode inode{};
    uint32_t number = 0;
    if (!resolve(path, number, &inode)) return false;
    status = {number, inode.mode, inode.size, inode.uid, inode.gid,
              inode.linksCount, inode.accessTime, inode.modificationTime,
              inode.changeTime};
    return true;
}

int64_t Volume::read(uint32_t number, uint64_t offset, void* buffer,
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
        for (size_t index = 0; index < count; ++index)
            static_cast<uint8_t*>(buffer)[copied + index] =
                blockData[within + index];
        copied += count;
    }
    const uint32_t now = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    if (!device_->readOnly() && now > inode.accessTime + 3600) {
        inode.accessTime = now;
        writeInode(number, inode);
    }
    return static_cast<int64_t>(copied);
}

int64_t Volume::write(uint32_t number, uint64_t offset, const void* buffer,
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
        for (size_t index = 0; index < count; ++index)
            blockData[within + index] =
                static_cast<const uint8_t*>(buffer)[copied + index];
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

bool Volume::clearInodeBlocks(Inode& inode) {
    const uint32_t pointers = blockSize_ / sizeof(uint32_t);
    const uint32_t blocks   = (inode.size + blockSize_ - 1) / blockSize_;
    for (uint32_t logical = 0; logical < blocks; ++logical) {
        const uint32_t block = getDataBlock(inode, logical);
        if (block != 0) freeBlock(block);
    }
    if (inode.blocks[singlyIndirectIndex] != 0)
        freeBlock(inode.blocks[singlyIndirectIndex]);
    if (inode.blocks[doublyIndirectIndex] != 0) {
        uint32_t indirect = 0;
        for (uint32_t index = 0; index < pointers; ++index) {
            if (readPointer(inode.blocks[doublyIndirectIndex], index,
                            indirect) &&
                indirect != 0)
                freeBlock(indirect);
        }
        freeBlock(inode.blocks[doublyIndirectIndex]);
    }
    if (inode.blocks[triplyIndirectIndex] != 0) {
        uint32_t first = 0;
        uint32_t second = 0;
        for (uint32_t firstIndex = 0; firstIndex < pointers; ++firstIndex) {
            if (!readPointer(inode.blocks[triplyIndirectIndex], firstIndex,
                             first) || first == 0) {
                continue;
            }
            for (uint32_t secondIndex = 0; secondIndex < pointers;
                 ++secondIndex) {
                if (readPointer(first, secondIndex, second) && second != 0) {
                    freeBlock(second);
                }
            }
            freeBlock(first);
        }
        freeBlock(inode.blocks[triplyIndirectIndex]);
    }
    for (uint32_t& block : inode.blocks) block = 0;
    inode.size         = 0;
    inode.sectorsCount = 0;
    return true;
}

bool Volume::truncate(uint32_t number) {
    Inode inode{};
    if (!readInode(number, inode) || !clearInodeBlocks(inode)) return false;
    const uint32_t now = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    inode.modificationTime = now;
    inode.changeTime = now;
    return writeInode(number, inode) && sync();
}

bool Volume::addDirectoryEntry(uint32_t directoryNumber, Inode& directory,
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

bool Volume::removeDirectoryEntry(uint32_t directoryNumber, Inode& directory,
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

bool Volume::directoryEmpty(const Inode& directory) {
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

bool Volume::create(const char* path, bool directory) {
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

bool Volume::remove(const char* path) {
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

bool Volume::link(const char* existing, const char* newPath) {
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

bool Volume::changeMode(const char* path, uint16_t mode) {
    uint32_t number = 0;
    Inode inode{};
    if (device_->readOnly() || !resolve(path, number, &inode)) return false;
    inode.mode = static_cast<uint16_t>((inode.mode & 0xf000U) | (mode & 07777U));
    inode.changeTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    return writeInode(number, inode) && sync();
}

bool Volume::changeOwner(const char* path, uint16_t uid, uint16_t gid) {
    uint32_t number = 0;
    Inode inode{};
    if (device_->readOnly() || !resolve(path, number, &inode)) return false;
    inode.uid = uid;
    inode.gid = gid;
    inode.changeTime = static_cast<uint32_t>(
        os::getWallClockTime() / 1000000000ULL);
    return writeInode(number, inode) && sync();
}

bool Volume::changeTimes(const char* path, uint32_t accessTime,
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

int Volume::readDirectory(const char* path, uint32_t index,
                          DirectoryEntry& result) {
    const int count = readDirectoryBatch(path, index, &result, 1);
    return count < 0 ? -1 : (count == 0 ? 1 : 0);
}

int Volume::readDirectoryBatch(const char* path, uint32_t index,
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

bool Volume::sync() {
    superblock_.state = 1;
    return writeSuperblock() && device_->flush();
}

}  // namespace fs::ext3
