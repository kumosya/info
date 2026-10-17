#include "../ext3.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext3 {

using namespace detail;

bool Filesystem::readInode(uint32_t number, Inode& inode) {
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

bool Filesystem::writeInode(uint32_t number, const Inode& inode) {
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

bool Filesystem::readPointer(uint32_t block, uint32_t index, uint32_t& value) {
    if (index >= blockSize_ / sizeof(uint32_t)) return false;
    uint32_t data[maximumBlockSize / sizeof(uint32_t)]{};
    if (!readBlock(block, data)) return false;
    value = data[index];
    return true;
}

bool Filesystem::writePointer(uint32_t block, uint32_t index, uint32_t value) {
    if (index >= blockSize_ / sizeof(uint32_t)) return false;
    uint32_t data[maximumBlockSize / sizeof(uint32_t)]{};
    if (!readBlock(block, data)) return false;
    data[index] = value;
    return writeBlock(block, data);
}

uint32_t Filesystem::getDataBlock(const Inode& inode, uint32_t logical) {
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

uint32_t Filesystem::ensureDataBlock(Inode& inode, uint32_t logical) {
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

bool Filesystem::clearInodeBlocks(Inode& inode) {
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

}  // namespace fs::ext3
