#include "../ext4.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext4 {

using namespace detail;

bool Filesystem::readInode(uint32_t number, uint8_t* inode) {
    if (number == 0 || number > inodesCount_ || inode == nullptr) return false;
    const uint32_t group = (number - 1) / inodesPerGroup_;
    const uint32_t local = (number - 1) % inodesPerGroup_;
    uint8_t descriptor[256]{};
    if (descriptorSize_ > sizeof(descriptor) || !readGroup(group, descriptor)) {
        return false;
    }
    uint64_t inodeTable = read32(descriptor + 8);
    if (descriptorSize_ >= 64) {
        inodeTable |= static_cast<uint64_t>(read32(descriptor + 40)) << 32;
    }
    const uint64_t offset = static_cast<uint64_t>(local) * inodeSize_;
    if (!readBlock(inodeTable + offset / blockSize_, blockBuffer_)) {
        return false;
    }
    const uint32_t within = static_cast<uint32_t>(offset % blockSize_);
    for (uint32_t index = 0; index < inodeSize_; ++index) {
        inode[index] = blockBuffer_[within + index];
    }
    return true;
}

bool Filesystem::writeInode(uint32_t number, const uint8_t* inode) {
    if (number == 0 || number > inodesCount_ || inode == nullptr ||
        !writeSupported_) {
        return false;
    }
    const uint32_t group = (number - 1) / inodesPerGroup_;
    const uint32_t local = (number - 1) % inodesPerGroup_;
    uint8_t descriptor[256]{};
    if (descriptorSize_ > sizeof(descriptor) || !readGroup(group, descriptor)) {
        return false;
    }
    uint64_t inodeTable = read32(descriptor + 8);
    if (descriptorSize_ >= 64) {
        inodeTable |= static_cast<uint64_t>(read32(descriptor + 40)) << 32;
    }
    const uint64_t offset = static_cast<uint64_t>(local) * inodeSize_;
    if (!readBlock(inodeTable + offset / blockSize_, blockBuffer_)) {
        return false;
    }
    const uint32_t within = static_cast<uint32_t>(offset % blockSize_);
    for (uint32_t index = 0; index < inodeSize_; ++index) {
        blockBuffer_[within + index] = inode[index];
    }
    return writeBlock(inodeTable + offset / blockSize_, blockBuffer_);
}

uint64_t Filesystem::getDataBlock(const uint8_t* inode, uint32_t logical) {
    if (inode == nullptr || (read32(inode + 32) & 0x00080000U) == 0) {
        return 0;
    }
    const uint8_t* root = inode + 40;
    if (read16(root) != 0xf30a) return 0;
    const uint16_t depth = read16(root + 6);
    if (depth > 5) return 0;
    return findExtent(root, logical, depth, true);
}

uint64_t Filesystem::findExtent(const uint8_t* node, uint32_t logical,
                                uint16_t depth, bool root) {
    if (node == nullptr) return 0;
    while (true) {
        if (read16(node) != 0xf30a) return 0;
        const uint16_t entries  = read16(node + 2);
        const uint16_t maximum  = read16(node + 4);
        const uint32_t capacity = ((root ? 60U : blockSize_) - 12) / 12;
        if (entries > maximum || maximum > capacity) return 0;
        const uint8_t* entry = node + 12;
        if (depth == 0) {
            for (uint16_t index = 0; index < entries; ++index, entry += 12) {
                const uint32_t first         = read32(entry);
                const uint16_t encodedLength = read16(entry + 4);
                const uint32_t length =
                    encodedLength == 0
                        ? 32768
                        : (encodedLength > 32768 ? encodedLength - 32768
                                                 : encodedLength);
                if (logical < first || logical - first >= length) continue;
                if (encodedLength > 32768) return 0;
                const uint64_t physical =
                    static_cast<uint64_t>(read32(entry + 8)) |
                    (static_cast<uint64_t>(read16(entry + 6)) << 32);
                const uint64_t block = physical + (logical - first);
                return block < blocksCount_ ? block : 0;
            }
            return 0;
        }

        const uint8_t* selected = nullptr;
        for (uint16_t index = 0; index < entries; ++index, entry += 12) {
            if (read32(entry) > logical) break;
            selected = entry;
        }
        if (selected == nullptr) return 0;
        const uint64_t child = read48(selected + 4);
        if (child >= blocksCount_ || !readBlock(child, blockBuffer_)) return 0;
        node = blockBuffer_;
        --depth;
        root = false;
    }
}

