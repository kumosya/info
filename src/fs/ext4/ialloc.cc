#include "service/fs/ext4/filesystem.h"
#include "service/fs/ext4/internal.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace fs::ext4 {

using namespace detail;

uint32_t Filesystem::allocateInode(bool directory, uint32_t preferredGroup) {
    if (!writeSupported_ || groupCount_ == 0) return 0;
    preferredGroup %= groupCount_;
    for (uint32_t scanned = 0; scanned < groupCount_; ++scanned) {
        const uint32_t group = (preferredGroup + scanned) % groupCount_;
        uint8_t descriptor[256]{};
        if (descriptorSize_ > sizeof(descriptor) ||
            !readGroup(group, descriptor) ||
            DiskEncoding::read16(descriptor + 14) == 0) {
            continue;
        }
        const uint64_t bitmapBlock =
            static_cast<uint64_t>(DiskEncoding::read32(descriptor + 4)) |
            (descriptorSize_ >= 64
                 ? static_cast<uint64_t>(DiskEncoding::read32(descriptor + 36))
                       << 32
                 : 0);
        if (!readBlock(bitmapBlock, blockBuffer_)) continue;
        uint32_t limit = inodesPerGroup_;
        if ((group + 1) * inodesPerGroup_ > inodesCount_) {
            limit = inodesCount_ - group * inodesPerGroup_;
        }
        for (uint32_t local = 0; local < limit; ++local) {
            const uint32_t number = group * inodesPerGroup_ + local + 1;
            if (number < DiskEncoding::read32(superblock_ + 84) ||
                inodeRetained(number) ||
                (blockBuffer_[local / 8] & (1U << (local % 8))) != 0) {
                continue;
            }
            blockBuffer_[local / 8] |= static_cast<uint8_t>(1U << (local % 8));
            if (!writeBlock(bitmapBlock, blockBuffer_)) return 0;
            DiskEncoding::write16(
                descriptor + 14,
                static_cast<uint16_t>(DiskEncoding::read16(descriptor + 14) -
                                      1));
            if (directory) {
                DiskEncoding::write16(
                    descriptor + 16,
                    static_cast<uint16_t>(
                        DiskEncoding::read16(descriptor + 16) + 1));
            }
            const uint32_t freeInodes =
                DiskEncoding::readFreeInodes(superblock_);
            if (freeInodes == 0) return 0;
            DiskEncoding::writeFreeInodes(superblock_, freeInodes - 1);
            return writeGroup(group, descriptor) &&
                           device_->writeSectors(2, 2, superblock_)
                       ? number
                       : 0;
        }
    }
    return 0;
}

void Filesystem::freeInode(uint32_t number, bool directory) {
    if (!writeSupported_ || number < DiskEncoding::read32(superblock_ + 84) ||
        number > inodesCount_) {
        return;
    }
    const uint32_t group = (number - 1) / inodesPerGroup_;
    const uint32_t local = (number - 1) % inodesPerGroup_;
    uint8_t descriptor[256]{};
    if (descriptorSize_ > sizeof(descriptor) || !readGroup(group, descriptor)) {
        return;
    }
    const uint64_t bitmapBlock =
        static_cast<uint64_t>(DiskEncoding::read32(descriptor + 4)) |
        (descriptorSize_ >= 64
             ? static_cast<uint64_t>(DiskEncoding::read32(descriptor + 36))
                   << 32
             : 0);
    if (!readBlock(bitmapBlock, blockBuffer_) ||
        (blockBuffer_[local / 8] & (1U << (local % 8))) == 0) {
        return;
    }
    blockBuffer_[local / 8] &= static_cast<uint8_t>(~(1U << (local % 8)));
    if (!writeBlock(bitmapBlock, blockBuffer_)) return;
    DiskEncoding::write16(
        descriptor + 14,
        static_cast<uint16_t>(DiskEncoding::read16(descriptor + 14) + 1));
    if (directory && DiskEncoding::read16(descriptor + 16) != 0) {
        DiskEncoding::write16(
            descriptor + 16,
            static_cast<uint16_t>(DiskEncoding::read16(descriptor + 16) - 1));
    }
    DiskEncoding::writeFreeInodes(
        superblock_, DiskEncoding::readFreeInodes(superblock_) + 1);
    if (writeGroup(group, descriptor)) {
        device_->writeSectors(2, 2, superblock_);
    }
}

}  // namespace fs::ext4
