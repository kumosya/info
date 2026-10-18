#include "../format.h"

namespace vfs {

uint64_t FormatProbe::isoBlockOffset(uint32_t block) {
    return static_cast<uint64_t>(block) * 2048;
}

}  // namespace vfs
