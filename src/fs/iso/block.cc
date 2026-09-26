#include "../format.h"

namespace fs {

uint64_t isoBlockOffset(uint32_t block) {
    return static_cast<uint64_t>(block) * 2048;
}

}  // namespace fs
