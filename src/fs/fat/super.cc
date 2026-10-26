#include "service/fs/fat/filesystem.h"

namespace vfs {

bool fat::Probe::detect(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 512 || data[510] != 0x55 || data[511] != 0xaa)
        return false;
    constexpr char name[] = "FAT32   ";
    for (uint32_t index = 0; index < 8; ++index) {
        if (data[82 + index] != static_cast<uint8_t>(name[index])) return false;
    }
    return true;
}

}  // namespace vfs
