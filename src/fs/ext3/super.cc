#include "service/fs/ext3/filesystem.h"

namespace vfs {

bool ext3::Probe::detect(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 1124) return false;
    const uint8_t* super = data + 1024;
    const uint16_t magic = static_cast<uint16_t>(super[56]) |
                           (static_cast<uint16_t>(super[57]) << 8);
    const uint32_t features = static_cast<uint32_t>(super[92]) |
                              (static_cast<uint32_t>(super[93]) << 8) |
                              (static_cast<uint32_t>(super[94]) << 16) |
                              (static_cast<uint32_t>(super[95]) << 24);
    const uint32_t incompatible = static_cast<uint32_t>(super[96]) |
                                  (static_cast<uint32_t>(super[97]) << 8) |
                                  (static_cast<uint32_t>(super[98]) << 16) |
                                  (static_cast<uint32_t>(super[99]) << 24);
    return magic == 0xef53 && (features & 0x4U) != 0 &&
           (incompatible & 0x40U) == 0;
}


}  // namespace vfs
