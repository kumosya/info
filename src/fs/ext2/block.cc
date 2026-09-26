#include "../format.h"

namespace fs {

uint64_t extBlockOffset(uint32_t block, uint32_t blockSize) {
    return static_cast<uint64_t>(block) * blockSize;
}

}  // namespace fs
