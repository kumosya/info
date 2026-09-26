#include "../format.h"

namespace fs {

bool probeExt3(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 1120) return false;
    const uint8_t* super = data + 1024;
    const uint16_t magic = static_cast<uint16_t>(super[56]) |
                           (static_cast<uint16_t>(super[57]) << 8);
    const uint32_t features = static_cast<uint32_t>(super[92]) |
                              (static_cast<uint32_t>(super[93]) << 8) |
                              (static_cast<uint32_t>(super[94]) << 16) |
                              (static_cast<uint32_t>(super[95]) << 24);
    return magic == 0xef53 && (features & 0x4U) != 0;
}

}  // namespace fs
