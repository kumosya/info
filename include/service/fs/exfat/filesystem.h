#pragma once
#include <cstddef>
#include <cstdint>

namespace vfs::exfat {
class Probe {
   public:
    bool detect(const uint8_t* data, size_t size);
};
}  // namespace vfs::exfat
