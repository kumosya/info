#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext3/internal.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>

namespace fs::ext3 {

using namespace detail;

bool Filesystem::mount(SectorDevice& device) {
    device_ = &device;
    for (uint32_t slot = 0; slot < blockCacheSlotCount; ++slot) {
        cacheValid_[slot] = false;
        cacheNext_[slot]  = cacheNoSlot;
    }
    for (uint32_t bucket = 0; bucket < cacheHashBuckets; ++bucket) {
        cacheHash_[bucket] = cacheNoSlot;
    }
    cacheClock_      = 0;
    readaheadInode_       = 0;
    readaheadLastLogical_ = 0;
    if (!readSuperblock() || superblock_.magic != superMagic ||
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

bool Filesystem::readSuperblock() {
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

bool Filesystem::writeSuperblock() {
    if (device_ == nullptr || device_->readOnly()) return false;
    const uint8_t* source = reinterpret_cast<const uint8_t*>(&superblock_);
    return device_->writeSector(2, source) &&
           device_->writeSector(3, source + sectorSize);
}

bool Filesystem::readGroup(uint32_t group, GroupDescriptor& descriptor) {
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

bool Filesystem::writeGroup(uint32_t group, const GroupDescriptor& descriptor) {
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

}  // namespace fs::ext3
