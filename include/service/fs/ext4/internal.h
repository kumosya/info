#pragma once

#include <cstddef>
#include <cstdint>

namespace fs::ext4::detail {

class DiskEncoding {
   public:
    static uint16_t read16(const uint8_t* data) {
        return static_cast<uint16_t>(data[0]) |
               (static_cast<uint16_t>(data[1]) << 8);
    }

    static uint32_t read32(const uint8_t* data) {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }

    static uint64_t read64(const uint8_t* data) {
        return static_cast<uint64_t>(read32(data)) |
               (static_cast<uint64_t>(read32(data + 4)) << 32);
    }

    static uint64_t read48(const uint8_t* data) {
        return static_cast<uint64_t>(read32(data)) |
               (static_cast<uint64_t>(read16(data + 4)) << 32);
    }

    static void write16(uint8_t* data, uint16_t value) {
        data[0] = static_cast<uint8_t>(value);
        data[1] = static_cast<uint8_t>(value >> 8);
    }

    static void write32(uint8_t* data, uint32_t value) {
        for (uint32_t index = 0; index < 4; ++index) {
            data[index] = static_cast<uint8_t>(value >> (index * 8));
        }
    }

    static uint64_t readFreeBlocks(const uint8_t* superblock) {
        return read32(superblock + 12);
    }

    static void writeFreeBlocks(uint8_t* superblock, uint64_t count) {
        write32(superblock + 12, static_cast<uint32_t>(count));
    }

    static uint32_t readFreeInodes(const uint8_t* superblock) {
        return read32(superblock + 16);
    }

    static void writeFreeInodes(uint8_t* superblock, uint32_t count) {
        write32(superblock + 16, count);
    }

    static uint32_t extentLength(const uint8_t* entry) {
        const uint16_t length = read16(entry + 4);
        return length == 0 ? 32768 : (length > 32768 ? length - 32768 : length);
    }

    static void setExtent(uint8_t* entry, uint32_t logical, uint64_t physical,
                          uint16_t length) {
        write32(entry, logical);
        write16(entry + 4, length);
        write16(entry + 6, static_cast<uint16_t>(physical >> 32));
        write32(entry + 8, static_cast<uint32_t>(physical));
    }

    static uint16_t alignedDirectoryEntry(uint32_t length) {
        return static_cast<uint16_t>((length + 3U) & ~3U);
    }

    static bool namesEqual(const char* left, const char* right, size_t length) {
        for (size_t index = 0; index < length; ++index) {
            if (left[index] != right[index]) return false;
        }
        return true;
    }
};

}  // namespace fs::ext4::detail
