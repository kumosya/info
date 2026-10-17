#include "../ext4.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext4 {

using namespace detail;

bool Filesystem::readBlock(uint64_t block, void* buffer) {
    if (device_ == nullptr || buffer == nullptr || block >= blocksCount_) {
        return false;
    }
    const uint32_t sectorsPerBlock = blockSize_ / 512;
    return device_->readSectors(block * sectorsPerBlock, sectorsPerBlock,
                                buffer);
}

bool Filesystem::writeBlock(uint64_t block, const void* buffer) {
    if (device_ == nullptr || device_->readOnly() || buffer == nullptr ||
        block >= blocksCount_) {
        return false;
    }
    const uint32_t sectorsPerBlock = blockSize_ / 512;
    return device_->writeSectors(block * sectorsPerBlock, sectorsPerBlock,
                                 buffer);
}

}  // namespace fs::ext4
