#include "../format.h"

namespace fs {

bool probeIso9660(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 7 || data[0] != 1) return false;
    constexpr char magic[] = "CD001";
    for (uint32_t index = 0; index < 5; ++index) {
        if (data[index + 1] != static_cast<uint8_t>(magic[index])) return false;
    }
    return data[6] == 1;
}

}  // namespace fs
