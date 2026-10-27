#pragma once
#include <cstdint>

#include <os/storage.h>

namespace storage {
namespace detail {
constexpr uint32_t blockSize               = 512;
constexpr uint32_t maximumPartitionEntries = 128;
class PartitionEncoding {
   public:
    static uint32_t readLe32(const uint8_t* bytes) {
        return static_cast<uint32_t>(bytes[0]) |
               (static_cast<uint32_t>(bytes[1]) << 8) |
               (static_cast<uint32_t>(bytes[2]) << 16) |
               (static_cast<uint32_t>(bytes[3]) << 24);
    }

    static uint64_t readLe64(const uint8_t* bytes) {
        return static_cast<uint64_t>(readLe32(bytes)) |
               (static_cast<uint64_t>(readLe32(bytes + 4)) << 32);
    }

    static uint32_t updateCrc32(uint32_t crc, const uint8_t* bytes,
                                uint32_t size) {
        for (uint32_t index = 0; index < size; ++index) {
            crc ^= bytes[index];
            for (uint32_t bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^
                      (0xedb88320U & (0U - static_cast<uint32_t>(crc & 1U)));
            }
        }
        return crc;
    }
};

}  // namespace detail
using namespace detail;
class PartitionTable {
   public:
    template <typename Device>
    static bool findPartitions(Device& driver, os::Storage::Part* partitions,
                               uint32_t capacity, uint32_t& count) {
        count = 0;
        uint8_t mbr[blockSize]{};
        if (driver.readBlock(0, mbr) != os::Storage::Error::Success ||
            mbr[510] != 0x55 || mbr[511] != 0xaa) {
            return false;
        }
        for (uint32_t index = 0; index < 4; ++index) {
            const uint8_t* entry = mbr + 446 + index * 16;
            const uint8_t type   = entry[4];
            const uint32_t start = PartitionEncoding::readLe32(entry + 8);
            const uint32_t partitionBlocks =
                PartitionEncoding::readLe32(entry + 12);
            if (type != 0 && type != 0xee && partitionBlocks != 0 &&
                start < driver.blockCount() &&
                partitionBlocks <= driver.blockCount() - start) {
                if (count >= capacity) return false;
                os::Storage::Part& output = partitions[count++];
                output                    = {};
                char partitionUuid[40]{};
                output.setStartBlock(start);
                output.setBlockCount(partitionBlocks);
                output.setType(type);
                output.setNumber(index + 1);
                formatHex(partitionUuid, PartitionEncoding::readLe32(mbr + 440),
                          8);
                partitionUuid[8] = '-';
                formatHex(partitionUuid + 9, index + 1, 2);
                output.setUuid(partitionUuid);
            }
        }
        if (count != 0) return true;
        uint8_t header[blockSize]{};
        if (driver.readBlock(1, header) != os::Storage::Error::Success ||
            !validSignature(header)) {
            return false;
        }
        const uint32_t headerSize = PartitionEncoding::readLe32(header + 12);
        const uint32_t expectedHeaderCrc =
            PartitionEncoding::readLe32(header + 16);
        const uint64_t firstUsable  = PartitionEncoding::readLe64(header + 40);
        const uint64_t lastUsable   = PartitionEncoding::readLe64(header + 48);
        const uint64_t entriesBlock = PartitionEncoding::readLe64(header + 72);
        const uint32_t entryCount   = PartitionEncoding::readLe32(header + 80);
        const uint32_t entrySize    = PartitionEncoding::readLe32(header + 84);
        const uint32_t expectedEntriesCrc =
            PartitionEncoding::readLe32(header + 88);
        if (headerSize < 92 || headerSize > blockSize || entryCount == 0 ||
            entryCount > maximumPartitionEntries || entrySize < 128 ||
            entrySize > blockSize || blockSize % entrySize != 0 ||
            firstUsable > lastUsable || lastUsable >= driver.blockCount()) {
            return false;
        }
        uint8_t savedCrc[4] = {header[16], header[17], header[18], header[19]};
        header[16] = header[17] = header[18] = header[19] = 0;
        const uint32_t headerCrc =
            PartitionEncoding::updateCrc32(0xffffffffU, header, headerSize) ^
            0xffffffffU;
        for (uint32_t index = 0; index < 4; ++index) {
            header[16 + index] = savedCrc[index];
        }
        if (headerCrc != expectedHeaderCrc) {
            return false;
        }

        uint8_t block[blockSize]{};
        uint32_t crc          = 0xffffffffU;
        uint64_t currentBlock = ~0ULL;
        for (uint32_t index = 0; index < entryCount; ++index) {
            const uint64_t byteOffset =
                static_cast<uint64_t>(index) * entrySize;
            const uint64_t blockNumber = entriesBlock + byteOffset / blockSize;
            const uint32_t offset =
                static_cast<uint32_t>(byteOffset % blockSize);
            if (offset + entrySize > blockSize || blockNumber != currentBlock) {
                if (driver.readBlock(blockNumber, block) !=
                    os::Storage::Error::Success) {
                    return false;
                }
                currentBlock = blockNumber;
            }
            crc =
                PartitionEncoding::updateCrc32(crc, block + offset, entrySize);
            if (!emptyGuid(block + offset)) {
                const uint64_t start =
                    PartitionEncoding::readLe64(block + offset + 32);
                const uint64_t end =
                    PartitionEncoding::readLe64(block + offset + 40);
                if (start < firstUsable || end > lastUsable || start > end) {
                    return false;
                }
                if (count >= capacity) return false;
                os::Storage::Part& output = partitions[count++];
                output                    = {};
                char partitionUuid[40]{};
                output.setStartBlock(start);
                output.setBlockCount(end - start + 1);
                output.setType(1);
                output.setNumber(index + 1);
                const uint8_t* uuid       = block + offset + 16;
                constexpr uint8_t order[] = {3, 2, 1,  0,  5,  4,  7,  6,
                                             8, 9, 10, 11, 12, 13, 14, 15};
                uint32_t position         = 0;
                for (uint32_t byte = 0; byte < 16; ++byte) {
                    if (byte == 4 || byte == 6 || byte == 8 || byte == 10)
                        partitionUuid[position++] = '-';
                    formatHex(partitionUuid + position, uuid[order[byte]], 2);
                    position += 2;
                }
                output.setUuid(partitionUuid);
            }
        }
        crc ^= 0xffffffffU;
        return crc == expectedEntriesCrc && count != 0;
    }

   private:
    static void formatHex(char* output, uint32_t value, uint32_t digits) {
        constexpr char hex[] = "0123456789abcdef";
        for (uint32_t index = 0; index < digits; ++index)
            output[index] = hex[(value >> ((digits - index - 1) * 4)) & 15];
        output[digits] = '\0';
    }
    static bool validSignature(const uint8_t* header) {
        constexpr char signature[] = "EFI PART";
        for (uint32_t index = 0; index < 8; ++index) {
            if (header[index] != static_cast<uint8_t>(signature[index])) {
                return false;
            }
        }
        return true;
    }

    static bool emptyGuid(const uint8_t* value) {
        for (uint32_t index = 0; index < 16; ++index) {
            if (value[index] != 0) {
                return false;
            }
        }
        return true;
    }
};

}  // namespace storage
