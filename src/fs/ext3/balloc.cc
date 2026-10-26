#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext3/internal.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>

namespace fs::ext3 {

using namespace detail;

uint32_t Filesystem::allocateBlock(uint32_t preferredGroup) {
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

void Filesystem::freeBlock(uint32_t block) {
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

}  // namespace fs::ext3
