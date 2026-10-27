#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext3/internal.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace fs::ext3 {

using namespace detail;

uint32_t Filesystem::allocateInode(bool directory, uint32_t preferredGroup) {
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
            if (number < superblock_.firstInode || inodeRetained(number) ||
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

void Filesystem::freeInode(uint32_t inode, bool directory) {
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

}  // namespace fs::ext3
