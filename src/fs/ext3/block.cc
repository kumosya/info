#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext3/internal.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace fs::ext3 {

using namespace detail;

uint32_t Filesystem::findCachedBlock(uint32_t block) const {
    const uint32_t bucket = block & (cacheHashBuckets - 1);
    uint32_t slot         = cacheHash_[bucket];
    while (slot != cacheNoSlot) {
        if (cacheValid_[slot] && cachedBlocks_[slot] == block) return slot;
        slot = cacheNext_[slot];
    }
    return cacheNoSlot;
}

void Filesystem::insertCacheSlot(uint32_t slot, uint32_t block) {
    const uint32_t bucket = block & (cacheHashBuckets - 1);
    cachedBlocks_[slot]   = block;
    cacheValid_[slot]     = true;
    cacheNext_[slot]      = cacheHash_[bucket];
    cacheHash_[bucket]    = slot;
}

void Filesystem::removeCacheSlot(uint32_t slot) {
    if (!cacheValid_[slot]) return;
    const uint32_t bucket = cachedBlocks_[slot] & (cacheHashBuckets - 1);
    uint32_t* link        = &cacheHash_[bucket];
    while (*link != cacheNoSlot) {
        if (*link == slot) {
            *link = cacheNext_[slot];
            break;
        }
        link = &cacheNext_[*link];
    }
    cacheNext_[slot]  = cacheNoSlot;
    cacheValid_[slot] = false;
}

uint32_t Filesystem::evictCacheSlot() {
    for (uint32_t slot = 0; slot < blockCacheSlotCount; ++slot) {
        if (!cacheValid_[slot]) return slot;
    }
    uint32_t selected = 0;
    for (uint32_t slot = 1; slot < blockCacheSlotCount; ++slot) {
        if (cacheAges_[slot] < cacheAges_[selected]) selected = slot;
    }
    removeCacheSlot(selected);
    return selected;
}

bool Filesystem::readBlock(uint32_t block, void* buffer) {
    if (device_ == nullptr || buffer == nullptr ||
        block >= superblock_.blocksCount) {
        return false;
    }
    const uint32_t hit = findCachedBlock(block);
    if (hit != cacheNoSlot) {
        os::Runtime::copyBytes(buffer, blockCache_[hit], blockSize_);
        cacheAges_[hit] = ++cacheClock_;
        return true;
    }
    return fetchBlock(block, buffer);
}

bool Filesystem::fetchBlock(uint32_t block, void* buffer) {
    const uint32_t selected = evictCacheSlot();
    const uint32_t sectors  = blockSize_ / sectorSize;
    if (!device_->readSectors(static_cast<uint64_t>(block) * sectors, sectors,
                              blockCache_[selected])) {
        return false;
    }
    os::Runtime::copyBytes(buffer, blockCache_[selected], blockSize_);
    insertCacheSlot(selected, block);
    cacheAges_[selected] = ++cacheClock_;
    return true;
}

void Filesystem::prefetchBlock(uint32_t block) {
    if (device_ == nullptr || block >= superblock_.blocksCount ||
        findCachedBlock(block) != cacheNoSlot) {
        return;
    }
    const uint32_t selected = evictCacheSlot();
    const uint32_t sectors  = blockSize_ / sectorSize;
    if (!device_->readSectors(static_cast<uint64_t>(block) * sectors, sectors,
                              blockCache_[selected])) {
        return;
    }
    insertCacheSlot(selected, block);
    cacheAges_[selected] = ++cacheClock_;
}

void Filesystem::prefetchAhead(uint32_t inodeNumber, const Inode& inode,
                               uint32_t logical) {
    if (readaheadInode_ != inodeNumber ||
        readaheadLastLogical_ + 1 != logical) {
        readaheadInode_       = inodeNumber;
        readaheadLastLogical_ = logical;
        return;
    }
    readaheadLastLogical_ = logical;
    const uint32_t fileBlocks =
        static_cast<uint32_t>((inode.size + blockSize_ - 1) / blockSize_);
    const uint32_t limit = logical + 1 + readaheadWindow < fileBlocks
                               ? logical + 1 + readaheadWindow
                               : fileBlocks;
    for (uint32_t target = logical + 1; target < limit; ++target) {
        const uint32_t block = getDataBlock(inode, target);
        if (block != 0) prefetchBlock(block);
    }
}

bool Filesystem::writeBlock(uint32_t block, const void* buffer) {
    if (device_ == nullptr || device_->readOnly() || buffer == nullptr ||
        block >= superblock_.blocksCount) {
        return false;
    }
    const uint32_t sectors = blockSize_ / sectorSize;
    if (!device_->writeSectors(static_cast<uint64_t>(block) * sectors, sectors,
                               buffer)) {
        return false;
    }
    const uint32_t hit = findCachedBlock(block);
    if (hit != cacheNoSlot) {
        os::Runtime::copyBytes(blockCache_[hit], buffer, blockSize_);
        cacheAges_[hit] = ++cacheClock_;
    }
    return true;
}

}  // namespace fs::ext3
