#pragma once

#include <cstddef>
#include <cstdint>

namespace fs::ext3::detail {

inline constexpr uint32_t sectorSize = 512;
inline constexpr uint32_t directBlockCount = 12;
inline constexpr uint32_t singlyIndirectIndex = 12;
inline constexpr uint32_t doublyIndirectIndex = 13;
inline constexpr uint32_t triplyIndirectIndex = 14;
inline constexpr uint32_t maximumBlockSize = 4096;

class DirectoryEncoding {
 public:
static uint16_t alignedEntrySize(uint32_t nameLength) {
    return static_cast<uint16_t>((sizeof(RawDirectoryEntry) + nameLength + 3) & ~3U);
}

static bool namesEqual(const char* left, const char* right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}
};

}  // namespace fs::ext3::detail
