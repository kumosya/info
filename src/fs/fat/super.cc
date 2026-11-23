#include "service/fs/fat/filesystem.h"
#include "service/fs/mount.h"

namespace fs {

    bool fat::Probe::detect() {
        const uint8_t* data = data_;
        const size_t size = size_;
        if (data == nullptr || size < 512 || data[510] != 0x55 ||
            data[511] != 0xaa) {
            return false;
        }

        constexpr char name[] = "FAT32   ";
        for (uint32_t index = 0; index < 8; ++index) {
            if (data[82 + index] != static_cast<uint8_t>(name[index])) {
                return false;
            }
        }

        return true;
    }

}  // namespace fs
