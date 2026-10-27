#include "service/fs/ext4/filesystem.h"
#include "service/fs/ext4/internal.h"

namespace fs::ext4 {

namespace {

class MetadataChecksum {
   public:
    static uint32_t crc32c(uint32_t crc, const uint8_t* data, size_t size);
};

uint32_t MetadataChecksum::crc32c(uint32_t crc, const uint8_t* data,
                                  size_t size) {
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0x82f63b78U : 0U);
        }
    }
    return crc;
}

constexpr uint32_t featureIncompatFileType     = 0x0002;
constexpr uint32_t featureIncompatRecover      = 0x0004;
constexpr uint32_t featureIncompatExtents      = 0x0040;
constexpr uint32_t featureIncompat64Bit        = 0x0080;
constexpr uint32_t featureIncompatFlexBg       = 0x0200;
constexpr uint32_t featureIncompatCsumSeed     = 0x2000;
constexpr uint32_t featureIncompatLargeDir     = 0x4000;
constexpr uint32_t featureIncompatOrphanFile   = 0x100000;
constexpr uint32_t featureRoCompatMetadataCsum = 0x0400;

}  // namespace

using namespace detail;

bool Filesystem::mount(SectorDevice& device) {
    device_ = &device;
    if (!device.readSectors(2, 2, superblock_) ||
        DiskEncoding::read16(superblock_ + 56) != superMagic) {
        return false;
    }

    const uint32_t logBlockSize = DiskEncoding::read32(superblock_ + 24);
    if (logBlockSize > 2) return false;
    blockSize_            = 1024U << logBlockSize;
    firstDataBlock_       = DiskEncoding::read32(superblock_ + 20);
    blocksPerGroup_       = DiskEncoding::read32(superblock_ + 32);
    inodesPerGroup_       = DiskEncoding::read32(superblock_ + 40);
    inodesCount_          = DiskEncoding::read32(superblock_);
    inodeSize_            = DiskEncoding::read16(superblock_ + 88);
    incompatibleFeatures_ = DiskEncoding::read32(superblock_ + 96);
    readOnlyFeatures_     = DiskEncoding::read32(superblock_ + 100);
    const bool has64Bit   = (incompatibleFeatures_ & featureIncompat64Bit) != 0;
    descriptorSize_ = has64Bit ? DiskEncoding::read16(superblock_ + 254) : 32;
    if (descriptorSize_ == 0) descriptorSize_ = 32;

    constexpr uint32_t supportedIncompatible =
        featureIncompatFileType | featureIncompatRecover |
        featureIncompatExtents | featureIncompat64Bit | featureIncompatFlexBg |
        featureIncompatCsumSeed | featureIncompatLargeDir |
        featureIncompatOrphanFile;
    constexpr uint32_t supportedReadOnly =
        0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040 |
        featureRoCompatMetadataCsum | 0x2000 | 0x8000;
    if ((incompatibleFeatures_ & featureIncompatExtents) == 0 ||
        (incompatibleFeatures_ & featureIncompatRecover) != 0 ||
        (incompatibleFeatures_ & ~supportedIncompatible) != 0 ||
        (readOnlyFeatures_ & ~supportedReadOnly) != 0 || blocksPerGroup_ == 0 ||
        inodesPerGroup_ == 0 || inodeSize_ < 128 || inodeSize_ > blockSize_ ||
        (inodeSize_ & (inodeSize_ - 1)) != 0 || descriptorSize_ < 32 ||
        descriptorSize_ > blockSize_ || descriptorSize_ % 8 != 0 ||
        DiskEncoding::read16(superblock_ + 58) != 1) {
        return false;
    }

    blocksCount_ = DiskEncoding::read32(superblock_ + 4);
    if (has64Bit) {
        blocksCount_ |=
            static_cast<uint64_t>(DiskEncoding::read32(superblock_ + 336))
            << 32;
    }
    if (blocksCount_ <= firstDataBlock_) return false;
    groupCount_ = static_cast<uint32_t>(
        (blocksCount_ - firstDataBlock_ + blocksPerGroup_ - 1) /
        blocksPerGroup_);

    if ((readOnlyFeatures_ & featureRoCompatMetadataCsum) != 0) {
        if (superblock_[373] != 1 ||
            MetadataChecksum::crc32c(0xffffffffU, superblock_, 1020) !=
                DiskEncoding::read32(superblock_ + 1020)) {
            return false;
        }
    }
    writeSupported_ =
        !has64Bit && (DiskEncoding::read32(superblock_ + 92) & 0x0004U) == 0 &&
        (readOnlyFeatures_ & (featureRoCompatMetadataCsum | 0x0010U)) == 0;

    return groupCount_ != 0 && readInode(rootInode, inodeBuffer_) &&
           (DiskEncoding::read16(inodeBuffer_) & 0xf000U) == modeDirectory;
}

bool Filesystem::readGroup(uint32_t group, uint8_t* descriptor) {
    if (group >= groupCount_ || descriptor == nullptr) return false;
    const uint64_t offset = static_cast<uint64_t>(group) * descriptorSize_;
    const uint64_t tableBlock =
        static_cast<uint64_t>(firstDataBlock_) + 1 + offset / blockSize_;
    if (!readBlock(tableBlock, blockBuffer_)) return false;
    const uint32_t within = static_cast<uint32_t>(offset % blockSize_);
    for (uint32_t index = 0; index < descriptorSize_; ++index) {
        descriptor[index] = blockBuffer_[within + index];
    }

    if ((DiskEncoding::read32(superblock_ + 100) &
         featureRoCompatMetadataCsum) != 0) {
        const uint16_t expected = DiskEncoding::read16(descriptor + 30);
        uint8_t savedLow[2]{descriptor[30], descriptor[31]};
        descriptor[30] = 0;
        descriptor[31] = 0;
        uint32_t seed  = 0xffffffffU;
        if ((incompatibleFeatures_ & featureIncompatCsumSeed) != 0) {
            seed = DiskEncoding::read32(superblock_ + 624);
        } else {
            seed = MetadataChecksum::crc32c(seed, superblock_ + 104, 16);
        }
        uint8_t groupBytes[4]{static_cast<uint8_t>(group),
                              static_cast<uint8_t>(group >> 8),
                              static_cast<uint8_t>(group >> 16),
                              static_cast<uint8_t>(group >> 24)};
        uint32_t checksum =
            MetadataChecksum::crc32c(seed, groupBytes, sizeof(groupBytes));
        checksum =
            MetadataChecksum::crc32c(checksum, descriptor, descriptorSize_);
        descriptor[30] = savedLow[0];
        descriptor[31] = savedLow[1];
        if (static_cast<uint16_t>(checksum) != expected) {
            return false;
        }
    }
    return true;
}

bool Filesystem::writeGroup(uint32_t group, uint8_t* descriptor) {
    if (group >= groupCount_ || descriptor == nullptr || !writeSupported_) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(group) * descriptorSize_;
    const uint64_t tableBlock =
        static_cast<uint64_t>(firstDataBlock_) + 1 + offset / blockSize_;
    if (!readBlock(tableBlock, blockBuffer_)) return false;
    const uint32_t within = static_cast<uint32_t>(offset % blockSize_);
    for (uint32_t index = 0; index < descriptorSize_; ++index) {
        blockBuffer_[within + index] = descriptor[index];
    }
    return writeBlock(tableBlock, blockBuffer_);
}

}  // namespace fs::ext4
