#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/fs/ext4/filesystem.h"
#include "service/fs/ext4/internal.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace fs::ext4 {

using namespace detail;

uint32_t Filesystem::allocateBlock(uint32_t preferredGroup) {
    if (!writeSupported_ || groupCount_ == 0) return 0;
    preferredGroup %= groupCount_;
    for (uint32_t scanned = 0; scanned < groupCount_; ++scanned) {
        const uint32_t group = (preferredGroup + scanned) % groupCount_;
        uint8_t descriptor[256]{};
        if (descriptorSize_ > sizeof(descriptor) ||
            !readGroup(group, descriptor) ||
            DiskEncoding::read16(descriptor + 12) == 0) {
            continue;
        }
        const uint64_t bitmapBlock =
            static_cast<uint64_t>(DiskEncoding::read32(descriptor)) |
            (descriptorSize_ >= 64
                 ? static_cast<uint64_t>(DiskEncoding::read32(descriptor + 32))
                       << 32
                 : 0);
        if (!readBlock(bitmapBlock, blockBuffer_)) continue;
        const uint64_t groupStart =
            static_cast<uint64_t>(firstDataBlock_) +
            static_cast<uint64_t>(group) * blocksPerGroup_;
        uint32_t limit = blocksPerGroup_;
        if (groupStart + limit > blocksCount_) {
            limit = static_cast<uint32_t>(blocksCount_ - groupStart);
        }
        for (uint32_t local = 0; local < limit; ++local) {
            if ((blockBuffer_[local / 8] & (1U << (local % 8))) != 0) {
                continue;
            }
            blockBuffer_[local / 8] |= static_cast<uint8_t>(1U << (local % 8));
            if (!writeBlock(bitmapBlock, blockBuffer_)) return 0;
            DiskEncoding::write16(
                descriptor + 12,
                static_cast<uint16_t>(DiskEncoding::read16(descriptor + 12) -
                                      1));
            if (descriptorSize_ >= 64) {
                DiskEncoding::write16(
                    descriptor + 44,
                    static_cast<uint16_t>(
                        DiskEncoding::read16(descriptor + 44) - 1));
            }
            uint64_t freeBlocks = DiskEncoding::readFreeBlocks(superblock_);
            if (freeBlocks == 0) return 0;
            DiskEncoding::writeFreeBlocks(superblock_, freeBlocks - 1);
            if (!writeGroup(group, descriptor) ||
                !device_->writeSectors(2, 2, superblock_)) {
                return 0;
            }
            const uint32_t block = static_cast<uint32_t>(groupStart + local);
            os::Runtime::clearMemory(blockBuffer_, blockSize_);
            return writeBlock(block, blockBuffer_) ? block : 0;
        }
    }
    return 0;
}

void Filesystem::freeBlock(uint32_t block) {
    if (!writeSupported_ || block < firstDataBlock_ || block >= blocksCount_) {
        return;
    }
    const uint32_t relative = block - firstDataBlock_;
    const uint32_t group    = relative / blocksPerGroup_;
    const uint32_t local    = relative % blocksPerGroup_;
    uint8_t descriptor[256]{};
    if (descriptorSize_ > sizeof(descriptor) || !readGroup(group, descriptor)) {
        return;
    }
    const uint64_t bitmapBlock =
        static_cast<uint64_t>(DiskEncoding::read32(descriptor)) |
        (descriptorSize_ >= 64
             ? static_cast<uint64_t>(DiskEncoding::read32(descriptor + 32))
                   << 32
             : 0);
    if (!readBlock(bitmapBlock, blockBuffer_) ||
        (blockBuffer_[local / 8] & (1U << (local % 8))) == 0) {
        return;
    }
    blockBuffer_[local / 8] &= static_cast<uint8_t>(~(1U << (local % 8)));
    if (!writeBlock(bitmapBlock, blockBuffer_)) return;
    DiskEncoding::write16(
        descriptor + 12,
        static_cast<uint16_t>(DiskEncoding::read16(descriptor + 12) + 1));
    if (descriptorSize_ >= 64) {
        DiskEncoding::write16(
            descriptor + 44,
            static_cast<uint16_t>(DiskEncoding::read16(descriptor + 44) + 1));
    }
    DiskEncoding::writeFreeBlocks(
        superblock_, DiskEncoding::readFreeBlocks(superblock_) + 1);
    if (writeGroup(group, descriptor)) {
        device_->writeSectors(2, 2, superblock_);
    }
}

}  // namespace fs::ext4