uint64_t Filesystem::ensureDataBlock(uint8_t* inode, uint32_t logical) {
    if (inode == nullptr || !writeSupported_) return 0;
    const uint64_t existing = getDataBlock(inode, logical);
    if (existing != 0) return existing;

    uint8_t* root = inode + 40;
    if ((read32(inode + 32) & 0x00080000U) == 0) {
        os::clearMemory(root, 60);
        write16(root, 0xf30a);
        write16(root + 4, 4);
        write16(root + 6, 0);
        write32(inode + 32, read32(inode + 32) | 0x00080000U);
    }
    if (read16(root) != 0xf30a || read16(root + 6) != 0 ||
        read16(root + 2) >= 4 || read16(root + 4) > 4) {
        return 0;
    }

    const uint32_t allocated = allocateBlock();
    if (allocated == 0) return 0;
    const uint16_t count = read16(root + 2);
    uint8_t entries[4 * 12]{};
    for (uint16_t index = 0; index < count; ++index) {
        os::copyBytes(entries + index * 12, root + 12 + index * 12, 12);
    }
    uint16_t insert = 0;
    while (insert < count && read32(entries + insert * 12) < logical) {
        ++insert;
    }
    if (insert < count && read32(entries + insert * 12) == logical) {
        freeBlock(allocated);
        return 0;
    }
    for (uint16_t index = count; index > insert; --index) {
        os::copyBytes(entries + index * 12, entries + (index - 1) * 12, 12);
    }
    setExtent(entries + insert * 12, logical, allocated, 1);

    uint16_t mergedCount = 0;
    uint8_t merged[4 * 12]{};
    for (uint16_t index = 0; index <= count; ++index) {
        const uint8_t* current = entries + index * 12;
        if (mergedCount != 0) {
            uint8_t* previous             = merged + (mergedCount - 1) * 12;
            const uint32_t previousLength = extentLength(previous);
            const uint32_t currentLength  = extentLength(current);
            const uint64_t previousPhysical =
                static_cast<uint64_t>(read32(previous + 8)) |
                (static_cast<uint64_t>(read16(previous + 6)) << 32);
            const uint64_t currentPhysical =
                static_cast<uint64_t>(read32(current + 8)) |
                (static_cast<uint64_t>(read16(current + 6)) << 32);
            if (read32(previous) + previousLength == read32(current) &&
                previousPhysical + previousLength == currentPhysical &&
                previousLength + currentLength <= 32767) {
                write16(previous + 4,
                        static_cast<uint16_t>(previousLength + currentLength));
                continue;
            }
        }
        os::copyBytes(merged + mergedCount * 12, current, 12);
        ++mergedCount;
    }
    if (mergedCount > 4) {
        freeBlock(allocated);
        return 0;
    }
    os::clearMemory(root + 12, 48);
    os::copyBytes(root + 12, merged, mergedCount * 12);
    write16(root + 2, mergedCount);

    const uint32_t sectorsPerBlock = blockSize_ / 512;
    write32(inode + 28, read32(inode + 28) + sectorsPerBlock);
    return allocated;
}

bool Filesystem::clearInodeBlocks(uint8_t* inode) {
    uint8_t* root = inode + 40;
    if (read16(root) == 0xf30a && read16(root + 6) == 0) {
        const uint16_t count = read16(root + 2);
        if (count > 4) return false;
        for (uint16_t index = 0; index < count; ++index) {
            const uint8_t* extent = root + 12 + index * 12;
            const uint32_t length = extentLength(extent);
            const uint64_t first =
                static_cast<uint64_t>(read32(extent + 8)) |
                (static_cast<uint64_t>(read16(extent + 6)) << 32);
            if (length > 32768 || first + length > blocksCount_) return false;
            for (uint32_t block = 0; block < length; ++block) {
                freeBlock(static_cast<uint32_t>(first + block));
            }
        }
    } else if (read16(root) == 0xf30a && read16(root + 6) != 0) {
        return false;
    }
    os::clearMemory(root, 60);
    write16(root, 0xf30a);
    write16(root + 4, 4);
    write32(inode + 28, 0);
    write32(inode + 4, 0);
    write32(inode + 108, 0);
    return true;
}

}  // namespace fs::ext4
